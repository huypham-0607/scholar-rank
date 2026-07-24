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

**Update**: Utilizing DuckDB built-in FTS extension for querying top-k relevant documents is potentially too slow. Exploring alternative sub-linear solutions instead.