# BMW Benchmark Report
## 1. Abstract

This document reports the benchmark results for an implementation of Block-Max WAND with BM25 scoring engine, which is used as a High-Recall retrieval engine for the Startorch project.

Benchmarks include:
- MRR@10 and Recall@1000 against Anserini BM25 implementation
- MS-MARCO retrieval latency
- OpenAlex retrieval latency.

### Quick summary

#### MS MARCO Retrieval Quality

Compared Startorch BMW MRR@10 and Recall@1000 performance with MS MARCO Passage Dataset against Anserini BM25. Expect around similar performance.

| MS MARCO Metrics       | Startorch BMW | Anserini BM25      |
| ---------------------- | ------------- | ------------------ |
| `MRR@10`               | 0.1926        | 0.1892             |
| `Recall@1000`          | 0.8778        | 0.8573             |

#### MS MARCO Retrieval Latency

| Queryset               | n       | Mean (ms) | p50 (ms) | p95 (ms) | p99 (ms) |
| ---------------------- | ------- | --------- | -------- | -------- | -------- |
| `queries.dev.small.tsv`| 6980    | 16.020    | 10.645   | 47.201   | 85.465   |

#### OpenAlex Retrieval Latency

These are all synthetically generated query sets mimicking realistic load in production. Each query set consists of 2000 queries with 3-4 distinct terms registered in posting list. They are classified into 5 types:
- Random set: randomly sampled terms.
- Common set: **top 1%** highest document frequency.
- Very common: **top 0.01%** highest document frequency.
- Rare set: **bottom 10%** highest document frequency.
- Skewed set: **top & bottom 0.01%** highest document frequency.

| name                                |    n |   max_rss |     mean |      p50 |       p95 |       p99 |       max |     min |
|:------------------------------------|-----:|----------:|---------:|---------:|----------:|----------:|----------:|--------:|
| `short_random_set.tsv_10`             | 2000 |   11404.4 |    0.493 |    0.015 |     2.027 |     3.569 |     8.505 |   0.002 |
| `short_common_set.tsv_10`             | 2000 |   11400.3 |    2.021 |    0.736 |     5.251 |    20.658 |   259.693 |   0.072 |
| `short_very_common_set.tsv_10`        | 2000 |   19223.6 |  218.796 |  185.421 |   444.786 |   707.867 |  3780.53  |   2.469 |
| `short_rare_set.tsv_10`               | 2000 |   11400.2 |    0.877 |    0.017 |     3.149 |     4.82  |    11.149 |   0.002 |
| `short_skewed_set.tsv_10`             | 2000 |   17547.1 |   89.069 |   50.138 |   302.425 |   534.029 |  4531.3   |   0.003 |


## 2. Introduction and scope

BM25 has existed since the 90s, and is still one of the most popular lexical retrieval baseline for most retrieval tasks. For Startorch, BM25 is designed as a lightweight engine to quickly retrieve relevant documents, which will then be accurately re-ranked using other algorithms.

Since Startorch operates on hundreds of millions of documents, an exhaustive disjunctive top-k BM25 implementation would be too slow. Hence, we decided to implement our own Safe Block-Max WAND BM25 variant (Ding & Suel, 2011).

This report will answer four main questions:
1. Does BM25 implementation matches a pre-existing reference?
2. Is rank-safety established by Block-Max WAND optimization?
3. How does Block-Max WAND perform at 300M OpenAlex document scale? How does it compare to exhaustive search?
4. How does Block-Max WAND latency respond to term document-frequency?

*PS. I think Block-Max WAND as an algorithm is already well-documented. This project adds nothing new to the existing knowledge base. It's best to treat this report as a technical report for a system engineering project.*

## 3. BMW Implementation

## 4. Experimental setup

## 5. Correctness and effectiveness (MS MARCO)

## 6. Efficiency: MS MARCO

## 7. Efficiency: OpenAlex

## 8. Analysis

## 9. Threats to validity

## 10. Conclusions and future work

## References