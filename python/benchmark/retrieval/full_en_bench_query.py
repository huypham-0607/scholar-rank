import io
import struct
import duckdb as db
import tomllib
import csv

from pathlib import Path
from scholar_rank import get_logger, load_benchmark_config, load_config, Tokenizer, scholar_rank_cpp, PROJECT_ROOT

logger = get_logger(__name__)

TOKEN_STREAM_FOLDER = "token_stream"
POSTING_FOLDER = "posting"
PARTIAL_FOLDER = "posting/partial"
QUERY_RESULT_FOLDER = "query_result"


def run_queries_perf_metrics(query_path: Path, k: int) -> tuple:
    benchmark_config = load_benchmark_config()
    config = load_config()

    posting_dir = Path(config["data-path"]["posting-path"]) / "full_en" / POSTING_FOLDER
    meta_path = posting_dir / scholar_rank_cpp.file_names.METADATA_BIN

    logger.info(f"Reading queries from {query_path}...")

    with open(query_path, mode='r', encoding='utf-8') as file:
        queries = list(csv.reader(file, delimiter='\t'))

    logger.info(f"Finished reading queries from {query_path}...")

    tokenizer = Tokenizer()

    logger.info(f"Tokenizing queries...")
    query_list = list(tokenizer.tokenize_query(query) for _, query in queries)
    logger.info(f"Finished tokenizing queries.")

    k_list = list(k for i in range (len(queries)))

    logger.info(f"Passing queries to BMW engine. Running engine...")
    _, (engine_latency, query_latency) = scholar_rank_cpp.query_batch_benchmark(meta_path, query_list, k_list)
    logger.info(f"Engine finished running, returning benchmarked results.")

    return engine_latency, query_latency