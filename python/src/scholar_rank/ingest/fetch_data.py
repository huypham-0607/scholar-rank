"""Fetching data from OpenAlex API, and saving it under data/...




"""

import boto3
import 

from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT

logger = get_logger(__name__)

UPSTREAM_MANIFEST_PATH: Path("data/parquet/works/manifest.json")

RAW_DATA_PATH: Path = get_project_root()/"data"/"openalex"
PROCESSED_DATA_PATH: Path = get_project_root()/"data"/"processed"

def get_manifest(upstream_path: Path, dest_path: Path) -> None:
    """Getting manifest.json for an Entity from OpenAlex S3 storage.

        Args:
            upstream_path: pathlib.Path to manifest.json location in S3 bucket.
            dest_path: pathlib.Path to save location for manifest.json.

        Returns:
            None
    """

    s3 = boto3.client('s3')
    s3.download_file('openalex',upstream_path, dest_path)
    is_successful = Path(dest_path).is_file()
    if not is_successful:
        logger.warning(f"Failed to fetch manifest.json.")
    else:
        logger.info(f"Fetching manifest.json successfully.")

def list_remote_shards(manifest_path: Path, raw_data_path: Path)->list:
    """Listing all shards not present in raw data path for an Entity.

        Args:
            manifest_path: pathlib.Path to manifest.json locally
            raw_data_path: pathlib.Path to raw data directory corresponding to manifest.json

        Returns:
            List of Paths of shards not on local storage, or empty list if exception occured

    """

    data = None

    try:
        with open(manifest_path,'r') as f:
            data = json.load(f)
    except Exception as e:
        logger.warning(f"Unable to open manifest.json: {e.message}")
        return []
    
    remote_shard_list = []

    for obj in data["files"]:
        # Extracting only the directory of a shard
        url = Path(obj["url"][27:])
        if (not Path(PROJECT_ROOT/"data"/"openalex"/url).is_file()):
            remote_shard_list.append(url)
        
    return remote_shard_list


def fetch_shard(shard_path, dest: Path) -> None:
    """
    """

def extract_compact(shard_path, out_path: Path):
    """

    """

def validate():
    """
    """

def delete_raw(shard_path: Path) -> None:
    """
    """