"""Tokenize document, generate a posting list.

For the scope of this project, we are only working with English-written
paper, to reduce complications during tokenization phase.

"""

import io
import struct
import duckdb as db

from pathlib import Path
from scholar_rank.utils import get_logger

logger = get_logger(__name__)

class Tokenizer:

    def __init__(
        self,
        db_path: Path,
        out_path: Path,
        lookup_out_path: Path,
        row_per_chunk: int = (1<<18),
        chunk_per_file: int = (1<<8)
    ):
        self.STOP_WORD_LIST = r"\b(i|me|my|myself|we|our|ours|ourselves|you|your|yours|yourself|yourselves|he|him|his|himself|she|her|hers|herself|it|its|itself|they|them|their|theirs|themselves|what|which|who|whom|this|that|these|those|am|is|are|was|were|be|been|being|have|has|had|having|do|does|did|doing|a|an|the|and|but|if|or|because|as|until|while|of|at|by|for|with|about|against|between|into|through|during|before|after|above|below|to|from|up|down|in|out|on|off|over|under|again|further|then|once|here|there|when|where|why|how|all|any|both|each|few|more|most|other|some|such|no|nor|not|only|own|same|so|than|too|very|s|t|can|will|just|don|should|now)\b"
        self.DB_PATH = db_path
        self.OUT_PATH = out_path
        self.LOOKUP_OUT_PATH = lookup_out_path
        self.ROW_PER_CHUNK = row_per_chunk
        self.CHUNK_PER_FILE = chunk_per_file

    def build_doc_id_lookup(self, con):
        """Rank raw_id (OpenAlex numeric id) into a dense mapped_id in [0,N).

        raw_id is sparse and can exceed int32, so downstream structures
        (posting lists, doc lengths, graph adjacency) index by mapped_id
        instead, giving them O(1) flat-array access. Writes the
        raw_id <-> mapped_id table to LOOKUP_OUT_PATH/doc_id_lookup.bin and
        returns the mapping relation so callers can join against it.
        """
        logger.info(f"Building doc_id lookup table from {self.DB_PATH}.")

        doc_id_map = con.sql(f"""
            SELECT
                id AS raw_id,
                (ROW_NUMBER() OVER (ORDER BY id ASC) - 1)::INTEGER AS mapped_id
            FROM (
                SELECT regexp_replace(id, 'W', '')::BIGINT AS id
                FROM read_parquet('{self.DB_PATH}/**/*.parquet')
            )
            ORDER BY raw_id ASC
        """)

        self.LOOKUP_OUT_PATH.mkdir(parents=True, exist_ok=True)
        lookup_path = self.LOOKUP_OUT_PATH / "doc_id_lookup.bin"
        logger.info(f"Writing doc_id lookup table to {lookup_path}.")

        buf = io.BytesIO()
        batch = doc_id_map.fetchmany(self.ROW_PER_CHUNK)
        with open(lookup_path, "wb") as out_file:
            while batch:
                for raw_id, mapped_id in batch:
                    buf.write(struct.pack('<qi', raw_id, mapped_id))

                out_file.write(buf.getvalue())
                buf.seek(0); buf.truncate()
                batch = doc_id_map.fetchmany(self.ROW_PER_CHUNK)

        logger.info(f"Finished writing doc_id lookup table ({lookup_path}).")
        return doc_id_map

    def get_token(self):
        con = db.connect()

        logger.info(f"Start serializing corpus from {self.DB_PATH}.")

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
                            '{self.STOP_WORD_LIST}',
                            ' ',
                            'g'
                        ),
                        '[a-z0-9]+'
                    ), 
                    token -> stem(token,'english')
                ) AS tokens
            FROM read_parquet('{self.DB_PATH}/**/*.parquet')
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

        batch = token_stream.fetchmany(self.ROW_PER_CHUNK)
        self.OUT_PATH.mkdir(parents=True, exist_ok=True)
        while batch:
            chunk_count = 0
            file_name = f"token_{file_count:04d}.bin"
            logger.info(f"Outputting stream into file {file_name}.")

            with open(f"{str(self.OUT_PATH)}/{file_name}", "wb") as out_file:
                while batch and chunk_count < self.CHUNK_PER_FILE:
                    for doc_id, token in batch:
                        # Convert to bytes object
                        encoded = token.encode("utf-8")

                        # Convert into little endian signed ll, usigned short
                        buf.write(struct.pack('<qH', doc_id, len(encoded)))
                        buf.write(encoded)

                    out_file.write(buf.getvalue())
                
                    buf.seek(0); buf.truncate()
                    chunk_count += 1
                    batch = token_stream.fetchmany(self.ROW_PER_CHUNK)

            file_count += 1
        logger.info(f"Finished serializing corpus from {self.DB_PATH}.")

def main():
    DB_PATH = Path('/data')

    label = 'math_english'

    tokenizer = Tokenizer(
        DB_PATH/label,
        DB_PATH/label/"token_stream",
        DB_PATH/label/"doc_id_lookup"
    )
    tokenizer.get_token()

if __name__ == "__main__":
    main()