# Data Pipeline

## What this is

Startorch's citation graph is built from [OpenAlex](https://openalex.org/), a free, open catalog of
scholarly works. Rather than querying OpenAlex's API one paper at a time (far too slow for hundreds of
millions of records), the pipeline pulls OpenAlex's full bulk data snapshot and processes it in bulk.

## Current status: complete

The full OpenAlex "Works" dataset, every paper OpenAlex tracks, along with its citation links and topic tags, has been downloaded and processed:

- **510+ million papers**, matching OpenAlex's own published count exactly.
- Reduced down to **~241GB** of compact, ready-to-use data (from a much larger raw download — see below).
- Fully validated: record counts, IDs, and citation links have all been checked against the source data.

## How it works

OpenAlex's raw data is large. The full uncompressed snapshot is on the order of 700GB+, and this project runs on hardware that can't comfortably hold both the raw download and a processed copy at once. So instead
of downloading everything first and processing it after, the pipeline works one chunk at a time:

1. Download one chunk of raw data.
2. Immediately strip it down to only the fields this project actually needs (paper title, authors, citation
   links, topic tags, etc.), discarding a lot of raw metadata that isn't relevant here.
3. Double-check the reduced version faithfully matches the original chunk.
4. Delete the raw chunk, keep only the compact version.
5. Move to the next chunk.

This keeps disk usage manageable throughout, rather than needing enough free space for the entire raw dataset
up front. The compact data is stored as [Parquet](https://parquet.apache.org/) files — a compressed, columnar
format that's both smaller on disk and fast to query with this project's tooling (DuckDB).