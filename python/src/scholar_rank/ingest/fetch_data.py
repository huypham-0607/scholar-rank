"""Fetching data from OpenAlex API, and saving it under data/compressed

    This module implements a manifest-based shard-by-shard data extraction for
    all data necessary for this project.

    This includes:
    - Works
    - Authors (Delegated)
    - Sources (Delegated)
    - Topics (Delegated)

    Data is expected to be saved inside ./data folder.

    This module also includes a resumation mechanism in cases part of the full database
    is already present on local machine. 

"""

import boto3
import duckdb
import json
import os

from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT
from botocore import UNSIGNED
from botocore.client import Config

logger = get_logger(__name__)

# Columns being extracted from raw shards
extracted_columns = [
    "id",
    "doi",
    "title",
    "authorships",
    "abstract_inverted_index",
    "type",
    "language",
    "primary_location",
    "publication_year",
    "publication_date",
    "referenced_works",
    "referenced_works_count",
    "cited_by_count",
    "topics"
]

# Columns present in final compact shard
columns = [
    "id",
    "doi",
    "title",
    "authorships",
    "authorships_truncated",
    "abstract_inverted_index",
    "type",
    "language",
    "primary_location",
    "publication_year",
    "publication_date",
    "referenced_works",
    "referenced_works_count",
    "cited_by_count",
    "topics"
]

entity = "works"

def get_manifest(upstream_path, dest_path: Path) -> None:
    """Getting manifest.json for an Entity from OpenAlex S3 storage.

        Args:
            upstream_path: pathlib.Path to manifest.json location in S3 bucket.
            dest_path: pathlib.Path to save location for manifest.json.

        Returns:
            None
    """

    s3 = boto3.client('s3', config=Config(signature_version=UNSIGNED))
    try:
        s3.download_file('openalex',upstream_path, dest_path)
    except Exception as e:
        logger.warning(f"Failed to fetch {upstream_path}.")
        raise

    logger.info(f"Fetched {upstream_path} successfully.")

def list_local_and_remote_shards(manifest_path: Path, raw_data_path: Path)->(list,list):
    """Listing all shards present and not present in raw data path for an Entity.

        raw_data_path should be the parent folder of all entity folders
        {raw_data_path}
            /works
            /authors
            /sources

        Args:
            manifest_path: pathlib.Path to manifest.json locally
            raw_data_path: pathlib.Path to raw data directory corresponding to manifest.json

        Returns:
            List of Paths of local and remote shards, or empty list if exception occured

    """

    data = None

    try:
        with open(manifest_path,'r') as f:
            data = json.load(f)
    except Exception as e:
        logger.warning(f"Unable to open manifest.json: {e}")
        raise
    
    remote_shard_list = []
    local_shard_list = []

    for obj in data["files"]:
        # Extracting only the directory of a shard (Hardcoded, subject to edits)
        url = obj["url"].replace('s3://openalex/data/parquet/', '').replace('s3://openalex/data/jsonl/', '')
        if (not Path(raw_data_path/url).is_file()):
            remote_shard_list.append(url)
        else:
            local_shard_list.append(url)
        
    return (local_shard_list,remote_shard_list)


def fetch_shard(upstream_path, dest_path: Path) -> None:
    """Fetching remote parquet files from S3 and save to dest

    Args:
        upstream_path: Path of shard on S3 storage
        dest_path: Path where remote shard is saved on local device.

    Returns:
        None

    """

    s3 = boto3.client('s3', config=Config(signature_version=UNSIGNED))
    try:
        s3.download_file('openalex',str(upstream_path), str(dest_path))
    except Exception as e:
        logger.warning(f"Failed to fetch {upstream_path}.")
        raise
    
    logger.info(f"Fetched {upstream_path} successfully.")

def show_shard(shard_path: Path):
    db = duckdb.connect();
    rel = db.read_parquet(shard_path)

    rel.show()

def extract_compact(shard_path, out_path, url: Path) -> dict:
    """Extract code metadata from each shard

    Details of metadata explained in data_reference.md

    Outstanding issues:
        authorships_truncated condition check is positioned after the actual
        authorships truncation. Need to confirm if this has 100% false rate.

    Args:
        shard_path: Local shard path for extraction.
        out_path: Output file path after extracting metadata.
        url: The key on Amazon S3. Used to append to metadata.

    Returns:
        A dictionary with following structure:

        {
            'url': Path,
            'meta': {
                'content_length': int
                'record_count': int
            }
        }

        This structure is reflective of that in 
    """

    db = duckdb.connect()
    rel = db.read_parquet(shard_path).select(*extracted_columns)

    # Number of records is an invariant after the transform. Count here for cheaper computation.
    record_count = rel.aggregate("count(*)").fetchone()[0]
    rel = rel.select("""
        * REPLACE (
            replace(id, 'https://openalex.org/', '') AS id,

            list_transform(
                CASE WHEN len(authorships) <= 3
                    THEN authorships
                    ELSE authorships[1:2] || [authorships[-1]]
                END,
                a -> {
                    'id': replace(a.author.id, 'https://openalex.org/', ''),
                    'display_name': a.author.display_name,
                    'raw_author_name': a.raw_author_name
                }
            ) AS authorships,

            CASE WHEN primary_location.source IS NULL THEN NULL
                ELSE {
                    'id': replace(primary_location.source.id, 'https://openalex.org/', ''),
                    'display_name': primary_location.source.display_name,
                    'type': primary_location.source.type,
                    'is_oa': primary_location.is_oa
                }
            END AS primary_location,

            list_transform(topics, a -> {
                'id': replace(a.id, 'https://openalex.org/', ''),
                'display_name': a.display_name,
                'score': a.score,
                'subfield_id': split_part(a.subfield.id, '/', -1),
                'subfield_display_name': a.subfield.display_name,
                'field_id': split_part(a.field.id, '/', -1),
                'field_display_name': a.field.display_name,
                'domain_id': split_part(a.domain.id, '/', -1),
                'domain_display_name': a.domain.display_name
            }) AS topics,

            replace(doi, 'https://doi.org/', '') AS doi,

            list_transform(referenced_works, a -> replace(a, 'https://openalex.org/', ''))
                AS referenced_works,
        ),
        coalesce(len(authorships), 0) > 3 AS authorships_truncated,
    """)

    out_path.parent.mkdir(parents = True, exist_ok = True)

    db.sql(f"""
        COPY (SELECT * FROM rel)
        TO '{out_path}'
        (FORMAT parquet, COMPRESSION zstd)
    """)

    files_data = {
        'url': url,
        'meta': {
            'content_length': os.path.getsize(out_path)
            'record_count': record_count
        }
    }

    return files_data

class ManifestData:
    key: Path               # Key (directory) of a file on S3 
    content_length: int     # File size
    record_count: int       # No of records in shard

    def __init__(
        self,
        key,
        content_length,
        record_count
    ):
        self.key = key
        self.content_length = content_length
        self.record_count = record_count

def get_manifest_data(manifest_path: Path) -> list:
    """Getting all files metadata from manifest.json.

    This method strips away 's3://...' prefix for url in each files,
    Leaving only the path relative to root (key) of S3 bucket.

    This is useful for both fetching files with boto3 or act as file directory
    for local saves.

    This method hardcoded the structure of manifest.json fetched from S3.
    Keep that in mind for future maintainence.

    Args:
        manifest_path: Path to the desired manifest.json

    Returns:
        A list of ManifestData 

    """
    try:
        with open(manifest_path, 'r') as f:
            manifest_data = json.load(f)
    except Exception as e:
        logger.warning(f"Unable to open manifest.json: {e}")
        raise

    # Getting all files metadata in manifest.json, and stripping s3 prefix.
    lst = [ManifestData(
        Path(obj["url"].replace('s3://openalex/data/parquet/', '')),
        obj['meta']['content_length'],
        obj['meta']['record_count']
    ) for obj in data["files"]]

    return lst

@dataclass
class ShardValidationResult:
    raw_content_length: int
    compact_content_length: int
    raw_record_count: int
    compact_record_count: int
    link_mismatch_count: int
    is_valid_schema: bool
    missing_columns: list[str]
    redundant_columns: list[str]

    def __init__(
        self,
        raw_content_length,
        compact_content_length,
        raw_record_count,
        compact_record_count,
        link_mismatch_count,
        is_valid_schema,
        missing_columns,
        redundant_columns
    ):
        self.raw_content_length = raw_content_length
        self.compact_content_length = compact_content_length
        self.raw_record_count = raw_record_count
        self.compact_record_count = compact_record_count
        self.link_mismatch_count = link_mismatch_count
        self.is_valid_schema = is_valid_schema
        self.missing_columns = missing_columns
        self.redundant_columns = redundant_columns

def validate_compact_shard(shard_path: Path, content_length, record_count: int):
    """Validate data on a per-shard basis

    This check should handle these following conditions
    - Validate compact entry count relative to raw entry counts
    - Check for schema integrity (missing/redundant columns)
    - Check referenced_works size matches that of referenced_works_count (Important check)
    - Compute reduction in file size

    Args:
        shard_path: Path to shard being validated
        content_length: File size of raw file (embedded in manifest.json)
        record_count: Record count of raw file (embedded in manifest.json)

    Returns:
        A ShardValidationResult object describing validation result.

    """
    
    db = duckdb.connect()

    rel = db.read_parquet(shard_path)
    cols = rel.columns
    compact_record_count = rel.aggregate("count(*) AS total").fetchone()[0]
    compact_content_length = os.path.getsize(shard_path)

    # referenced_works_count vs actual list length - NULL counts as a mismatch
    link_mismatch_count = rel.aggregate(
        "count(*) FILTER (WHERE referenced_works_count IS DISTINCT FROM len(referenced_works)) AS bad"
    ).fetchone()[0]

    # column-set check against the expected schema
    expected_columns = columns
    missing_columns   = [c for c in expected_columns if c not in cols]
    redundant_columns = [c for c in cols if c not in expected_columns]

    res = ShardValidationResult(
        content_length,
        compact_content_length,
        record_count,
        compact_record_count,
        link_mismatch_count,
        (len(missing_columns) == 0 and len(redundant_columns) == 0),
        missing_columns,
        redundant_columns,
    )

    return res

def validate_compact_data(manifest_path, compact_path: Path):
    """Validate the extracted shards as outlined in data_pipeline.md

    compact_path should be the parent folder of all entity folders
    {compact_path}
        /works
        /authors
        /sources

    Main validation checks are:
    - Work ID uniqueness
    - referenced_works integrity (referenced_works id exist, no duplicate links. etc.)
    - Compute file size for each shard, and compute total file size

    This function produces two files:
    - {compact_path}/{entity}/integrity_report.txt: Validation report across all shards
    - {compact_path}/{entity}/compact_manifest.json: manifest.json mirror for compact shards.

    Args:
        manifest_path: Path to manifest.json for raw data.
        compact_path: Path to extracted parquet shards

    Returns:
        None
    """

    files = get_manifest_data(manifest_path)

    compact_shard_paths = [compact_path/file.key for file in files]

    total_raw_bytes = sum([file.content_length for file in files])

    con = duckdb.connect()

    # __ PASS 1: collect all ids (needed before any reference check) _____________
    con.execute("CREATE TABLE all_ids (id VARCHAR)")

    compact_manifest = {
        "format": "parquet",
        "entity": entity,
        "record_count": 0,
        "content_length": 0,
        "files": []
    }
    total_bytes = 0
    for path in compact_shard_paths:
        p = str(path)

        # Getting record count & size per shard
        record_count = con.execute(
            "SELECT count(*) FROM read_parquet(?)", [p]
        ).fetchone()[0]
        size = os.path.getsize(p)

        # Appending work_ids to all_ids
        con.execute("INSERT INTO all_ids SELECT id FROM read_parquet(?)", [p])

        compact_manifest["files"].append({
            "key": Path(p).relative_to(compact_path),
            "meta": {
                "content_length": size,
                "record_count": record_count
            }
        })
        compact_manifest["record_count"] += record_count
        compact_manifest["content_length"] += size 
        total_bytes += size

    con.execute("CREATE INDEX idx_all_ids ON all_ids(id)")

    shard_paths = [str(p) for p in compact_shard_paths]

    # __ CHECK 1: Work ID uniqueness — list of (id, occurrences) __________________
    duplicate_ids = con.sql("""
        SELECT id, count(*) AS n
        FROM all_ids
        GROUP BY id
        HAVING count(*) > 1
        ORDER BY n DESC
    """).fetchall()

    # __ CHECK 2a: dangling references — list of (work_id, referenced_work_id) ___
    dangling_refs = con.sql(f"""
        SELECT w.id AS work_id, w.ref AS referenced_work_id
        FROM (
            SELECT id, unnest(referenced_works) AS ref
            FROM read_parquet(?)
        ) w
        ANTI JOIN all_ids a ON a.id = w.ref
        ORDER BY work_id
    """, [shard_paths]).fetchall()

    # __ CHECK 2b: duplicate links within a record ________________________________
    # List of work ids that contain at least one repeated reference.
    dup_link_works = con.sql(f"""
        SELECT id AS work_id
        FROM read_parquet(?)
        WHERE len(referenced_works) <> len(list_distinct(referenced_works))
        ORDER BY work_id
    """, [shard_paths]).fetchall()

    # Detail: which reference was repeated, and how many times, per work.
    dup_link_detail = con.sql(f"""
        SELECT id AS work_id, ref AS referenced_work_id, count(*) AS occurrences
        FROM (
            SELECT id, unnest(referenced_works) AS ref
            FROM read_parquet(?)
        )
        GROUP BY id, ref
        HAVING count(*) > 1
        ORDER BY work_id, occurrences DESC
    """, [shard_paths]).fetchall()

    # __ REPORT _________________________________________________________________
    print(f"Shards:               {len(compact_shard_paths)}")
    print(f"Total raw size:       {total_raw_bytes:,} bytes ({total_raw_bytes/1e9:.2f} GB)")
    print(f"Total size:           {total_bytes:,} bytes ({total_bytes/1e9:.2f} GB)")
    print(f"Size reduction:       {float(total_bytes)/total_raw_bytes:.2f}% of raw size")
    print(f"Duplicate work ids:   {len(duplicate_ids)}")
    print(f"Dangling references:  {len(dangling_refs)}")
    print(f"Works w/ dup links:   {len(dup_link_works)}")

    with open(compact_path/entity/"integrity_report.txt", "w", encoding="utf-8") as f:
        f.write(f"Shards: {len(compact_shard_paths)}\n")
        f.write(f"Total raw size: {total_raw_bytes:,} bytes ({total_raw_bytes/1e9:.2f} GB)")
        f.write(f"Total size: {total_bytes:,} bytes ({total_bytes/1e9:.2f} GB)\n\n")
        f.write(f"Size reduction: {float(total_bytes)/total_raw_bytes:.2f}% of raw size")

        f.write(f"Duplicate work ids ({len(duplicate_ids)}):\n")
        for wid, n in duplicate_ids:
            f.write(f"  {wid}\t{n}\n")

        f.write(f"\nDangling references ({len(dangling_refs)}):\n")
        for work_id, ref_id in dangling_refs:
            f.write(f"  {work_id} -> {ref_id}\n")

        f.write(f"\nWorks with duplicate links ({len(dup_link_works)}):\n")
        for (wid,) in dup_link_works:
            f.write(f"  {wid}\n")

        f.write(f"\nDuplicate link detail ({len(dup_link_detail)}):\n")
        for work_id, ref_id, n in dup_link_detail:
            f.write(f"  {work_id} -> {ref_id} (x{n})\n")

    with open(compact_path/entity/"compact_manifest.json", "w") as f:
        json.dump(compact_manifest, f)


def delete_raw(shard_path: Path) -> None:
    """Delete raw data shard to preserve space

    Args:
        shard_path: Path to the shard to be deleted

    Returns:
        None

    """

    shard_path.unlink(missing_ok=True)