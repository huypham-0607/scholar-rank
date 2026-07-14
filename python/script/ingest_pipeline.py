"""
"""

from scholar_rank.ingest.fetch_data import get_manifest, list_local_and_remote_shards, fetch_shard, extract_compact
from scholar_rank.utils import PROJECT_ROOT

# Fetching work entities from OpenAlex S3 storage
UPSTREAM_MANIFEST_PATH: Path("data/parquet/works/manifest.json")
RAW_DATA_PATH: Path = PROJECT_ROOT/"data"/"openalex"
PROCESSED_DATA_PATH: Path = PROJECT_ROOT/"data"/"processed"

# manifest_path = RAW_DATA_PATH/"openalex"/"works"/"manifest.json"

# get_manifest(UPSTREAM_MANIFEST_PATH, manifest_path)

# local_shard_list, remote_shard_list = list_local_and_remote_shard(manifest_path, RAW_DATA_PATH)

extract_compact("/home/halzyon/Data/coding_work/projects/scholar-rank/data/openalex/works/updated_date=2016-06-24/part_0000.parquet",RAW_DATA_PATH)