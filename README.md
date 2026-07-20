# scholar-rank

ScholarRank is a graph-based, computationally efficient literature discovery engine that helps researchers query related papers in unfamiliar fields.

# Project Status (as of 2026-07-19)

**Data pipeline: done.** The full OpenAlex Works corpus (510M+ works, ~207GB compact) has been fetched from the
public OpenAlex S3 snapshot, extracted to a compact schema, and validated — see `docs/data_pipeline.md` for the
pipeline design and `python/src/scholar_rank/ingest/fetch_data.py` for the implementation.

**Current milestone (2-4 weeks): BM25 lexical retrieval + Global PageRank + Approximate top-k Personalized
PageRank, the latter two implemented in C++.** This is a deliberate scope narrowing — dense/semantic retrieval
is *not* part of this milestone (see below), so the near-term deliverable is closer to a graph-ranking
research/demo system with a lexical retrieval front end than a full search engine. That's an intentional
tradeoff to prioritize the project's core technical thesis (graph-based ranking as the differentiator — see
Project Motivation below) over retrieval breadth. Full reasoning: `docs/algorithm_design.md` §5.3.

**Semantic/embedding retrieval: planned, not abandoned.** Deferred until after the current milestone, to be
picked up once BM25 and the PPR engine are both working. See `docs/algorithm_design.md` for the retrieval
architecture it's designed to slot into.

# Project Motivation

Modern literacy recommendation tools mainly focuses on keyword/semantic search, citation counts, or general LLM recommendation. Each of these methods has their own limitations.

ScholarRank explores a different approach: Using the citation graph itself as a ranking signal.

The goal is to build a literature discovery tool that is:
- Computationally efficient
- Scalable to large citation graphs
- Explainable
- Benchmarkable

# Project structure

- `python/src/scholar_rank/` — Python package: data ingestion (`ingest/fetch_data.py`), shared utilities.
- `python/notebook/` — exploratory/one-off analysis (corpus null-rate sweeps, ad hoc backfills).
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
  - *Active*: BM25 over title/topics/keywords — the immediate next implementation step, via DuckDB's FTS
    extension.
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