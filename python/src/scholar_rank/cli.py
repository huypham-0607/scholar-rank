"""
    Supports 4 commands:
        - fetch_data
        - subset
        - build_posting (which also includes tokenizer)
        - query
    
"""

import argparse
import tomllib

from pathlib import Path
from scholar_rank.ingest.fetch_data import EntityIngestor
from scholar_rank.utils import PROJECT_ROOT, get_logger

logger = get_logger(__name__)
CONFIG_PATH = PROJECT_ROOT / "project-config.toml"

def load_config() -> dict:
    with open(CONFIG_PATH, "rb") as f:
        return tomllib.load(f)

def resolve_path(raw: str) -> Path:
    p = Path(raw)
    return p if p.is_absolute() else (PROJECT_ROOT / p).resolve()

def cmd_ingest(args: argparse.Namespace, config: dict) -> None:
    paths = config["data-path"]
    ingestor_cls = EntityIngestor.registry[args.entity]
    ingestor = ingestor_cls(
        upstream_prefix=Path(paths["upstream_path"]),
        raw_path=resolve_path(paths["tmp_data_path"]),
        compact_path=resolve_path(paths["data_path"]) / "full_corpus",
    )
    ingestor.orchestrate(forced_fetch=args.forced_fetch)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="scholar-rank")
    subparsers = parser.add_subparsers(dest="command", required = True)

    ingest_parser = subparsers.add_parser("ingest", help="Fetch + extract OpenAlex shards")
    ingest_parser.add_argument(
        "--entity",
        required=True,
        choices=sorted(EntityIngestor.registry.keys()),
        help="Which OpenAlex entity to ingest.",
    )
    ingest_parser.add_argument(
        "--forced-fetch",
        help="Force a refetch regardless if compact shards are present.",
        action="store_true"
    )
    ingest_parser.set_defaults(func=cmd_ingest)

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    config = load_config()
    args.func(args, config)


if __name__ == "__main__":
    main()