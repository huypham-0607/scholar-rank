"""Tokenize document, generate a posting list.

For the scope of this project, we are only working with English-written
paper, to reduce complications during tokenization phase.

"""

import io
import struct
import duckdb as db
import numpy as np
import pandas as pd

from pathlib import Path
from collections.abc import Callable
from scholar_rank import get_logger

logger = get_logger(__name__)

class Tokenizer:

    def __init__(
        self
    ):
        self.stop_word_list = r"\b(i|me|my|myself|we|our|ours|ourselves|you|your|yours|yourself|yourselves|he|him|his|himself|she|her|hers|herself|it|its|itself|they|them|their|theirs|themselves|what|which|who|whom|this|that|these|those|am|is|are|was|were|be|been|being|have|has|had|having|do|does|did|doing|a|an|the|and|but|if|or|because|as|until|while|of|at|by|for|with|about|against|between|into|through|during|before|after|above|below|to|from|up|down|in|out|on|off|over|under|again|further|then|once|here|there|when|where|why|how|all|any|both|each|few|more|most|other|some|such|no|nor|not|only|own|same|so|than|too|very|s|t|can|will|just|don|should|now)\b"

    def read_doc_id_lookup(
        self,
        lookup_path: Path
    ):
        """Read back the raw_id <-> mapped_id table written by build_doc_id_lookup.

        Reads the fixed-width binary layout build_doc_id_lookup wrote: each
        record is a little-endian int64 raw_id immediately followed by a
        little-endian int32 mapped_id (struct format '<qi'), back to back,
        no delimiters or count header.

        Args:
            lookup_path: Exact file path to the lookup table (not its directory).

        Returns:
            DuckDB relation with raw_id/mapped_id columns, same shape as
            build_doc_id_lookup's return value.
        """
        dtype = np.dtype([("raw_id", "<i8"), ("mapped_id", "<i4")])
        df = pd.DataFrame(np.fromfile(lookup_path, dtype=dtype))

        con = db.connect()
        doc_id_map = con.sql("SELECT raw_id, mapped_id FROM df")
        return doc_id_map

    def build_doc_id_lookup(
        self,
        con,
        db_path: Path,
        lookup_out_path: Path,
        lookup_file_name: str,
        row_per_chunk: int
    ):
        """Rank raw_id (OpenAlex numeric id) into a dense mapped_id in [0,N).

        raw_id is sparse and can exceed int32, so downstream structures
        (posting lists, doc lengths, graph adjacency) index by mapped_id
        instead, giving them O(1) flat-array access. Writes the
        raw_id <-> mapped_id table to lookup_out_path/lookup_file_name and
        returns the mapping relation so callers can join against it.
        """
        logger.info(f"Building doc_id lookup table from {db_path}.")

        doc_id_map = con.sql(f"""
            SELECT
                id AS raw_id,
                (ROW_NUMBER() OVER (ORDER BY id ASC) - 1)::INTEGER AS mapped_id
            FROM (
                SELECT regexp_replace(id, 'W', '')::BIGINT AS id
                FROM read_parquet('{db_path}/**/*.parquet')
            )
            ORDER BY raw_id ASC
        """)

        lookup_out_path.mkdir(parents=True, exist_ok=True)
        lookup_path = lookup_out_path / lookup_file_name
        logger.info(f"Writing doc_id lookup table to {lookup_path}.")

        buf = io.BytesIO()
        batch = doc_id_map.fetchmany(row_per_chunk)
        with open(lookup_path, "wb") as out_file:
            while batch:
                for raw_id, mapped_id in batch:
                    buf.write(struct.pack('<qi', raw_id, mapped_id))

                out_file.write(buf.getvalue())
                buf.seek(0); buf.truncate()
                batch = doc_id_map.fetchmany(row_per_chunk)

        logger.info(f"Finished writing doc_id lookup table ({lookup_path}).")
        return doc_id_map

    def get_token(
        self,
        db_path: Path,
        out_path: Path,
        lookup_out_path: Path,
        token_stream_file_name: Callable[[int], str],
        lookup_file_name: str,
        spill_path: Path,
        row_per_chunk: int = (1<<18),
        chunk_per_file: int = (1<<8)
    ):
        con = db.connect(config={"temp_directory": str(spill_path)})

        logger.info(f"Start serializing corpus from {db_path}.")

        doc_id_map = self.build_doc_id_lookup(con, db_path, lookup_out_path, lookup_file_name, row_per_chunk)

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
            FROM read_parquet('{db_path}/**/*.parquet')
            JOIN doc_id_map m ON regexp_replace(id, 'W', '')::BIGINT = m.raw_id
        """)

        logger.info(f"Creating _token_stream table...")

        rel = con.sql(""" SELECT id, tokens FROM tokenized ORDER BY id ASC """)
        con.sql(""" CREATE OR REPLACE TEMP TABLE _token_stream AS SELECT id, unnest(tokens) AS token FROM rel """)

        logger.info(f"Finished creating _token_stream table.")

        token_stream = con.sql("SELECT id, token FROM _token_stream")

        buf = io.BytesIO()

        file_count = 0

        no_tokens = 0
        max_token_length = 0
        
        logger.info(f"Begin serializing token to {str(out_path)}.")

        batch = token_stream.fetchmany(row_per_chunk)
        out_path.mkdir(parents=True, exist_ok=True)
        while batch:
            chunk_count = 0
            file_name = token_stream_file_name(file_count)
            logger.info(f"Outputting stream into file {file_name}.")

            with open(f"{str(out_path)}/{file_name}", "wb") as out_file:
                while batch and chunk_count < chunk_per_file:
                    for doc_id, token in batch:
                        # Convert to bytes object
                        encoded = token.encode("utf-8")

                        no_tokens += 1
                        max_token_length = max(max_token_length, len(encoded))

                        # Convert into little endian signed ll, usigned short
                        buf.write(struct.pack('<qH', doc_id, len(encoded)))
                        buf.write(encoded)

                    out_file.write(buf.getvalue())

                    buf.seek(0); buf.truncate()
                    chunk_count += 1
                    batch = token_stream.fetchmany(row_per_chunk)

            file_count += 1
        logger.info(f"Finished serializing corpus from {db_path}.")
        logger.info(f"No of tokens: {no_tokens}")
        logger.info(f"Max token length = {max_token_length}")

    def tokenize_query(self, query: str) -> list[str]:
        con = db.connect()

        res = con.sql(f"""
            SELECT list_transform(
                regexp_extract_all(
                    regexp_replace(lower($query), '{self.stop_word_list}', ' ', 'g'),
                    '[a-z0-9]+'
                ),
                token -> stem(token, 'english')
            ) AS tokens
        """, params={"query": query}).fetchone()

        return res[0]

def main():
    con = db.connect(config={"temp_directory": str(spill_path)})
    con.execute(
        f"""
        SELECT name, value FROM duckdb_settings()
        WHERE name IN ('threads','worker_threads','preserve_insertion_order','memory_limit');
        """
    )

if __name__ == "__main__":
    main()