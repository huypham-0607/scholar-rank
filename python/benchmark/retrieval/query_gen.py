import csv
import random

import pandas as pd

from pathlib import Path
from scholar_rank import get_logger, load_config, load_benchmark_config, scholar_rank_cpp
from ms_marco_pipeline import build_posting, run_queries_perf_metrics

logger = get_logger(__name__)

QUERY_RESULT_FOLDER = "query_result"

SEED = 67
N_QUERIES = 2000
QUERY_LEN_RANGE = (5, 10)

def _percentile_slice(
    mapping_sorted: list[tuple[str, int]],
    fraction: float,
    from_end: bool = False,
) -> list[tuple[str, int]]:
    """Slice mapping_sorted (descending by df) into a df percentile band.

    from_end=False takes the highest-df fraction (the front of the list);
    from_end=True takes the lowest-df fraction (the back of the list).
    """
    n = len(mapping_sorted)
    k = int(n * fraction)
    if from_end:
        return mapping_sorted[n - k:]
    return mapping_sorted[:k]

def _write_query_set(
    term_pool: list[tuple[str, int]],
    out_path: Path,
    n_queries: int = N_QUERIES,
    seed: int = SEED,
):
    """Write n_queries deterministic queries sampled from term_pool to out_path.

    Each query draws a random length in QUERY_LEN_RANGE and that many
    distinct terms from term_pool (without replacement within a query,
    with replacement across queries).
    """
    rng = random.Random(seed)
    terms = [term for term, _ in term_pool]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        for qid in range(n_queries):
            query_len = rng.randint(*QUERY_LEN_RANGE)
            query_terms = rng.sample(terms, query_len)
            writer.writerow([qid, " ".join(query_terms)])

def generate_random_set(mapping_sorted: list[tuple[str, int]], out_dir: Path):
    """random_set.tsv: uniform random sampling across the entire vocabulary."""
    logger.info("Generating random_set...")
    _write_query_set(mapping_sorted, out_dir / "random_set.tsv")
    logger.info("Finished generating random_set.")

def generate_common_set(mapping_sorted: list[tuple[str, int]], out_dir: Path):
    """common_set.tsv: terms drawn from the top 1% highest-df band."""
    logger.info("Generating common_set...")
    pool = _percentile_slice(mapping_sorted, 0.01)
    _write_query_set(pool, out_dir / "common_set.tsv")
    logger.info("Finished generating common_set.")

def generate_very_common_set(mapping_sorted: list[tuple[str, int]], out_dir: Path):
    """very_common_set.tsv: terms drawn from the top 0.01% highest-df band."""
    logger.info("Generating very_common_set...")
    pool = _percentile_slice(mapping_sorted, 0.0001)
    _write_query_set(pool, out_dir / "very_common_set.tsv")
    logger.info("Finished generating very_common_set.")

def generate_rare_set(mapping_sorted: list[tuple[str, int]], out_dir: Path):
    """rare_set.tsv: terms drawn from the bottom 10% lowest-df band."""
    logger.info("Generating rare_set...")
    pool = _percentile_slice(mapping_sorted, 0.1, from_end=True)
    _write_query_set(pool, out_dir / "rare_set.tsv")
    logger.info("Finished generating rare_set.")

def generate_skewed_set(mapping_sorted: list[tuple[str, int]], out_dir: Path):
    """skewed_set.tsv: terms drawn from the union of the top 0.01% and bottom 0.01% df band."""
    logger.info("Generating skewed_set...")
    pool = (
        _percentile_slice(mapping_sorted, 0.0001)
        + _percentile_slice(mapping_sorted, 0.0001, from_end=True)
    )
    _write_query_set(pool, out_dir / "skewed_set.tsv")
    logger.info("Finished generating skewed_set.")

def main():
    """Produced a few query set for full-en based on term occurences

    Each query set will have 10000 queries, each with length (number of terms) of [5,10].
    
    Query sets will be saved in tsv. Format of all query sets are:
        qid query_string

    List of query sets
        - random_set.tsv: Uniformed random sampling.
        - common_set.tsv: Random terms with top 10% highest df.
        - very_common_set.tsv: Random terms with top 0.1% df.
        - rare_set.tsv: Random terms with bottom 10% lowest df.
        - skewed_set.tsv: Random terms with top 0.1% highest df or top 0.1% lowest df.
    """
    config = load_config()
    benchmark_config = load_benchmark_config()

    paths = config["data-path"]

    full_meta_path = Path(paths["posting-path"]) / "full_en" / config["posting"]["posting-folder"] / "metadata.bin"
    query_data_dir = Path(benchmark_config["paths"]["data-dir"]) / "full-en"
    query_data_dir.mkdir(parents=True, exist_ok=True)

    # flag = True
    # query_sets = ["random_set.tsv", "common_set.tsv", "very_common_set.tsv", "rare_set.tsv", "skewed_set.tsv"]
    # for query_set in query_sets:
    #     flag = flag and (query_data_dir / query_set).is_file()

    # if flag:
    #     logger.info(f"All {len(query_sets)} sets already present, skipping...")
    #     return

    mapping = scholar_rank_cpp.read_term_df_mapping(full_meta_path)
    mapping_sorted = sorted(mapping, key=lambda term_df: term_df[1], reverse=True)

    logger.info(f"Total no of terms: {len(mapping_sorted)}.")

    generate_random_set(mapping_sorted, query_data_dir)
    generate_common_set(mapping_sorted, query_data_dir)
    generate_very_common_set(mapping_sorted, query_data_dir)
    generate_rare_set(mapping_sorted, query_data_dir)
    generate_skewed_set(mapping_sorted, query_data_dir)

if __name__ == "__main__":
    main()

