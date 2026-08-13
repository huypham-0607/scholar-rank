"""Makes a smaller Works subset from the full corpus, for development and testing.

Driven by the CLI (scholar-rank gen-works-subset) and project-config.toml.
"""

import duckdb as db
import math

from pathlib import Path
from scholar_rank import get_logger, get_current_time

logger = get_logger(__name__)


class WorksSubsetter:
    """Filters full_corpus_path/works by filter_condition, writes the result to subset_path.

    full_corpus_path is the corpus root (the folder that holds works/), not works/ itself.
    """

    def __init__(
        self,
        full_corpus_path: Path,
        subset_path: Path,
        filter_condition: str,
        rows_per_chunk: int = 450000
    ):
        logger.info("Initializing WorksSubsetter.")

        self.full_corpus_path = full_corpus_path
        self.subset_path = subset_path
        self.filter_condition = filter_condition
        self.rows_per_chunk = rows_per_chunk

        logger.info("Finished initializing WorksSubsetter.")

    def validate_database(self):
        """Checks subset_path against full_corpus_path: filter match, duplicate IDs, link counts.

        Writes database_validation_log.txt under subset_path.
        """
        errors = []

        con = db.connect()

        logger.info("Begin validating Database")

        ids = con.sql(f"""
            SELECT id
            FROM read_parquet('{self.full_corpus_path}/works/**/*.parquet')
            WHERE {self.filter_condition};

        """).fetchall()

        ids = set(id[0] for id in ids)
        present = set()

        # Re-runs filter_condition on each row instead of a fixed expected value, so
        # this check stays correct no matter which profile built the subset.
        rel = con.sql(f"""
            SELECT id, referenced_works, referenced_works_count,
            ({self.filter_condition}) AS matches_filter
            FROM read_parquet('{str(self.subset_path)}/**/*.parquet')
        """)

        logger.info("First pass: Filter-condition + Duplicate ID + link mismatch check.")

        while (row := rel.fetchone()) is not None:
            data = dict(zip(rel.columns, row))

            if not data["matches_filter"]:
                message = f"ID {data["id"]} does not satisfy the filter condition."
                logger.warning(message)
                errors.append(message)
                continue
            if data["id"] in present:
                message = f"Duplicate ID {data["id"]} detected in subset."
                logger.warning(message)
                errors.append(message)
                continue
            present.add(data["id"])
            if data["referenced_works_count"] != len(data["referenced_works"]):
                message = f"ID {data["id"]} contains link mismatch."
                logger.warning(message)
                errors.append(message)

        logger.info("Second pass: Missing valid subset entries check.")

        for id in ids:
            if id not in present:
                message = f"ID {id} present in full corpus but not in subset."
                logger.warning(message)
                errors.append(message)

        total_links = 0
        dangling_links = 0

        logger.info("Third pass: Total links + dangling links computation.")

        rel = con.sql(f"""
            SELECT id, referenced_works, referenced_works_count
            FROM read_parquet('{str(self.subset_path)}/**/*.parquet')
        """)

        while (row := rel.fetchone()) is not None:
            data = dict(zip(rel.columns, row))

            total_links += data["referenced_works_count"]

            dangling_links += len(set(data["referenced_works"]) - present)

        logger.info("Finished validating Database")
        logger.info(f"Total nodes: {len(present)}.")
        logger.info(f"Total links: {total_links}.")
        logger.info(f"Total dangling links: {dangling_links}.")
        logger.info(f"Total valid links: {total_links-dangling_links}.")
        logger.info(f"Total errors: {len(errors)}.")

        try:
            with open(self.subset_path/"database_validation_log.txt", "w", encoding="utf-8") as f:
                f.write(f"Time created: {get_current_time()}.\n")
                f.write(f"Total nodes: {len(present)}.\n")
                f.write(f"Total links: {total_links}.\n")
                f.write(f"Total dangling links: {dangling_links}.\n")
                f.write(f"Total valid links: {total_links-dangling_links}.\n")
                f.write(f"Total errors: {len(errors)}.\n")
                for error in errors:
                    f.write(f"{error}\n")
        except Exception as e:
            logger.warning(f"Failed to open database_validation_log.txt: {e}")
            raise

    def subset_database(self):
        """Filters full_corpus_path/works by filter_condition, writes subset_path in chunks."""
        logger.info("Connecting to DB.")
        self.subset_path.mkdir(parents=True, exist_ok=True)

        con = db.connect()
        logger.info("Established connection to DB")

        logger.info("Started fetching subset.")

        con.sql(f"""
            CREATE OR REPLACE TEMP TABLE _subset AS
            SELECT *
            FROM read_parquet('{self.full_corpus_path}/works/**/*.parquet')
            WHERE {self.filter_condition}
        """)

        n_rows = con.sql("SELECT count(*) FROM _subset").fetchone()[0]
        n_chunks = math.ceil(n_rows / self.rows_per_chunk)
        logger.info(f"Filtered subset: {n_rows} rows -> {n_chunks} chunks of ~{self.rows_per_chunk} rows each")

        for i in range(n_chunks):
            con.sql(f"""
                COPY (SELECT * FROM _subset WHERE hash(id) % {n_chunks} = {i})
                TO '{str(self.subset_path)}/part_{i:04d}.parquet'
                (FORMAT PARQUET, COMPRESSION zstd)
            """)

        con.sql("DROP TABLE _subset")

        logger.info("Subset saved")
