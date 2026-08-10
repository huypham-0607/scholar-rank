# BMW Write-up.

## WAND core idea

First, lets define $\text{WAND}(X,w,\theta)$ (Weak AND) as the following.

$$ \sum_{1\leq i \leq k} x_{i}w_{i} \geq \theta $$

Where $x_{i}$ is the indicator variable for $X_{i}$, defined as:

$$ x_{i} = \begin{cases} 1, & \text{if } X_i \text{ is true} \\ 0, & \text{otherwise.} \end{cases} $$

Loosely speaking, $\text{WAND}(X,w,\theta)$ allows us to check for a boolean mask $X$, a weight distribution $w$, and a threshold $\theta$, whether sum of all elements in a subset of $w$ where $X_{i}$ is true, exceeded a certain value.

Why is this relevant to finding top $k$ most relevant document? Well, this allows us to construct a **score upper bound** of certain document $d$ based on term presence.

Given a query $q$, consider this scoring formula formula:

$$\text{Score}(d, q) = \sum_{1 \leq t \leq k} X_{t} \alpha_t w(t, d)$$

Where $\alpha_t$ is certain weighting factor dependent on $t$, $X_{t}$ denotes term $t$ presence in both doc $d$ and query $q$ ($1$ if true, $0$ if false), and $w(t,d)$ denotes some contribution of term $t$ to doc $d$.

We can see this pattern appear in many scoring formulas. Here is $\text{BM25}$ for reference.

$$ RSV_d = \sum_{t \in q} \log\left[ \frac{N}{\text{df}_t} \right] \cdot \frac{(k_1 + 1)\text{tf}_{td}}{k_1\left((1 - b) + b \times (L_d / L_{\text{ave}})\right) + \text{tf}_{td}} $$

$$ \log\left[ \frac{N}{\text{df}_t} \right] \cdot \frac{(k_1 + 1)\text{tf}_{td}}{k_1\left((1 - b) + b \times (L_d / L_{\text{ave}})\right) + \text{tf}_{td}} $$

Instead of calculating $w(t,d)$ for each term-doc pair, we can instead provide a loose upper bound.

$$ \text{EstScore}_{d,t} = \sum_{1 \leq t \leq k} \text{UB}_{t} X_{t} $$

Where $UB_{t}$ is the maximum contribution of term $t$ for any document $d$.

This approch prunes away documents that are wildly irrelevant to the query (very few or no terms in common), drastically speeding up our retrieval latency.

## WAND implementation.

The core of a WAND-based algorithm is maintaining these invariants:
- All documents with `doc_id <= cur_doc` have already been considered as candidates
- for any term `t`, any document containing term $t$, with `doc_id < posting[t].doc_id` has already been considered a candidate.

(I'll finish this after full implementation lol)

## BMW implementation.

Operations we need to support:
- Getting $\text{UB}_t$ - Upper bound contribution of a term.
- Getting $\text{BlockUB}_{t,b}$ - Upper bound contribution of a block
- Getting $w(t,d)$ - Actual contrib of term $t$ to doc $d$.
- $\text{NextShallow}(\text{list},d)$ - Moving block pointer to block containing $d$.
- $\text{Next}(\text{list},d)$ - Moving list pointer to doc with $id\geq d$

Constraints:
- We are only allowed to load block posting list into memory. Operations requiring access to specific document (Getting $w(t,d)$, $\text{Next}(\text{list},d)$) will be done out of core.

Upon receiving query, we need to support the following operations:
- Loading block posting list for each query term found. Initialize block pointer to first block.
- Initialize each posting pointer to 0. 
- For $\text{Next}(\text{list},d)$, binary search on block posting list. Based on file starting position in found block posting list, brute force to find value. 


Things we need to store in disk:
- Block metadata - Term Upper bound <float> at the start of each list, followed by:
    - <start_doc_id(vbe-compressed delta)>
    - <file_index(unsigned int)>
    - <start_addr(size_t)>
    - <block_ub(float)>
- Posting data:
    - <doc_id(vbe-compressed delta)>
    - <doc_tf(unsgined int)>
    - <doc_len(unsigned int)>


For querying:

Functions we have to implement:
- A mmap() wrapper for safe file handling
- pivoting(lists, \theta)
- next(list[i], d+1);
- next_shallow(list[i], d);
- evaluate_partial(d, p);
- get_new_candidate(d,p)

Program flow:
- Load up all Term -> block metadata.
- Load up mmap of all relevant files
- Initialize heap for top k retrieval
- Repeat
    - Sort all terms in non decreasing order of doc_id
    - find pivot p, cur_doc = p->doc_id
    - if p == MAX_DOC return
    - for i = 0 -> p
        - NextShallow(list[i], d)
    - flag = CheckBlockMax(\theta, p);
    if (flag == true):
        - if (list[0] -> doc_id == d) - Enough mass
            - EvaluatePartial(d, p);
            - Move all list[i] with Next(list[i], d+1) for i in [0,p]
        - else
            - Choose one preceeding list with largest IDF, move with Next(list[i], d+1)
    else
        - d' = get_new_candidate(d,p) - Getting new candidate with block skip logic
        - Choose one preceeding list with largest IDF, move with Next(list[i], d')