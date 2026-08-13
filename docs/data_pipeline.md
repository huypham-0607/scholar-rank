# Data Pipeline

## What this is

ScholarRank's citation graph is built from [OpenAlex](https://openalex.org/), a free, open catalog of
scholarly works. Rather than querying OpenAlex's API one paper at a time (far too slow for hundreds of
millions of records), the pipeline pulls OpenAlex's full bulk data snapshot and processes it in bulk.

## Current status: complete

The full OpenAlex "Works" dataset — every paper OpenAlex tracks, along with its citation links and topic
tags — has been downloaded and processed:

- **510+ million papers**, matching OpenAlex's own published count exactly.
- Reduced down to **~241GB** of compact, ready-to-use data (from a much larger raw download — see below).
- Fully validated: record counts, IDs, and citation links have all been checked against the source data.

## How it works, briefly

OpenAlex's raw data is large — the full uncompressed snapshot is on the order of 700GB+ — and this project
runs on hardware that can't comfortably hold both the raw download and a processed copy at once. So instead
of downloading everything first and processing it after, the pipeline works one chunk at a time:

1. Download one chunk of raw data.
2. Immediately strip it down to only the fields this project actually needs (paper title, authors, citation
   links, topic tags, etc.) — discarding a lot of raw metadata that isn't relevant here.
3. Double-check the reduced version faithfully matches the original chunk.
4. Delete the raw chunk, keep only the compact version.
5. Move to the next chunk.

This keeps disk usage manageable throughout, rather than needing enough free space for the entire raw dataset
up front. The compact data is stored as [Parquet](https://parquet.apache.org/) files — a compressed, columnar
format that's both smaller on disk and fast to query with this project's tooling (DuckDB).

## A few honest data quirks worth knowing

- **About half of all papers are missing an abstract.** This isn't a bug — many publishers restrict bulk
  redistribution of abstract text, so OpenAlex simply doesn't have it for those papers. This project currently
  works around it by relying on paper titles and topic tags instead, which are available for nearly every
  paper.
- **A small fraction of citation links (~4%) point to papers that aren't in the dataset.** This happens
  naturally in any citation database — papers get merged, retracted, or deprecated over time. It's expected
  and accounted for, not something to "fix."
- Only the "Works" (papers) dataset has been processed so far. OpenAlex also tracks authors, journals, and
  topics as their own datasets — those aren't ingested yet, since the current project phase doesn't need them
  as standalone data. The pipeline code is now structured so a new dataset can be added as its own module,
  without changing the shared fetch/validate/delete logic — but nothing beyond Works has been built yet.
- **Abstracts are not kept in the compact data**, even for the roughly half of papers that have one. BM25
  search (see `docs/retrieval_engine.md`) only uses titles, topics, and keywords, so keeping abstracts around
  would use extra disk space for a field nothing currently reads.

## Potential update: PostgreSQL for fast lookups

We're exploring adding PostgreSQL alongside the current setup, specifically to speed up "look up this paper's
citation links" style queries needed while building small test datasets from the full graph. The current
tooling (DuckDB) is excellent for big, one-off analysis over the whole dataset, but not well suited to doing
many small, repeated lookups quickly at this scale.

The likely shape: keep DuckDB for bulk analysis and big one-time processing jobs, and use PostgreSQL
specifically for fast individual lookups — a tool built from the ground up for exactly that kind of access
pattern. Not yet started or decided; a more detailed technical plan exists in project notes for when this is
picked up.
