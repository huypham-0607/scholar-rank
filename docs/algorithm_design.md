# Algorithm Design for Scholar-Rank

<!-- Funny how this became a text-retrieval project from an authority ranking project -->

## 1. Abstract

Key principals for a Search Engine
- Relevance - Search result must be highly related to the given query. Achievable through keyword matching/semantic relevance.
- Authority - Search result must be highly credible/high quality sources. Achievable through various graph-based ranking algorithm (PR, HITS, SALSA).

## 2) Preliminary Design

A document score relative to a given query is a combination of multiple (normalized) scoring factors.

### 2.1) Base scoring formmulation

$S(d,q) = R(d,q)(w_{R} + w_{GA}GA(d)) + w_{LA}LA(d,q)$

- $d$ - Document being assessed/scored
- $q$ - User query
- $w_{x}$ - Specified weight for criteria $x$

Criteriation terms will be scaled and normalized accordingly.

### 2.2) Relevance (R(d,q))

Compute relevance of a specific document relative to given query.

<!--
    Resolution before stage 2:
    - Explicitly mention how these 4 candidates combine for final criteria score.
-->

Potential candidates to quantify relevance:
- BM25 score (Title, topics & keywords - Abstract with low weight)
- embedding similarity/semantic relevance
- Exact matches (Would weight higher for Title/keywords than abstract)
- Query-term coverage percentage

About abstract:
- Due to the very high percentages of NULL abstracts, abstract would only contributes a small portion to total weight for applicable metrics.
- Documents with NULL abstract would have their score normalized to compensate for missing abstract weight. (This, of course, must be further experiment and research depending on nature of specified metrics).

### 2.3) Local authority (LA(d,q))

Compute authority for a query-subsetted document graph.

Potential candidates to quantify local authority.
- Approximate top-k PPR / local push using seeds from High-recall candidate retrieval 
- Per-topic PPR score + topic semantic relevance to query. 
- Potentially some other metrics related to candidates after HRCR (?)

### 2.4) Global authority (GA(d))

Compute authority for a the global subgraph.

Obviously, Global PageRank will be the main engine behind this metric.

Other potential candidates
- HITS/SALSA (limited research currently)

Notes:
- As per the formula referenced in section 2.1, $GA(d)$ is scaled with $R(d,q)$. This is to ensure that generally credible but unrelated papers will not flood top results.
- Due to the near-DAG feature of citation graphs, we need a way to limit the effect of paper age for our PageRank. We can read [this paper about CiteRank](https://arxiv.org/abs/physics/0612122) for information and potential fix.

### 2.5) Additional/Experimental criterias (subject to change)
- F(d,q): Freshness score for a document/query pair, Higher score for more recent paper, and higher/lower score if query explicitly mention a timestamp.
- Q(d): Intrinsic Quality for a research paper. Hard to quantify this.

## 3. Pipeline

### Stage 1: High-recall candidate retrieval

Retrieve 500-2000 potential candidates using
- Lexical retrieval
    - BM25 over title/topic/keywords
    - Title/topic/keyword matches > Abstract matches
    - Compute exact entity matches
- Semantic retrieval
    - Embed query and document metadata
    - Retrieve based on similarity (Further research needed)
- Near neighbor extension (?)
    - Also retrieve close neighbor of potential candidates

Scoring system - Subject to more research

### Stage 2: Rerank subsetted candidates

Uses scoring system mentioned in section 2.

## 4. Validation

### Building dataset

Usually for text retrieval, we validate results using a set of predetermined queries + expected document chunk (ie. Golden dataset)

Two potential sources:
- Manual dataset: Handpicked query/result pairs, precompiled data, etc.
- Synthetic generation: Leverage multiple LLM models to generate query/result pairs & cross-validate returned dataset (or potentially treating them as "votes").

### Quantify accuracy

Generally, a good score can quantify these attributes:
- No of matches in actual document chunk and expected document chunk.
- Relevance of top result in actual document chunk.
- How far down are top results from expected document chunk in actual document chunk.

Potential metrics:

- Recall@K
- nDCG@10
- MRR

Baseline Models:

- Pure BM25
- VSM

## 5) MVP scope cut:

### 5.1) Query adaptivity

- MVP version only support one fixed weight formula for all queries (ie. foundational vs advanced vs influential are all treated the same). This is intentional to reduce project complexity.
- Non-free-text query are not supported for now.

### 5.2) Explainable output

- ie. A set of strings explaining why a particular document is ranked high.
- Not the current main scope for now, but worth keeping in mind.