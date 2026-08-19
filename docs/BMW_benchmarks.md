# BMW Benchmark Report
## Abstract

This document reports the benchmark results for an implementation of Block-Max WAND with BM25 scoring engine, which is used as a High-Recall retrieval engine for the Startorch project.

Benchmarks include:
- MRR@10 and Recall@1000 against Anserini BM25 implementation
- MS-MARCO retrieval latency
- OpenAlex retrieval latency.

Quick summary:
- MRR@10: 0.1926 (BMW), 0.1892 (Anserini)
- Recall@1000: 0.8778 (BMW), 0.8573 (Anserini)
- MS-MARCO retrieval latency: 
- OpenAlex retrieval latency:

## Introduction and Scope

BM25 has existed since the 90s, and is still one of the most popular lexical retrieval baseline for most retrieval tasks. For Startorch, BM25 is designed as a lightweight High Recall retrieval engine to 