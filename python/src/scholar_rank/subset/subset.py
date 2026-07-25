"""Subsetting full corpus for convenient testing.

"""

import duckdb as db
import numpy as np
import json

from scholar_rank.ingest.fetch_data import get_manifest_data
from dataclasses import dataclass
from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT, get_current_time

logger = get_logger(__name__)

class Subsetter:

    # ____________________ CONSTANTS __________________________

    def __init__(self):
        
        logger.info("Initializing Subsetter.")
        self.COMPACT_PATH = PROJECT_ROOT/'data'/'compact'
        self.DB_PATH = Path("/data")
        self.MANIFEST_PATH = PROJECT_ROOT/'data'/'compact'/'works'/"compact_manifest.json"

        logger.info("Finished initializing Subsetter.")

    # Debug function
    def validate_database(self):
        errors = []

        con = db.connect(str(self.DB_PATH/'math_subset.duckdb'))

        logger.info("Begin validating Database")

        ids = con.sql(f"""
            SELECT id
            FROM read_parquet('{self.COMPACT_PATH}/works/**/*.parquet')
            WHERE topics[1].field_display_name = 'Mathematics';

        """).fetchall()

        ids = set(id[0] for id in ids)
        present = set()

        rel = con.sql(f"""
            SELECT id, referenced_works, referenced_works_count,
            topics[1].field_display_name as field
            FROM works
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
            FROM works
        """)

        while (row := rel.fetchone()) is not None:
            data = dict(zip(rel.columns,row))

            total_links += data["referenced_works_count"]

            dangling_links += len(set(data["referenced_works"]) - present)

        logger.info("Finished validating Database")
        logger.info(f"Total nodes: {len(present)}.")
        logger.info(f"Total links: {total_links}.")
        logger.info(f"Total dangling links: {dangling_links}")
        logger.info(f"Total valid links: {total_links-dangling_links}")
        logger.info(f"Total errors: {len(errors)}")

        try:
            with open(self.DB_PATH/"database_validation_log.txt", "w", encoding="utf-8") as f:
                f.write(f"Time created: {get_current_time()}\n")
                for error in errors:
                    f.write(f"{error}\n")
        except Exception as e:
            logger.warning(f"Failed to open extraction_log.txt: {e}")
            raise

    def subset_mathematics(self):
        logger.info("Connecting to DB.")
        con = db.connect(str(self.DB_PATH / 'math_subset.duckdb'))
        logger.info("Established connection to DB")

        logger.info("Started fetching subset.")
        con.sql(f"""
            CREATE TABLE works AS
            SELECT * EXCLUDE (abstract_inverted_index)
            FROM read_parquet('{self.COMPACT_PATH}/works/**/*.parquet')
            WHERE topics[1].field_display_name = 'Mathematics';

            CREATE INDEX idx_id ON works (id);
        """)
    
        logger.info("Subset saved")

COMPACT_PATH = PROJECT_ROOT/'data'/'compact'
DB_PATH = "/data/math_subset.duckdb"

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


    subsetter = Subsetter()
    if not Path(DB_PATH).is_file():
        subsetter.subset_mathematics()
    subsetter.validate_database()

if __name__ == "__main__":
    main()