import argparse
import resource

import pandas as pd
import ms_marco_pipeline
import full_en_bench_query

from pathlib import Path
from scholar_rank import get_logger, load_benchmark_config, scholar_rank_cpp

logger = get_logger(__name__)

QUERY_RESULT_FOLDER = "query_result"

def write_perf_parquet(
    run_name: str,
    query_latency: list[float],
    engine_latency: float,
    max_rss: int,
    out_path: Path,
):
    row = pd.DataFrame([{
        "run_name": run_name,
        "query_latency": query_latency,
        "engine_latency": engine_latency,
        "max_rss": max_rss,
    }])

    out_path.parent.mkdir(parents=True, exist_ok=True)

    if out_path.exists():
        existing = pd.read_parquet(out_path)
        combined = pd.concat([existing, row], ignore_index=True)
    else:
        combined = row

    combined.to_parquet(out_path)

def run_msmarco_single(filename: str) -> tuple:
    benchmark_config = load_benchmark_config()

    query_path = Path(benchmark_config["msmarco"]["data-dir"]) / filename

    logger.info(f"Started running MS MARCO queries from {query_path}.")

    ms_marco_pipeline.build_posting(is_forced=False)
    engine_latency, query_latency = ms_marco_pipeline.run_queries_perf_metrics(query_path)

    logger.info(f"Finished running MS MARCO queries from {query_path}.")

    # query_batch_benchmark returns datetime.timedelta - convert to plain milliseconds
    query_latency_ms = [td.total_seconds() * 1000 for td in query_latency]
    engine_latency_ms = engine_latency.total_seconds() * 1000

    # Assuming we run on Linux, max_rss is in kilobytes
    max_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss

    return query_latency_ms, engine_latency_ms, max_rss

def run_full_en_single(filename: str, k: int) -> tuple:
    benchmark_config = load_benchmark_config()

    query_path = Path(benchmark_config["paths"]["data-dir"]) / "full-en" / filename

    logger.info(f"Started running full-en queries from {query_path}, k = {k}.")

    engine_latency, query_latency = full_en_bench_query.run_queries_perf_metrics(query_path, k)

    logger.info(f"Finished running full-en queries from {query_path}, k = {k}.")

    # query_batch_benchmark returns datetime.timedelta - convert to plain milliseconds
    query_latency_ms = [td.total_seconds() * 1000 for td in query_latency]
    engine_latency_ms = engine_latency.total_seconds() * 1000

    # Assuming we run on Linux, max_rss is in kilobytes
    max_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss

    return query_latency_ms, engine_latency_ms, max_rss

def main():
    parser = argparse.ArgumentParser(
        description="Run one BMW performance benchmark and append its result to perf_raw.parquet. "
        "Invoke this once per (query file, k) combination (e.g. from a shell loop) rather than "
        "looping in-process, so each run gets its own OS process and therefore its own "
        "independent peak-RSS reading."
    )
    parser.add_argument(
        "type", type=int, choices=[0, 1],
        help="Corpus type: 0 = msmarco, 1 = full-en"
    )
    parser.add_argument(
        "filename", type=str,
        help="Query file name; the containing directory is resolved internally based on type"
    )
    parser.add_argument(
        "--k", type=int, default=None,
        help="Top-k depth to query at. Required when type=1 (full-en) - each k needs its own "
        "process invocation for RAM isolation. Ignored when type=0 (msmarco), which is fixed at k=1000."
    )
    args = parser.parse_args()

    if args.type == 1 and args.k is None:
        parser.error("--k is required when type=1 (full-en)")

    benchmark_config = load_benchmark_config()

    if args.type == 1:
        query_result_dir = Path(benchmark_config["paths"]["benchmark-dir"]) / "full-en" / QUERY_RESULT_FOLDER
        out_path = query_result_dir / "perf_raw.parquet"

        logger.info(f"Current run: k = {args.k}...")
        query_latency, engine_latency, max_rss = run_full_en_single(args.filename, args.k)
        logger.info(f"Writing full-en performance data to {out_path}, k = {args.k}.")
        write_perf_parquet(f"{args.filename}_{args.k}", query_latency, engine_latency, max_rss, out_path)

    else:
        query_latency, engine_latency, max_rss = run_msmarco_single(args.filename)

        query_result_dir = Path(benchmark_config["msmarco"]["posting-dir"]) / QUERY_RESULT_FOLDER
        out_path = query_result_dir / "perf_raw.parquet"

        logger.info(f"Writing MS MARCO performance data to {out_path}.")
        write_perf_parquet(args.filename, query_latency, engine_latency, max_rss, out_path)

if __name__ == "__main__":
    main()
