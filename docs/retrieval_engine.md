# Retrieval Engine for Scholar-Rank

## 1. Okapi BM25 lexical retrieval

BM25 is a lexical scoring function. It quantify document relatedness to a certain query according to these metrics:

- No of documents containing a word $x$.
- No of occurences of word $x$ in a document $y$.
- Length of certain document $y$ (and by extension, average document length for normalization).

Given a query $Q$, containing $n$ keyword $q_{1}, q_{2}, \ldots q_{n}$, the BM25 score for document $D$ is:

$ \large score(D,Q) = \sum_{i=1}^{n}IDF(q_{i}) \cdot \frac{f(q_{i},D) \cdot (k+1)}
{f(q_{i},D) + k \cdot (1-b+b\cdot \frac{|D|}{avgdl})}$

Where:
- $f(q_{i},D)$: Frequency (occurences) of $q_{i}$ in document $D$
- $|D|$: Length of document $D$
- $avgdl$: Average length of all documents.
- $k$,$b$: Adjustable parameters.

Here, $IDF(q_{i})$ is the **Inverse Document Frequency** of a keyword, defined as:

$IDF(q_{i}) = ln(\frac{N-n(q_{i})+0.5}{n(q_{i})+0.5}+1)$

Loosely speaking, it quantifies "**rarity**" of a keyword among documents. The more documents containing $q_{i}$, the lower $IDF(q_{i})$ is.

The term $ \frac{f(q_{i},D)\cdot(k+1)}{f(q_{i},D)z+k(\ldots)} $ represents the information gain of $q_{i}$ appearing $f(q_{i},D)$, up to an upper limit of $(k+1)$.

Parameter $k$ adjusts both the **upper bound** and the **convergence rate** of the function. $k$ is generally in range of $[1.0,2.0]$

The term $1-b+b\frac{|D|}{avgdl}$ represents the **length normalization** for a document. The higher the document length, the more dilluted the score for each keyword will be.

Parameter $b$ adjust the dilluting effect of the document length.

Generally, the best value for $k$ and $b$ are $b \in [0.5,0.8]$ and $k \in [1.2,2.0]$

*Note: These concepts are very loosely explained. I will write a separate write-up on derivation of BM25 from the binary independence model, and its interpretation in detail.*

## 2. Implementation details

### 2.1) Key issues to consider

A few issues:
- Consider weight of Domain/Field/Subfield when two or more topics shares the same LCA
- Consider weight of certain word $x$ when it appears in multiple columns (Title/topics and its hierarchy/keywords).
- Consider utilizing weight of **topic/keyword scores** provided by OpenAlex.
- Consider the usage of **abstract** and its issue (High null rate, dilluted keyword/topic score due to length normalization)

### 2.2) Design

We will use BM25F as our core ranking system. BM25F will run on these fields:

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `title`                   | `VARCHAR`     | Title of the work |
| `topics`                  | `VARCHAR`     | Collapsed, concatenated & stopword-free topics. |
| `subfields`               | `VARCHAR`     | Collapsed, concatenated & stopword-free subfields. |
| `fields`                  | `VARCHAR`     | Collapsed, concatenated & stopword-free subfields. |
| `domains`                 | `VARCHAR`     | Collapsed, concatenated & stopword-free domains. |
| `keywords`                | `VARCHAR`     | Concatenated keywords. |

Additional notes: We are losing a lot of information by treating all keywords and topics with the same weight (especially for topic where the difference between rank 1 and rank 2 topic is substantial). There is a potential remedy for this - Using Reciprocal Rank Fusion to estimate the weight for each terms. But this feature will be delegated.

### 2.3) Resolutions to §2.1's concerns

1. **Domain/Field/Subfield weight when topics share an LCA**: undeduplicated hierarchy repetition is kept, not
   stripped — a paper whose topics converge on the same subfield/field/domain gets that concentration reflected
   naturally through repeated term frequency (subject to BM25's own saturation, so it doesn't runaway-inflate).
   Empirically, subfield-level convergence is the most informative level to lean on if this ever becomes an
   explicit feature (~32.6% of multi-topic docs converge there vs. 76.1% at field and 94.3% at domain — domain
   convergence is close to a given given only ~4-5 domains exist total, so it carries little discriminating
   signal despite being extremely common).
2. **Word appearing in multiple columns** (title/topics-hierarchy/keywords): resolved as part of the BM25F
   correction below — pooling term frequency across fields before one shared saturation curve means repeated
   words across fields don't get an inflated, double-counted bonus the way naively summing separate per-field
   BM25 scores would.
3. **OpenAlex topic/keyword `score` field**: **dropped for this phase** — topics and keywords are indexed with
   uniform term frequency, no weighting by their OpenAlex confidence score. Simplifies indexing (no need to
   inject per-token weight via repetition or custom tf) at the cost of losing real signal (topic rank-1 vs.
   rank-2 scores differ substantially — median 0.801 vs. 0.195). Revisit with rank-based weighting (not raw
   score magnitude) if this matters later — raw magnitude isn't safely comparable across documents (e.g.
   keyword scores run systematically ~0.05-0.13 higher for documents that have an abstract, since OpenAlex
   derives keyword scores from title+abstract), but a document's own rank ordering (its top keyword is still
   its top keyword) isn't affected by that confound.
4. **Abstract usage**: excluded from lexical scoring entirely for this phase — not just because it's long/noisy
   text, but because IDF computed over the whole corpus (abstract is null for 47.72% of it) makes any word
   confined to abstract text look artificially rarer, and therefore artificially higher-weighted, than its true
   in-context commonness (verified: "and" appears in 64% of abstract-having docs but only 33.5% of the full
   corpus, giving IDF≈1.09, not the near-zero a stopword should get). This isn't fixable by a stopword list
   alone since it applies to any word common-in-abstract but absent from null-abstract documents, not just
   canonical stopwords. A separate idea — rescaling other fields' weights per-document when abstract is null,
   so the weight "budget" stays constant — was considered and ruled out: BM25F has no real score ceiling
   proportional to total field weight, so there's nothing principled to redistribute; a missing field
   contributing exactly 0 is already correct behavior, identical to a present field with no query-term overlap.

**Update**: Utilizing DuckDB built-in FTS extension for querying top-k relevant documents is potentially too slow. Exploring alternative sub-linear solutions instead.

## 3. Approximate top-k retrieval (dynamic pruning)

`match_bm25`'s documented usage pattern (`SELECT *, match_bm25(id, query) FROM works WHERE score IS NOT NULL`)
projects over the entire base table before filtering — no evidence found that DuckDB pushes this into an index
seek (the internal `fts_main_<table>` schema isn't documented), and observed performance across the full 510M
rows is consistent with a full scan. The IR literature has a well-established answer to "get the top-k
BM25-scoring documents without scoring every candidate": dynamic pruning algorithms that exploit BM25's
per-term score upper bound (saturation guarantees a term can never contribute more than `IDF(q_i) * (k+1)`) to
skip documents that provably can't enter the top-k, without touching documents that share zero query terms.

Likely means implementing this ourselves as a custom component (own postings representation, own top-k loop)
rather than through DuckDB's FTS macro — WAND/BMW fundamentally need sorted posting-list iterators with
skip-ahead and a priority-queue control loop, which doesn't map onto declarative bulk SQL. This also parallels
Approximate top-k PPR (Phase 3): both are instances of "priority queue bounded by an upper-bound function"
best-first search, just with a different upper-bound function (residual mass for PPR's local push, per-term
score bound for BM25's dynamic pruning).

### 3.1) Core algorithms, roughly in reading order

- **MaxScore** — Turtle & Flood, [*"Query Evaluation: Strategies and Optimizations"*](https://research.engineering.nyu.edu/~suel/papers/bmm.pdf) (1995). The original term-at-a-time pruning
  idea: BM25's saturation bounds any single term's max contribution, so documents that can't possibly beat the
  current top-k threshold get skipped.
- **WAND (Weak AND)** — Broder, Carmel, Herscovici, Soffer, Zien, [*"Efficient Query Evaluation using a
  Two-Level Retrieval Process"*](https://www.researchgate.net/publication/221613425_Efficient_query_evaluation_using_a_two-level_retrieval_process)
  (CIKM 2003). The standard starting point — reduces full scoring by >90% with near-zero recall loss.
  Maintains posting-list iterators sorted by doc ID, skips using per-list max-score bounds.
- **Block-Max WAND (BMW)** — Ding & Suel, [*"Faster Top-k Document Retrieval Using Block-Max Indexes"*](https://research.engineering.nyu.edu/~suel/papers/bmw.pdf)
  (SIGIR 2011). Modern default — stores score upper bounds per *block* of postings instead of per whole list,
  enabling much tighter skipping. What production systems (Lucene 8+, etc.) actually run today.
- Follow-ups worth knowing exist, not essential first reads: Dimopoulos, Nepomnyachiy & Suel,
  [*"Optimizing Top-k Document Retrieval Strategies for Block-Max Indexes"*](https://research.engineering.nyu.edu/~suel/papers/bmm.pdf)
  (WSDM 2013); Mallia & Porciani, [*"Faster BlockMax WAND with Longer Skipping"*](https://www.antoniomallia.it/uploads/ECIR19a.pdf)
  (ECIR 2019) — variable-block refinements.

### 3.2) Background / grounding

- Manning, Raghavan & Schütze, *Introduction to Information Retrieval*, Ch. 7 ("Computing scores in a complete
  search system") — free at [nlp.stanford.edu/IR-book](https://nlp.stanford.edu/IR-book/). Covers static
  pruning (champion lists, tiered indexes) and dynamic pruning conceptually before WAND's specific mechanics —
  good to read before the papers above.

### 3.3) Reference implementations

- [PISA](https://github.com/pisa-engine/pisa) — production-grade C++ IR research engine implementing MaxScore,
  WAND, Block-Max WAND, Variable Block-Max WAND. Real code to study; a full system, not a minimal example.
- [ajikan/WAND-Implementation](https://github.com/ajikan/WAND-Implementation) — small, single-purpose C++ WAND
  implementation. Better first read than PISA for seeing the core algorithm without the surrounding engine.
- [Vespa: WAND — Accelerated OR search](https://docs.vespa.ai/en/ranking/wand.html) — practical explanation
  from a real production system's perspective, good alongside the papers.