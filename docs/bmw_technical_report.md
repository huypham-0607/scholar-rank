# Block-Max WAND BM25 IR Technical & Benchmark Report
## 1. Abstract

This document reports the technical designn & benchmark results for an implementation of Block-Max WAND with BM25 scoring engine, which is used as a High-Recall retrieval engine for the Startorch project.

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

BM25 has existed since the 90s, and is still one of the most popular lexical retrieval baseline for most retrieval tasks. For Startorch, BM25 is designed as a lightweight IR engine to quickly retrieve relevant documents, which will then be accurately re-ranked using other algorithms.

Originally, we wanted to use DuckDB built in BM25 in the FTS module. But since Startorch operates on hundreds of millions of documents, an exhaustive disjunctive top-k BM25 implementation would be too slow. Hence, we decided to implement our own Safe Block-Max WAND BM25 variant (Ding & Suel, 2011).

This report will answer four main questions:
1. How was the entire IR pipeline designed?
2. Does BM25 implementation matches a pre-existing reference?
2. Is rank-safety established by Block-Max WAND optimization?
3. How does Block-Max WAND perform at 300M OpenAlex document scale? How does it compare to exhaustive search?
4. How does Block-Max WAND latency respond to term document-frequency?

*PS. I think Block-Max WAND as an algorithm is already well-documented. This project adds nothing new to the existing knowledge base. It's best to treat this report as a technical report for a system engineering project.*

## 3. Retrieval Design & Implementation

This section will go over some implementation details of our BMW BM25.

### OpenAlex Corpus

Our main corpus for this project is OpenAlex Works entity, which is an open-source academic catalog, featuring hundreds over 510 million scholarly works. For this project, we will only be working with subset of works written in English (which is around 345 million works).

Data are fetched directly from OpenAlex S3 storage. Since we are working with 780 GB+ of compressed data, we've designed our pipeline as follows:

1. Download one chunk of raw data.
2. Strip away fields unrelevant for this project. See remaining fields at `data_reference.md`
3. Validate per-chunk integrity.
4. Delete the raw chunk, keep only the compact version.

This keeps disk usage managable throughout the process. Compact data is stored as [Parquet](https://parquet.apache.org/), and has cummulative size of 103GB compressed. Source code for fetching process can be found at `ingest/fetch_data.py`

### Tokenization

For the scope of this project, **we will not be indexing full documents**. There are a few reasons for this decision, though here are the main ones

- Space constraint on device (1 TB of storage).
- A substantial percentage of documents are not open-access.
- Complexity of processing and handling documents, as well as crawling process if needed, is unfeasible workload for a solo project, and is not the core objective of Startorch.

Alternatively, we will construct our document by concatenating elements across 6 fields

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `title`                   | `VARCHAR`     | Title of given work |
| `topics`                  | `VARCHAR`     | OpenAlex assigned topics of given work |
| `subfields`               | `VARCHAR`     | OpenAlex assigned subfields of given work |
| `fields`                  | `VARCHAR`     | OpenAlex assigned fields of given work |
| `domains`                 | `VARCHAR`     | OpenAlex assigned domains of given work |
| `keywords`                | `VARCHAR`     | OpenAlex assigned keywords of given work |
 
Readers can reference OpenAlex documentation on how they determine topics and keywords.

Currently, these fields are treated as one flat bag of word model (no field weighting yet). This is worth looking into in retrospect as certain fields (eg. `title`) has higher indicative value than a field like `domain` (Though this is partly remedied by BM25 term saturation).

An issue worth flagging, OpenAlex treat topics as a hierarchy of `domains` &rarr;`fields` &rarr; `subfields` &rarr; `topics`. A consolidated research paper might have list of `topics` that share common ancestors at higher level in the hierarchy. This has the potential to skew both BM25 accuracy and BMW pruning effectiveness for common terms.

Tokenization step will be implemented in Python (`tokenizer/tokenizer.py`). Results will be serialized and saved as binary shards `token_*.bin`

We will consider each single word as a token. Hyphen connected words are collapsed an considered a single word. For instance "We value your well-being" will be considered 5 tokens: "We", "value", "your", "well", "being". For the scope of this project, machine learning based tokenization methods are not considered.

Normalization rules are as follows:
- All accents are stripped.
- All tokens will be decapitalized.
- All non-alphabetical characters are stripped (This is of minimal impact for our dataset, since our fields are generally low-nuance).
- Stopwords are stripped from the concatenated document text *before* tokenization (a fixed English stopword list, matched with regex word boundaries, replaced globally — not just the first occurrence).

For stemmer, we use DuckDB FTS's built-in `stem(token, 'english')` function directly in SQL, which  is Snowball/Porter2 under the hood.

### Inverted index list (Posting list)

During the tokenization process, Python will also produce another artifact - a remapping of OpenAlex Works id (saved as `doc_id_lookup.bin`). Raw OpenAlex IDs are sparse and exceed `int32`, so documents are squished into `[0,N)` space. There are two main benefits to this.

- Reduced disk usage for doc_id storage from 8 bytes to 4 bytes.
- Any doc_id mapping downstream can be referenced with flat array instead of a hashmap.

Everything downstream from now on will be written in C++.

**Build pipeline, each with their own file** - (`cpp/src/retrieval/`):

| Stage | File | Output |
|---|---|---|
| SPIMI partial-block construction | `construct_inverted_blocks.cpp` | `block_*.bin`, one set per memory-limited chunk |
| Document length table | `construct_doc_len_list.cpp` | `doc_len_list.bin`, dense array by doc_id |
| K-way merge + BMW block-metadata | `merge_inverted_blocks.cpp` | `posting_*.bin` + one consolidated `block_meta.bin` |

Shared pieces both later stages depend on:
- `posting_list.cpp` (`PostingItem`/`PostingList`)
- `bm25.cpp` (`calc_BM25`/`bm25_saturation`)
- `token_stream.cpp` (`read_token`, the tokenizer wire-format reader)
- `vbe.cpp` (Variable byte encoding for doc_id, expensive saving as `unsigned long long`).

This is a classic SPIMI out of core indexing setup. Token are read and accummulate in memory, and flushes when memory usage is reaching certain threshold. This results in several "blocks" of posting lists. These blocks are then merged by maintaining a read pointer for each block, relying on lexicographically sorted term invariant to produce complete posting lists in one pass.

`merge_inverted_blocks.cpp` also produces `block_meta.bin`, which saves block level information (start, end position in file, block-level term contribution) necessary for BMW pruning.

An additional document length table is computed to facilitate exact BM25 computation for each document. Technically, we can store exact BM25 score directly in posting list. But we decided to store term frequency instead to avoid recomputation if design changes.

### Query Engine

This is where we get substaintial performance gain on exhaustive top-k BM25. The key idea for any WAND-based methods is to skip ahead any documents whose score will never exceed a certain threshold, estimated by their term maximum contribution across all documents.

Block-Max WAND improved upon this by chunking posting lists into blocks (usually of size 64 or 128), and using max term contribution across documents inside that block, enforcing an even stricter estimation.

Readers can dive read more about Block-Max WAND in the original research paper.

The query engine is implemeted in `src/retrieval/query_engine.cpp`. Here is quick implementation rundown.

Each search term gets its own cursor (`PostingPointer`) into that term's posting
list. This is implemented by traversing a memory mapped file (for efficiency with non-sequential reads). On every round, the engine sorts the cursors by the document ID, calculating pivot (position where running global WAND score exceeded current top-k threshold). Two optimizations compared to exhaustive search:

- **Term-level optimization**: If an best possible BM25 score of every document term cannot make it to top-k, skip all documents from beginning of cursor list to the pivot.
- **Block-level shortcut**: If block level BM25 score does not exceed top-k threshold, skip the whole block without decoding a single posting inside it.

Only documents surviving both pruning optimizations ever got exact BM25 score computed.

All block level operations are done **in-memory**, and the algorithm is designed to minimize the number of document level access as much as possible, as long as pruning as many uncompetitive documents as possible. This keeps query latency low.


## 4. Experimental setup

### Environment setup

| Item | Description |
|---|---|
| Processor | Intel Core Ultra 7 Processor 255HX  |
| Document length table | `construct_doc_len_list.cpp` |
| RAM | Samsung 16x2 GB DDR5 5600 MT/s |
| Storage | Western Digital SN7100 1TB NVMe SSD Gen4 PCIe |
| Operating System | Arch Linux x86_64 |
| Kernel version | Linux 7.1.8 |
| Compiler | g++ (GCC) 16.2.1 |
| Language standards | `-std=c++20` |
| Optimization flags | None |

### OpenAlex query sets

For OpenAlex corpus, query sets are synthetically generated by randomly sampling terms from present in posting lists. Each query set also has additional generation requirements to test extreme/adversarial cases.

| Queryset | No of queries | Query length | Sampling method |
|---|---|---|---|
| `random_set.tsv` | 2000 | [5,10] | Uniformed random sampling. |
| `common_set.tsv` | 2000 | [5,10] | Random terms with top 1% highest doc frequency. |
| `very_common_set.tsv` | 2000 | [5,10] | Random terms with top 0.01% doc frequency. |
| `rare_set.tsv` | 2000 | [5,10] | Random terms with bottom 10% lowest df. |
| `skewed_set.tsv` | 2000 | [5,10] |Random terms with top 0.01% highest df or top 0.01%  |
| `short_random_set.tsv` | 2000 | [3,4] | Uniformed random sampling. |
| `short_common_set.tsv` | 2000 | [3,4] | Random terms with top 1% highest doc frequency. |
| `short_very_common_set.tsv` | 2000 | [3,4] | Random terms with top 0.01% doc frequency. |
| `short_rare_set.tsv` | 2000 | [3,4] | Random terms with bottom 10% lowest df. |
| `short_skewed_set.tsv` | 2000 | [3,4] |Random terms with top 0.01% highest df or top 0.01%  |

### MS MARCO Passage Full Ranking query set

MS MARCO Passage Ranking query set is useful for comparing Startorch BM25 retrieval quality relative to other benchmarked BM25 implementations. For this report, we will be comparing with Anserini BM25 based on two metrics: **MRR@10** and **Recall@1000**. Engine for computing MRR@10 and Recall@1000 are Microsoft's `ms_marco_eval.py` & NIST's `trec_eval` respectively.

| Queryset | No of queries | Description |
|---|---|---|
| `queries.dev.small.tsv`| 6980 | Uniformed random sampling. |

## 5. Rank-safety & effectiveness

### 5.1) Rank Safety

| name                         |    n |   exact_matches |   sum_missing |   sum_extra |
|:-----------------------------|-----:|----------------:|--------------:|------------:|
| random_set.tsv_1000_2000     | 2000 |            2000 |             0 |           0 |
| common_set.tsv_1000_2000     | 2000 |            1999 |             3 |           3 |
| very_common_set.tsv_1000_100 |  100 |              70 |             0 |           0 |
| skewed_set.tsv_1000_100      |  100 |              93 |             1 |           1 |
| rare_set.tsv_1000_2000       | 2000 |            2000 |             0 |           0 |

### 5.2) Retrieval effectiveness

| MS MARCO Metrics       | Startorch BMW | Anserini BM25      |
| ---------------------- | ------------- | ------------------ |
| `MRR@10`               | 0.1926        | 0.1892             |
| `Recall@1000`          | 0.8778        | 0.8573             |

## 6. Efficiency: MS MARCO

| name                  |    n |   max_rss |   mean |    p50 |    p95 |    p99 |     max |   min |
|:----------------------|-----:|----------:|-------:|-------:|-------:|-------:|--------:|------:|
| queries.dev.small.tsv | 6980 |   1777.94 |  16.02 | 10.645 | 47.201 | 85.465 | 388.862 | 0.009 |

## 7. Efficiency: OpenAlex

| name                                |    n |   max_rss |     mean |      p50 |       p95 |       p99 |       max |     min |
|:------------------------------------|-----:|----------:|---------:|---------:|----------:|----------:|----------:|--------:|
| random_set.tsv_10                   | 2000 |   11402.7 |    1.983 |    1.413 |     6.513 |    11.489 |    35.692 |   0.005 |
| random_set.tsv_1000                 | 2000 |   11399.9 |    0.597 |    0.041 |     1.873 |     8.051 |    72.145 |   0.012 |
| common_set.tsv_10                   | 2000 |   12243.1 |    5.15  |    2.756 |    17.801 |    54.659 |   130.198 |   0.135 |
| common_set.tsv_1000                 | 2000 |   12636   |   11.29  |    5.644 |    41.187 |    80.076 |   277.762 |   0.349 |
| very_common_set.tsv_10              | 2000 |   19754   | 1013.88  |  878.842 |  2141.28  |  3050.8   |  5462.67  | 119.053 |
| very_common_set.tsv_1000            | 2000 |   21658.4 | 1291.94  | 1130.97  |  2743.65  |  3988.49  |  7267.84  | 143.525 |
| rare_set.tsv_10                     | 2000 |   11399.4 |    1.238 |    0.921 |     4.303 |     6.578 |    14.78  |   0.005 |
| rare_set.tsv_1000                   | 2000 |   11401.2 |    0.133 |    0.026 |     0.051 |     1.791 |    63.05  |   0.012 |
| skewed_set.tsv_10                   | 2000 |   19489.1 |  333.689 |  258.509 |   883.225 |  1485.74  |  2774.37  |   0.008 |
| skewed_set.tsv_1000                 | 2000 |   22638.3 |  504.893 |  386.489 |  1343.55  |  2083.46  |  4020.1   |   0.02  |
| random_set.tsv_10_exhaustive        | 2000 |   11400.2 |    3.098 |    0.076 |    12.524 |    36.171 |   291.406 |   0.004 |
| random_set.tsv_1000_exhaustive      | 2000 |   11403.1 |    0.868 |    0.036 |     2.099 |     5.427 |   291.184 |   0.013 |
| common_set.tsv_10_exhaustive        | 2000 |   13076.2 |   48.393 |    5.055 |   160.609 |   827.891 |  5910.74  |   0.273 |
| common_set.tsv_1000_exhaustive      | 2000 |   13240.5 |   45.98  |    4.447 |   148.544 |   731.608 |  5742.53  |   0.324 |
| very_common_set.tsv_10_exhaustive   |  100 |   18289.5 | 3431.02  | 2102.21  | 11324.4   | 15336.2   | 27140.9   | 511.406 |
| very_common_set.tsv_1000_exhaustive |  100 |   17911.7 | 3341.37  | 2065.07  | 10750.5   | 15142.6   | 25709.5   | 532.604 |
| rare_set.tsv_10_exhaustive          | 2000 |   11400   |    1.567 |    0.756 |     5.49  |     9.917 |   198.909 |   0.004 |
| rare_set.tsv_1000_exhaustive        | 2000 |   11398   |    0.319 |    0.025 |     1.601 |     2.196 |   195.709 |   0.012 |
| skewed_set.tsv_10_exhaustive        |  100 |   16024.9 | 1842.09  |  844.847 |  6834.97  |  8751.83  | 28035     |  61.22  |
| skewed_set.tsv_1000_exhaustive      |  100 |   16207.5 | 1943.1   |  882.63  |  7223.48  |  9224.74  | 28553.6   |  70.186 |
| short_random_set.tsv_10             | 2000 |   11404.4 |    0.493 |    0.015 |     2.027 |     3.569 |     8.505 |   0.002 |
| short_common_set.tsv_10             | 2000 |   11400.3 |    2.021 |    0.736 |     5.251 |    20.658 |   259.693 |   0.072 |
| short_very_common_set.tsv_10        | 2000 |   19223.6 |  218.796 |  185.421 |   444.786 |   707.867 |  3780.53  |   2.469 |
| short_rare_set.tsv_10               | 2000 |   11400.2 |    0.877 |    0.017 |     3.149 |     4.82  |    11.149 |   0.002 |
| short_skewed_set.tsv_10             | 2000 |   17547.1 |   89.069 |   50.138 |   302.425 |   534.029 |  4531.3   |   0.003 |

## 8. Analysis

## 9. Threats to validity

## 10. Conclusions and future work

## References

- **WAND (Weak AND)** — Broder, Carmel, Herscovici, Soffer, Zien, [*"Efficient Query Evaluation using a
  Two-Level Retrieval Process"*](https://www.researchgate.net/publication/221613425_Efficient_query_evaluation_using_a_two-level_retrieval_process)
  (CIKM 2003).

- **Block-Max WAND (BMW)** — Ding & Suel, [*"Faster Top-k Document Retrieval Using Block-Max Indexes"*](https://research.engineering.nyu.edu/~suel/papers/bmw.pdf)
  (SIGIR 2011). 