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

We will use Block-Max WAND with BM25 as our core ranking metric.

#### 2.2.1) Tokenization

We will construct our document by concatenating elements across 6 fields

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `title`                   | `VARCHAR`     | Title of given work |
| `topics`                  | `VARCHAR`     | OpenAlex assigned topics of given work |
| `subfields`               | `VARCHAR`     | OpenAlex assigned subfields of given work |
| `fields`                  | `VARCHAR`     | OpenAlex assigned fields of given work |
| `domains`                 | `VARCHAR`     | OpenAlex assigned domains of given work |
| `keywords`                | `VARCHAR`     | OpenAlex assigned keywords of given work |


As a preliminary design, we will consider each single word as a token. Hyphen connected words are collapsed an considered a single word. For instance "We value your well-being" will be considered 5 tokens: "We", "value", "your", "well", "being". For the scope of this project, machine learning based tokenization methods are not considered.

Normalization rules are as follows:
- All accents are stripped.
- All tokens will be decapitalized.
- All non-alphabetical characters are stripped (This is of minimal impact for our dataset, since our fields are generally low-nuance).

For stemmer, we will be using standard PyStemmer.

#### 2.2.2) Inverted Index List (Posting List).



## 3. Readings

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