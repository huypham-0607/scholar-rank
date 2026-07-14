"""Fetching data from OpenAlex API, and saving it under data/...




"""

import boto3
import duckdb

from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT

logger = get_logger(__name__)

def get_manifest(upstream_path, dest_path: Path) -> None:
    """Getting manifest.json for an Entity from OpenAlex S3 storage.

        Args:
            upstream_path: pathlib.Path to manifest.json location in S3 bucket.
            dest_path: pathlib.Path to save location for manifest.json.

        Returns:
            None
    """

    s3 = boto3.client('s3', config=Config(signature_version=UNSIGNED))
    s3.download_file('openalex',upstream_path, dest_path)
    is_successful = Path(dest_path).is_file()
    if not is_successful:
        logger.warning(f"Failed to fetch manifest.json.")
    else:
        logger.info(f"Fetching manifest.json successfully.")

def list_local_and_remote_shards(manifest_path: Path, raw_data_path: Path)->(list,list):
    """Listing all shards present and not present in raw data path for an Entity.

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
        logger.warning(f"Unable to open manifest.json: {e.message}")
        return ([],[])
    
    remote_shard_list = []
    local_shard_list = []

    for obj in data["files"]:
        # Extracting only the directory of a shard
        url = Path(obj["url"][27:])
        if (not Path(PROJECT_ROOT/"data"/"openalex"/url).is_file()):
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
    s3.download_file('openalex',upstream_path, dest_path)
    is_successful = Path(dest_path).is_file()

    if not is_successful:
        logger.warning(f"Failed to fetch {upstream_path}.")
    else:
        logger.info(f"Fetching {upstream_path} successfully.")

def extract_compact(shard_path, out_path: Path):
    """Extract code metadata from each shard

    Details of metadata explained in data_reference.md

    Args:
        shard_path: Local shard path for extraction.
        out_path: Output file path after extracting metadata.

    Returns:
        None
    """

    columns = [
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

    rel = duckdb.read_parquet(shard_path)

    .select(*columns).show()

    

def validate():
    """
    """

def delete_raw(shard_path: Path) -> None:
    """
    """