import io
import struct
import duckdb as db
import numpy as np
import pandas as pd
import tomllib
import csv

from pathlib import Path
from scholar_rank import get_logger, load_benchmark_config, Tokenizer, scholar_rank_cpp, PROJECT_ROOT
from ms_marco_pipeline import build_posting, run_queries

logger = get_logger(__name__)

TOKEN_STREAM_FOLDER = "token_stream"
POSTING_FOLDER = "posting"
PARTIAL_FOLDER = "posting/partial"
QUERY_RESULT_FOLDER = "query_result"

MEM_LIMIT = (1<<30)
K1 = 0.82
B = 0.68
BLOCK_SIZE = 128
SPLIT_SIZE = (1<<30)

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


def main():
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

if __name__ == "__main__":
    main()