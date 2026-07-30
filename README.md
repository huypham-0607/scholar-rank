# scholar-rank

ScholarRank is a graph-based, computationally efficient literature discovery engine that helps researchers query related papers in unfamiliar fields.

# Project Status (as of 2026-07-25)

**Data pipeline: done.** Full OpenAlex Works corpus (510M+ works, 241GB compact) fetched, extracted, and
validated. See `docs/data_pipeline.md`.

**Lexical retrieval (BM25): architecture decided, build in progress.**
- Tokenization pipeline built: title + topic hierarchy (subfield/field/domain) + keywords → normalized,
  stemmed tokens (`python/src/scholar_rank/tokenize/`).
- DuckDB's built-in full-text search index turned out too slow at full corpus scale, so retrieval is being
  built as a custom **Block-Max WAND** engine instead — a dynamic-pruning algorithm that finds top-k results
  without scoring every document. Python handles tokenization; C++ will build and serve the actual index. See
  `docs/retrieval_engine.md`.

**Graph engine (Global PageRank + Approximate top-k Personalized PageRank, C++): not started.** Prep work
done: a topic-restricted test subgraph (Mathematics field, ~4.7M works, real citation edges) to develop and
validate against, instead of iterating against the full 510M-node graph on every change.

**Semantic/embedding retrieval: still deferred, not abandoned** — picked up once BM25 and the graph engine
are both working.

## Current progress & near-term todo

- [x] Data pipeline (Phase 1)
- [x] Tokenization/normalization pipeline
- [x] Development test subgraph (Mathematics field subset)
- [ ] Block-Max WAND index construction + query engine (C++)
- [ ] Global PageRank (C++)
- [ ] Approximate top-k Personalized PageRank (C++)
- [ ] End-to-end validation against public benchmarks (BEIR for lexical, SNAP/OGB citation graphs for ranking)

# Project Motivation

Modern literacy recommendation tools mainly focuses on keyword/semantic search, citation counts, or general LLM recommendation. Each of these methods has their own limitations.

ScholarRank explores a different approach: Using the citation graph itself as a ranking signal.

The goal is to build a literature discovery tool that is:
- Computationally efficient
- Scalable to large citation graphs
- Explainable
- Benchmarkable

# Project structure

- `python/src/scholar_rank/` — Python package: data ingestion (`ingest/`), test-subgraph tooling (`subset/`),
  tokenization pipeline (`tokenize/`), shared utilities.
- `python/notebook/` — exploratory/one-off analysis (corpus null-rate sweeps, ad hoc backfills, field-score
  distributions).
- `docs/` — design docs: `initialization.md` (full project spec/phases), `data_pipeline.md` (Phase 1 pipeline
  design), `algorithm_design.md` (Phase 4 retrieval/scoring design), `data_reference.md` (OpenAlex field
  reference).
- `data/` — local only, gitignored: `openalex/` (raw, transient — deleted after validation), `compact/`
  (extracted corpus, the persistent artifact).

# Design Overview

Goals of this project:
- Design a system that searches for related papers for particular paper/topic
- Optimize such queries using traditional optimization/heuristics to be commercially viable.
- Run this system on OpenAlex full graph, benchmark the results.

General Idea:
A paper's relevance score for a given query combines three signals — full formula and pipeline design in
`docs/algorithm_design.md`:

- **Relevance ($R(d,q)$)** — how well a paper textually/semantically matches the query.
  - *Active*: BM25 over title/topic-hierarchy/keywords, served by a custom Block-Max WAND engine (retrieves
    top-k without scoring every document — DuckDB's built-in full-text index was tried first, too slow at
    full corpus scale). See `docs/retrieval_engine.md`.
  - *Deferred, not dropped*: embedding-based semantic relevance. Designed in, planned as a follow-up once BM25
    and the graph engine below are both working — see Project Status above.
- **Local authority ($LA(d,q)$)** — a paper's graph-based authority *relative to the query*. Approximate top-k
  - Personalized PageRank / local push, seeded from BM25 candidates but walking the **full** citation graph, not just the candidate subset.
- **Global authority ($GA(d)$)** — a paper's general citation-graph prestige, independent of any query.
  - Global PageRank is the primary signal here.
  - HITS/SALSA is a secondary candidate, currently under limited research.

**Current focus is the graph engine underneath $LA(d,q)$ and $GA(d)$** — Global PageRank and Approximate top-k
PPR, implemented in C++ (see Project Status above). This is the project's core technical thesis: using the
citation graph itself, not keyword/semantic matching, as the primary differentiator for literature discovery.

## References
- [The anatomy of a large-scale hypertextual Web search engine](https://snap.stanford.edu/class/cs224w-readings/Brin98Anatomy.pdf)
- [The $25,000,000,000 Eigenvector: The Linear Algebra Behind Google](https://www.rose-hulman.edu/~bryan/googleFinalVersionFixed.pdf)
- [Deeper Inside PageRank](https://www.stat.uchicago.edu/~lekheng/meetings/mathofranking/ref/langville.pdf)
- [The Probabilistic Relevance Framework:
BM25 and Beyond](https://www.staff.city.ac.uk/~sbrp622/papers/foundations_bm25_review.pdf)