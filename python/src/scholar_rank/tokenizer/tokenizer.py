"""Tokenize document, generate a posting list.

For the scope of this project, we are only working with English-written
paper, to reduce complications during tokenization phase.

"""

import io
import struct
import duckdb as db

from pathlib import Path
from collections.abc import Callable
from scholar_rank import get_logger

logger = get_logger(__name__)

class Tokenizer:

    def __init__(
        self,
        db_path: Path,
        out_path: Path,
        lookup_out_path: Path,
        token_stream_file_name: Callable[[int], str],
        lookup_file_name: str,
        row_per_chunk: int = (1<<18),
        chunk_per_file: int = (1<<8)
    ):
        self.stop_word_list = r"\b(i|me|my|myself|we|our|ours|ourselves|you|your|yours|yourself|yourselves|he|him|his|himself|she|her|hers|herself|it|its|itself|they|them|their|theirs|themselves|what|which|who|whom|this|that|these|those|am|is|are|was|were|be|been|being|have|has|had|having|do|does|did|doing|a|an|the|and|but|if|or|because|as|until|while|of|at|by|for|with|about|against|between|into|through|during|before|after|above|below|to|from|up|down|in|out|on|off|over|under|again|further|then|once|here|there|when|where|why|how|all|any|both|each|few|more|most|other|some|such|no|nor|not|only|own|same|so|than|too|very|s|t|can|will|just|don|should|now)\b"
        self.db_path = db_path
        self.out_path = out_path
        self.lookup_out_path = lookup_out_path
        self.token_stream_file_name = token_stream_file_name
        self.lookup_file_name = lookup_file_name
        self.row_per_chunk = row_per_chunk
        self.chunk_per_file = chunk_per_file

    def build_doc_id_lookup(self, con):
        """Rank raw_id (OpenAlex numeric id) into a dense mapped_id in [0,N).

        raw_id is sparse and can exceed int32, so downstream structures
        (posting lists, doc lengths, graph adjacency) index by mapped_id
        instead, giving them O(1) flat-array access. Writes the
        raw_id <-> mapped_id table to lookup_out_path/lookup_file_name and
        returns the mapping relation so callers can join against it.
        """
        logger.info(f"Building doc_id lookup table from {self.db_path}.")

        doc_id_map = con.sql(f"""
            SELECT
                id AS raw_id,
                (ROW_NUMBER() OVER (ORDER BY id ASC) - 1)::INTEGER AS mapped_id
            FROM (
                SELECT regexp_replace(id, 'W', '')::BIGINT AS id
                FROM read_parquet('{self.db_path}/**/*.parquet')
            )
            ORDER BY raw_id ASC
        """)

        self.lookup_out_path.mkdir(parents=True, exist_ok=True)
        lookup_path = self.lookup_out_path / self.lookup_file_name
        logger.info(f"Writing doc_id lookup table to {lookup_path}.")

        buf = io.BytesIO()
        batch = doc_id_map.fetchmany(self.row_per_chunk)
        with open(lookup_path, "wb") as out_file:
            while batch:
                for raw_id, mapped_id in batch:
                    buf.write(struct.pack('<qi', raw_id, mapped_id))

                out_file.write(buf.getvalue())
                buf.seek(0); buf.truncate()
                batch = doc_id_map.fetchmany(self.row_per_chunk)

        logger.info(f"Finished writing doc_id lookup table ({lookup_path}).")
        return doc_id_map

    def get_token(self):
        con = db.connect()

        logger.info(f"Start serializing corpus from {self.db_path}.")

        doc_id_map = self.build_doc_id_lookup(con)

        tokenized = con.sql(f"""
            INSTALL fts;
            LOAD fts;
            SELECT
                m.mapped_id AS id,
                list_transform(
                    regexp_extract_all(
                        regexp_replace(
                            lower(concat_ws(' ',
                                coalesce(title, ''),
                                list_aggregate(list_transform(topics, t -> t.display_name), 'string_agg', ' '),
                                list_aggregate(list_transform(topics, t -> t.subfield_display_name), 'string_agg', ' '),
                                list_aggregate(list_transform(topics, t -> t.field_display_name), 'string_agg', ' '),
                                list_aggregate(list_transform(topics, t -> t.domain_display_name), 'string_agg', ' '),
                                list_aggregate(list_transform(keywords, k -> k.display_name), 'string_agg', ' ')
                            )),
                            '{self.stop_word_list}',
                            ' ',
                            'g'
                        ),
                        '[a-z0-9]+'
                    ),
                    token -> stem(token,'english')
                ) AS tokens
            FROM read_parquet('{self.db_path}/**/*.parquet')
            JOIN doc_id_map m ON regexp_replace(id, 'W', '')::BIGINT = m.raw_id
        """)

        token_stream = con.sql(f"""
            SELECT id, unnest(tokens) AS token FROM tokenized
            ORDER BY id ASC
        """)

        logger.info(con.sql("SELECT count(*) FROM token_stream").fetchone())

        # Hardcoded here for debugging.
        token_list = con.sql(f"""
            COPY token_stream
            TO '{"/data/cached"}/token_data.parquet'
            (FORMAT PARQUET, COMPRESSION zstd)
        """)

        buf = io.BytesIO()

        file_count = 0

        batch = token_stream.fetchmany(self.row_per_chunk)
        self.out_path.mkdir(parents=True, exist_ok=True)
        while batch:
            chunk_count = 0
            file_name = self.token_stream_file_name(file_count)
            logger.info(f"Outputting stream into file {file_name}.")

            with open(f"{str(self.out_path)}/{file_name}", "wb") as out_file:
                while batch and chunk_count < self.chunk_per_file:
                    for doc_id, token in batch:
                        # Convert to bytes object
                        encoded = token.encode("utf-8")

                        # Convert into little endian signed ll, usigned short
                        buf.write(struct.pack('<qH', doc_id, len(encoded)))
                        buf.write(encoded)

                    out_file.write(buf.getvalue())

                    buf.seek(0); buf.truncate()
                    chunk_count += 1
                    batch = token_stream.fetchmany(self.row_per_chunk)

            file_count += 1
        logger.info(f"Finished serializing corpus from {self.db_path}.")
