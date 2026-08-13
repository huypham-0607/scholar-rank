# scholar-rank

ScholarRank is a graph-based, computationally efficient literature discovery engine that helps researchers query related papers in unfamiliar fields.

The goal is to utilize **Block-Max WAND** with **BM25 scoring**, **Global PageRank**, and **PPR** to query for top-k documents retrieval with **sub-second latency** across **500M+ research works** from OpenAlex database.

# Project Status (as of 2026-08-13)

**Data pipeline: done.** Full OpenAlex Works corpus (510M+ works, 241GB compact) fetched, extracted, and
validated. See `docs/data_pipeline.md`.

**Three technical pillars:**
- **Block-Max WAND** — a custom lexical retrieval engine (dynamic pruning, finds top-k results without scoring
  every document), replacing DuckDB's built-in full-text search, which was too slow at full corpus scale.
  Both halves are done and tested in C++: building the index, and answering a query against it (134 tests
  passing). Not yet callable from outside C++ — see "Next steps" below. See `docs/retrieval_engine.md`.
- **Global PageRank** — whole-graph authority score. Not started yet.
- **Approximate top-k Personalized PageRank (local push)** — query-time, seed-driven authority that stays
  bounded-memory by walking only the relevant part of the graph rather than the whole thing. Not started yet.

Supporting work: a topic-restricted test subgraph (Mathematics field, ~4.7M works, real citation edges) to
develop and validate against, instead of iterating against the full 510M-node graph on every change. C++ build
system, logging, and a small shared utility layer (RAII file/mmap wrappers, variable-byte encoding) are
working. On the Python side, a first pass at a real command-line tool (`scholar-rank`) and a shared config
file (`project-config.toml`) now exist, currently covering fetching data and building the test subgraph.

**Semantic/embedding retrieval: dropped for now**, not part of the near-term plan — judged too computationally
ambitious to take on alongside the three pillars above. May be revisited later (not before ~3 months out).

## Next steps

1. **Finish the Python side**: add the remaining command-line steps (building the index, running a query), and
   connect Python's query tokenizer to the C++ query engine, so a search can be run end to end from one command.
2. **Benchmark the C++ engine**: measure how fast Block-Max WAND retrieval is, and how good the results are.
3. **Then, one of two directions** (not decided yet): build a small demo of the project working end to end, or
   move straight on to the graph side — CSR/CSC graph storage, feeding into Global PageRank.

## Current progress & near-term todo

- [x] Data pipeline (Phase 1)
- [x] Tokenization/normalization pipeline (incl. dense doc_id remapping)
- [x] Development test subgraph (Mathematics field subset)
- [x] C++ build system + logging
- [x] Block-Max WAND index construction (SPIMI build + BMW block-metadata merge, C++)
- [x] Block-Max WAND query engine (C++)
- [ ] Python command-line tool + config, connected end-to-end to the C++ engine
- [ ] Benchmark the C++ retrieval engine
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

Items marked ✅ exist and work (tested); ⏳ exist but incomplete; unmarked = not started yet.

```
scholar-rank/
├── cpp/                                 C++ retrieval + (future) graph engine, built with CMake
│   ├── CMakeLists.txt                  ✅ C++20, ctest wired up
│   ├── include/, src/
│   │   ├── utils/                      ✅ SafeFile/SafeFileMmap (RAII wrappers), logger, variable-byte encoding
│   │   ├── retrieval/                  ✅ SPIMI index construction + BMW query engine — done and tested
│   │   └── graph/                      # CSR/CSC, PageRank, PPR — not started
│   ├── apps/                           ✅ CLI: build_inverted_blocks, build_doc_len_list, merge_inverted_blocks
│   │                                      (no CLI entry point for running a query yet)
│   ├── tests/                          ✅ 134 GoogleTest cases (`ctest --test-dir build`)
│   └── benchmarks/                     # BEIR/SNAP/OGB datasets already compiled, not wired up yet
│
├── python/
│   ├── src/scholar_rank/
│   │   ├── cli.py                      ⏳ the `scholar-rank` command — ingest + gen-works-subset done,
│   │   │                                  build-posting + query not added yet
│   │   ├── ingest/fetch_data.py        ✅ EntityIngestor (base class) + WorksIngestor
│   │   ├── works_subset/works_subset.py ✅ WorksSubsetter — builds the smaller test subgraph
│   │   ├── tokenizer/tokenizer.py      ✅ tokenization + dense doc_id remapping
│   │   └── utils.py                    ✅ shared logging/path helpers
│   ├── script/smoke_test.py            ✅ dependency-free sanity checks for the CLI
│   └── notebook/                       exploratory/one-off analysis (null-rate sweeps, backfills, etc.)
│
├── docs/                                design docs
│   ├── initialization.md               full project spec: phases 1-5, success criteria
│   ├── algorithm_design.md             Phase 4 retrieval/scoring design, full cpp/python project tree
│   ├── retrieval_engine.md             BM25/Block-Max WAND design, in depth
│   ├── data_pipeline.md                Phase 1 pipeline design (complete)
│   └── data_reference.md               OpenAlex field reference
│
├── project-config.toml                 ✅ paths + subset filter profiles, read by the CLI
├── data/                                local only, gitignored: openalex/ (raw, transient), compact/ (kept)
└── README.md                            this file
```

See `docs/algorithm_design.md` for the file-by-file breakdown of `cpp/` and `python/`.

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
  - *Dropped for now*: embedding-based semantic relevance — too computationally ambitious to take on alongside
    the current milestone. See Project Status above.
- **Local authority ($LA(d,q)$)** — a paper's graph-based authority *relative to the query*. Approximate top-k
  - Personalized PageRank / local push, seeded from BM25 candidates but walking the **full** citation graph, not just the candidate subset.
- **Global authority ($GA(d)$)** — a paper's general citation-graph prestige, independent of any query.
  - Global PageRank is the primary signal here.
  - HITS/SALSA is a secondary candidate, currently under limited research.

**Current focus is finishing the retrieval path** — connecting the finished Block-Max WAND engine to a real
command-line tool, then benchmarking it (see "Next steps" above), before deciding whether to build a small
demo or move on to the graph engine behind $LA(d,q)$ and $GA(d)$. The citation graph itself, not
keyword/semantic matching, remains the project's core technical thesis — retrieval is the supporting layer
that needs to work first.

## References
- [The anatomy of a large-scale hypertextual Web search engine](https://snap.stanford.edu/class/cs224w-readings/Brin98Anatomy.pdf)
- [The $25,000,000,000 Eigenvector: The Linear Algebra Behind Google](https://www.rose-hulman.edu/~bryan/googleFinalVersionFixed.pdf)
- [Deeper Inside PageRank](https://www.stat.uchicago.edu/~lekheng/meetings/mathofranking/ref/langville.pdf)
- [The Probabilistic Relevance Framework:
BM25 and Beyond](https://www.staff.city.ac.uk/~sbrp622/papers/foundations_bm25_review.pdf)
