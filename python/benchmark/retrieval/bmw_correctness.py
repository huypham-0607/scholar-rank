import argparse
import csv

import pandas as pd
import full_en_bench_query

from pathlib import Path
from scholar_rank import get_logger, load_benchmark_config
from ms_marco_pipeline import build_posting, run_queries

logger = get_logger(__name__)

QUERY_RESULT_FOLDER = "query_result"

def write_ms(qid_list: list, results: list[list[tuple[float,int]]], out_path: Path):
    buffer = []
    for qid, ranked in zip(qid_list, results):
        for rank, (score, pid) in enumerate(ranked, start=1):
            buffer.append([qid, pid, rank])

    with open(out_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerows(buffer)

def write_trec(qid_list: list, results: list[list[tuple[float,int]]], out_path: Path):
    buffer = []
    for qid, ranked in zip(qid_list, results):
        for rank, (score, pid) in enumerate(ranked, start=1):
            buffer.append([qid, "Q0", pid, rank, score, "BMW"])

    with open(out_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerows(buffer)

def write_correctness_parquet(
    run_name: str,
    exact_matches: int,
    recall_at_1000: int,
    out_path: Path,
):
    row = pd.DataFrame([{
        "run_name": run_name,
        "exact_matches": exact_matches,
        "recall_at_1000": recall_at_1000,
    }])

    out_path.parent.mkdir(parents=True, exist_ok=True)

    if out_path.exists():
        existing = pd.read_parquet(out_path)
        combined = pd.concat([existing, row], ignore_index=True)
    else:
        combined = row

    combined.to_parquet(out_path)

def ms_marco_benchmark():
    benchmark_config = load_benchmark_config()

    query_path = Path(benchmark_config["msmarco"]["data-dir"]) / "queries.dev.small.tsv"

    build_posting(is_forced=False)
    pid_list, results = run_queries(query_path)

    query_result_dir = Path(benchmark_config["msmarco"]["posting-dir"]) / QUERY_RESULT_FOLDER
    ms_path = query_result_dir / "ms_mrr10.tsv"
    trec_path = query_result_dir / "trec_recall1000.trec"

    ms_path.parent.mkdir(parents=True, exist_ok=True)
    trec_path.parent.mkdir(parents=True, exist_ok=True)

    write_ms(pid_list, results, ms_path)
    write_trec(pid_list, results, trec_path)

def compare_bmw_exhaustive(queryset: str, k: int, x: int) -> tuple:
    """Compare the first x queries of queryset between the pruned BMW engine
    and the unpruned exhaustive engine, both run at the same k.

    exact_matches: no. of the x queries where BMW's full ranked result list
    (score and doc_id at every position) is identical to exhaustive's.
    recall_at_1000: total no. of documents present in both BMW's and
    exhaustive's result sets, summed across the x queries (exhaustive is
    ground truth here, so this is how many of its documents BMW also found).
    """
    benchmark_config = load_benchmark_config()
    query_path = Path(benchmark_config["paths"]["data-dir"]) / "full-en" / queryset

    logger.info(f"Running BMW engine on first {x} queries of {queryset}, k = {k}...")
    bmw_results, _, _ = full_en_bench_query.run_queries_perf_metrics(query_path, k, cap=x)
    logger.info(f"Running exhaustive engine on first {x} queries of {queryset}, k = {k}...")
    exhaustive_results, _, _ = full_en_bench_query.run_queries_exhaustive_perf_metrics(query_path, k, cap=x)

    exact_matches = 0
    recall_at_1000 = 0
    for bmw_res, exhaustive_res in zip(bmw_results, exhaustive_results):
        if bmw_res == exhaustive_res:
            exact_matches += 1

        bmw_docs = {doc_id for _, doc_id in bmw_res}
        exhaustive_docs = {doc_id for _, doc_id in exhaustive_res}
        recall_at_1000 += len(bmw_docs & exhaustive_docs)

    return exact_matches, recall_at_1000

def main():
    parser = argparse.ArgumentParser(
        description="Run either the MS MARCO correctness benchmark or the full-en BMW-vs-exhaustive check."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("ms-marco", help="Run BMW against MS MARCO dev-small and write ms_mrr10.tsv/trec_recall1000.trec.")

    full_en_parser = subparsers.add_parser(
        "full-en-check",
        help="Compare BMW vs exhaustive results on a full-en query set and append to correctness_raw.parquet."
    )
    full_en_parser.add_argument("queryset", type=str, help="Query set file name (e.g. random_set.tsv)")
    full_en_parser.add_argument("--k", type=int, required=True, help="Top-k depth to compare at")
    full_en_parser.add_argument("--x", type=int, required=True, help="Compare the first x queries of the query set")

    args = parser.parse_args()

    if args.command == "ms-marco":
        ms_marco_benchmark()
    else:
        benchmark_config = load_benchmark_config()

        logger.info(f"Comparing BMW vs exhaustive on {args.queryset}, k = {args.k}, first {args.x} queries...")
        exact_matches, recall_at_1000 = compare_bmw_exhaustive(args.queryset, args.k, args.x)

        query_result_dir = Path(benchmark_config["paths"]["benchmark-dir"]) / "full-en" / QUERY_RESULT_FOLDER
        out_path = query_result_dir / "correctness_raw.parquet"

        logger.info(f"Writing correctness data to {out_path}.")
        write_correctness_parquet(
            f"{args.queryset}_{args.k}_{args.x}", exact_matches, recall_at_1000, out_path
        )

if __name__ == "__main__":
    main()
