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
from scholar_rank.works_subset.works_subset import WorksSubsetter
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
        upstream_prefix=Path(paths["upstream-path"]),
        raw_path=resolve_path(paths["tmp-data-path"]),
        compact_path=resolve_path(paths["data-path"]) / paths["full-corpus-folder"],
    )
    ingestor.orchestrate(forced_fetch=args.forced_fetch)

def cmd_gen_works_subset(args: argparse.Namespace, config: dict) -> None:
    paths = config["data-path"]
    full_corpus_path = resolve_path(paths["data-path"]) / paths["full-corpus-folder"]
    subset_path = resolve_path(paths["data-path"]) / paths["works-subset-folder"] / args.profile.replace("-", "_")
    condition = config["works-subset"]["subset-profiles"][args.profile]
    subsetter = WorksSubsetter(full_corpus_path, subset_path, condition)
    subsetter.subset_database()
    subsetter.validate_database()

def build_parser(config: dict) -> argparse.ArgumentParser:
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

    gen_works_subset_parser = subparsers.add_parser("gen-works-subset", help="Extract works subset from full corpus.")
    gen_works_subset_parser.add_argument(
        "--profile",
        required=True,
        choices=sorted(config["works-subset"]["subset-profiles"]),
        help="Subset profile to use (see project-config.toml for filter conditions)"
    )
    gen_works_subset_parser.set_defaults(func=cmd_gen_works_subset)

    return parser


def main():
    config = load_config()
    parser = build_parser(config)
    args = parser.parse_args()
    args.func(args, config)


if __name__ == "__main__":
    main()