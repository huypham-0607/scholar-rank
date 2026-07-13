# scholar-rank

ScholarRank is a graph-based, computationally efficient literature discovery engine that helps researchers query related papers in unfamiliar fields.

# Project Motivation

Modern literacy recommendation tools mainly focuses on keyword/semantic search, citation counts, or general LLM recommendation. Each of these methods has their own limitations.

ScholarRank explores a different approach: Using the citation graph itself as a ranking signal.

The goal is to build a literature discovery tool that is:
- Computationally efficient
- Scalable to large citation graphs
- Explainable
- Benchmarkable

# Project structure


# Design Overview

Goals of this project:
- Design a system that searches for related papers for particular paper/topic
- Optimize such queries using traditional optimization/heuristics to be commercially viable.
- Run this system on OpenAlex full graph, benchmark the results.

General Idea:
The final score of "relatedness" of a certain paper to a given query will be a combination of multiple searching algorithms.

Potential candidates:
- Global PageRank: General prestige of any given paper, measured by the importance of other papers citing it.
- Personalized PageRank: Modified PG with a teleportation seed distribution relative to given query.
- HITS/SALSA: Measures "Hub" and "Authority" score of a certain paper
- Semantic Search algorithm to reduces search space for specific query.

## References
- [The anatomy of a large-scale hypertextual Web search engine](https://snap.stanford.edu/class/cs224w-readings/Brin98Anatomy.pdf)
- [The $25,000,000,000 Eigenvector: The Linear Algebra Behind Google](https://www.rose-hulman.edu/~bryan/googleFinalVersionFixed.pdf)
- 
