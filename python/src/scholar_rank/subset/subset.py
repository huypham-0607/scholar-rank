"""Subsetting full corpus for convenient testing.

"""

import duckdb as db
import numpy as np
import json
import math

from scholar_rank.ingest.fetch_data import get_manifest_data
from dataclasses import dataclass
from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT, get_current_time

logger = get_logger(__name__)

class Subsetter:

    # ____________________ CONSTANTS __________________________

    def __init__(
        self,
        compact_path: Path,
        db_path: Path,
        filter_condition: str,
        rows_per_chunk: int = 450000
    ):
        logger.info("Initializing Subsetter.")
        
        self.COMPACT_PATH = compact_path
        self.DB_PATH = db_path
        self.FILTER_CONDITION = filter_condition
        self.ROWS_PER_CHUNK = rows_per_chunk

        logger.info("Finished initializing Subsetter.")

    # Debug function
    def validate_database(self):
        errors = []

        con = db.connect()

        logger.info("Begin validating Database")

        ids = con.sql(f"""
            SELECT id
            FROM read_parquet('{self.COMPACT_PATH}/works/**/*.parquet')
            WHERE {self.FILTER_CONDITION};

        """).fetchall()

        ids = set(id[0] for id in ids)
        present = set()

        rel = con.sql(f"""
            SELECT id, referenced_works, referenced_works_count,
            topics[1].field_display_name as field
            FROM read_parquet('{str(self.DB_PATH)}/**/*.parquet')
        """)

        logger.info("First pass: Field + Duplicate ID + link mismatch check.")

        while (row := rel.fetchone()) is not None:
            data = dict(zip(rel.columns,row))

            if data["field"] != "Mathematics":
                message = f"ID {data["id"]} is not in Mathematics field."
                logger.warning(message)
                errors.append(message)
                continue
            if data["id"] in present:
                message = f"Duplicate ID {data["id"]} detected in math_subset.duckdb."
                logger.warning(message)
                errors.append(message)
                continue
            present.add(data["id"])
            if data["referenced_works_count"] != len(data["referenced_works"]):
                message = f"ID {data["id"]} contains link mismatch."
                logger.warning(message)
                errors.append(message)

        logger.info("Second pass: Missing valid Mathematics entries check.")

        for id in ids:
            if id not in present:
                message = f"ID {data["id"]} present in compact corpus but not in math_subset.duckdb."
                logger.warning(message)
                errors.append(message)

        total_links = 0
        dangling_links = 0

        logger.info("Third pass: Total links + dangling links computation.")

        rel = con.sql(f"""
            SELECT id, referenced_works, referenced_works_count
            FROM read_parquet('{str(self.DB_PATH)}/**/*.parquet')
        """)

        while (row := rel.fetchone()) is not None:
            data = dict(zip(rel.columns,row))

            total_links += data["referenced_works_count"]

            dangling_links += len(set(data["referenced_works"]) - present)

        logger.info("Finished validating Database")
        logger.info(f"Total nodes: {len(present)}.")
        logger.info(f"Total links: {total_links}.")
        logger.info(f"Total dangling links: {dangling_links}.")
        logger.info(f"Total valid links: {total_links-dangling_links}.")
        logger.info(f"Total errors: {len(errors)}.")

        try:
            with open(self.DB_PATH/"database_validation_log.txt", "w", encoding="utf-8") as f:
                f.write(f"Time created: {get_current_time()}.\n")
                f.write(f"Total nodes: {len(present)}.\n")
                f.write(f"Total links: {total_links}.\n")
                f.write(f"Total dangling links: {dangling_links}.\n")
                f.write(f"Total valid links: {total_links-dangling_links}.\n")
                f.write(f"Total errors: {len(errors)}.\n")
                for error in errors:
                    f.write(f"{error}\n")
        except Exception as e:
            logger.warning(f"Failed to open extraction_log.txt: {e}")
            raise

    def subset_database(self):
        logger.info("Connecting to DB.")
        print(type(self.DB_PATH))
        (self.DB_PATH).mkdir(parents=True, exist_ok=True)
        
        con = db.connect()
        logger.info("Established connection to DB")

        logger.info("Started fetching subset.")

        con.sql(f"""
            CREATE OR REPLACE TEMP TABLE _subset AS
            SELECT * EXCLUDE abstract_inverted_index
            FROM read_parquet('{self.COMPACT_PATH}/works/**/*.parquet')
            WHERE {self.FILTER_CONDITION}
        """)

        n_rows = con.sql("SELECT count(*) FROM _subset").fetchone()[0]
        n_chunks = math.ceil(n_rows / self.ROWS_PER_CHUNK)
        logger.info(f"Filtered subset: {n_rows} rows -> {n_chunks} chunks of ~{self.ROWS_PER_CHUNK} rows each")

        for i in range(n_chunks):
            con.sql(f"""
                COPY (SELECT * FROM _subset WHERE hash(id) % {n_chunks} = {i})
                TO '{str(self.DB_PATH)}/part_{i:04d}.parquet'
                (FORMAT PARQUET, COMPRESSION zstd)
            """)

        con.sql("DROP TABLE _subset")

        logger.info("Subset saved")

COMPACT_PATH = Path(PROJECT_ROOT/'data'/'compact')
DB_PATH = Path('/data')

def main():
    # logger.info("Connecting to DB.")
    # con = db.connect(DB_PATH)
    # logger.info("Established connection to DB")

    # logger.info("Started fetching count.")
    # count = con.sql(f"""
    #     SELECT field, count(*) AS freq 
    #     FROM (
    #         SELECT *, topics[1].field_display_name AS field
    #         FROM read_parquet('{self.COMPACT_PATH}/works/**/*.parquet')
    #     )
    #     GROUP BY field
    # """).fetchall()
    # logger.info("Fetched count")

    # print(count)
    # con.close()

    label = 'math_english'
    condition = "topics[1].field_display_name = 'Mathematics' AND language = 'en'"

    subsetter = Subsetter(COMPACT_PATH, DB_PATH/label, condition)
    subsetter.subset_database()
    subsetter.validate_database()

if __name__ == "__main__":
    main()