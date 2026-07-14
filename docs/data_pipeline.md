# Data pipeline

## Data

Sources are taken from the [OpenAlex API](https://developers.openalex.org/).

Bulk metadata is sourced from the OpenAlex **snapshot** (S3 `s3://openalex/data/parquet/works/`), partitioned by
`updated_date`, not the per-ID REST API. At 510M+ works, the REST API is only appropriate for point lookups /
incremental updates after the initial bulk load — it cannot reasonably ingest the full corpus (page size 200,
rate-limited, would take days). Each partition/file is treated as one shard.

## Current state / disk constraint (as of 2026-07-12)

The raw snapshot was downloaded directly (`data/openalex/works/updated_date=*/part_*.parquet`) without extraction,
which is exactly the anti-pattern this project's Phase 1 design warns against. Status:

- `data/openalex/works/` currently holds **385 GB** of raw parquet shards, download stalled (empty
  `updated_date=2026-02-11/`), most likely because the filesystem ran low on space.
- Filesystem has **893 GB total, 164 GB free** right now.
- Manifest (`data/openalex/works/manifest.json`, generated 2026-06-26): **510,372,821** records,
  **724.97 GB** total raw content (~1.42 KB/record average). The full raw snapshot alone exceeds any
  600 GB project budget — it cannot be retained permanently, partially or fully.

**Action item:** stop bulk-downloading raw shards before extracting them. Process what's already on disk into
compact output, delete the raw shard once validated, then resume fetching remaining shards one at a time using
the same extract-then-delete loop (see Workflow below). This alone should free most of the 385 GB currently
stuck on disk.

## Format decision: Parquet (zstd), not JSONL/raw JSON

The compact per-shard output is written as **Parquet with zstd compression**, matching the raw snapshot's own
format. Reasons:

- Columnar + dictionary encoding compresses repeated categorical strings (type, language, source names, topic
  names) far better than JSONL, which re-pays string overhead on every record.
- Typed schema avoids re-parsing/re-validating on every downstream read.
- Native, zero-copy-ish integration with DuckDB/Polars/Arrow for Phase 2 graph construction (no JSON parsing step).
- Splittable and appendable, so per-shard files can be processed independently and merged later.

JSON/JSONL was considered and rejected: on measurement (see below), an equivalent JSONL compact export ran
3-5x larger than the Parquet+zstd equivalent for the same fields, with no compensating benefit for this project
(no external system requires JSON here).

## Size estimation (measured, not guessed)

Measured directly against the locally downloaded shards using DuckDB, rather than assumed from field counts:

**Method:** projected every downloaded shard's first part file (347 files, ~17 GB raw, 18,082,002 records —
about 3.5% of the full corpus, spanning the entire available date range 2016–2026) down to the recommended
compact schema (`id, doi, title, type, publication_year, language, cited_by_count, referenced_works,
referenced_works_count, primary_topic, topics, primary_location.source, top 3 authorships`), written as
Parquet/zstd, and compared byte sizes.

| Variant | Bytes/record | Extrapolated total (510.37M records) |
|---|---|---|
| Raw snapshot (measured, from manifest) | 1420.5 | 724.97 GB |
| Compact, IDs kept as full OpenAlex URLs | 95.9 | 48.9 GB |
| Compact, IDs stripped to bare form + topic/source names moved to dimension tables | 87.2 | 44.5 GB |

**Compact works dataset: ~45 GB for the entire 510M-work corpus** — a ~94% reduction from raw. This uses IDs
stripped of the `https://openalex.org/` prefix (store just `W1234567890`), and moves `topics.display_name` /
`primary_location.source.display_name` into small side lookup tables (`topics.parquet`, `sources.parquet`,
each a few thousand–hundred thousand rows, negligible size) joined at query time instead of repeating the
string in every one of 510M rows.

`abstract_inverted_index` is deliberately excluded (not in the recommended field list) — it is very likely the
single largest field in the raw record and has no use in Phase 1–3 (graph construction, ranking). Defer
extracting it, if ever needed for Phase 4 text retrieval, to a separate pass over only the subset of works kept
in the graph.

### Citation edges / graph footprint (Phase 2 planning)

- Measured average `referenced_works_count` across the sample: **4.65/work** → ≈ **2.37 billion** directed
  citation edges corpus-wide.
- Exploded edge list as `(src_id, dst_id)` int64 pairs: ≈ 38 GB (transient — only needed while building the CSR
  graph, can be deleted afterward).
- CSR forward + reverse neighbor arrays (int32 node IDs, since 510M nodes fits comfortably under the 2^31 limit):
  ≈ 19 GB combined.
- CSR offset arrays (both directions): ≈ 4 GB.
- `openalex_id_to_node_id` / `node_id_to_openalex_id` mapping table: ≈ 10 GB.

### Steady-state total footprint estimate

| Artifact | Size |
|---|---|
| `works_compact.parquet` (+ dimension tables) | ~45 GB |
| `raw_citation_edges.parquet` (transient, deletable post-Phase 2) | ~38 GB |
| CSR graph (forward + reverse, int32) | ~23 GB |
| ID mapping tables | ~10 GB |
| **Permanent total (excluding transient edge list)** | **~80-90 GB** |

This leaves large headroom under the 600 GB budget for benchmark datasets, indices, and later-phase artifacts,
and comfortably fits even in the 164 GB currently free — once the raw snapshot is no longer retained.

## Tooling & resources (to research before implementing)

**S3 access** (bucket referenced as `s3://openalex/data/parquet/works/...` in `manifest.json` — OpenAlex is
listed on the AWS Open Data program, so this is very likely anonymous/no-credential read access, but confirm
against OpenAlex's own download docs before assuming):

- `boto3` (AWS SDK for Python) — `list_objects_v2`/paginator to enumerate shard keys, `download_file` /
  streaming `get_object()['Body']` to fetch bytes, `Config(signature_version=UNSIGNED)` for anonymous access.
  Python-native, gives retry/progress control — recommended for the actual extractor script.
- AWS CLI (`aws s3 cp` / `sync ... --no-sign-request`) — simpler for ad-hoc/manual pulls, not a library, easy
  to shell out to if not worth writing boto3 code for a one-off resume.
- DuckDB `httpfs` extension (`INSTALL httpfs; LOAD httpfs;`) — can `read_parquet('s3://...')` directly over
  HTTP range requests, no separate local-download step. Worth prototyping on a single shard: if throughput is
  acceptable, it removes the download-then-delete round trip entirely for any shard not already local.

**Reading/transforming parquet** — key concept: Parquet is immutable. There is no in-place edit; every
transform is read → project/reshape → write a new file.

- `duckdb` — already proven on this exact nested schema during the size measurement (struct/list access,
  `list_transform`, `COPY (SELECT ...) TO ... (FORMAT parquet, COMPRESSION zstd)` in one statement). Runs
  out-of-core, so a shard never needs to fit fully in RAM. Primary recommended engine for consistency with
  work already done.
- `pyarrow` — lower-level; `pyarrow.parquet.ParquetFile.iter_batches()` for manual row-group-at-a-time
  streaming if finer Python-side control is needed between read and write. Interops directly with DuckDB
  (`.arrow()`), so the two aren't an either/or.
- `polars` — a viable DuckDB alternative (`scan_parquet`, lazy evaluation), dataframe-native instead of
  SQL-native, similar performance profile. Worth knowing if SQL feels awkward for a given transform.
- `pandas` — avoid as the primary engine: eager loading, weak nested-struct ergonomics (manual flattening
  needed), memory-heavy at this scale. Fine only for tiny side tables (e.g. the `topics.parquet` dimension
  table).

**Resources to look up:** boto3 S3 client docs (`list_objects_v2`, `download_file`, anonymous-access config);
DuckDB httpfs/S3 docs; PyArrow `pyarrow.parquet` module docs; OpenAlex's own "download to your machine" docs
(confirm bucket/region/anonymous-access specifics rather than assuming). No AWS account or API key is needed
for the bulk S3 snapshot path — that's only relevant to the REST API path (point lookups), where an email/API
key gets "polite pool" rate limits and should come from an env var, never hardcoded.

**On consolidating shards ("1 folder / 1 file"):** don't physically merge into one file per processing step.
Write one compact output file per processed shard (mirrors source partitioning, so each unit stays
independently retriable/deletable — important since parquet has no concurrent-append and a failed write
shouldn't risk a monolithic accumulated file), all under one directory (`data/processed/works/`). Every
relevant tool (DuckDB glob reads, `pyarrow.dataset`, Polars) can query a directory of parquet files as a single
logical table, so it already reads as "centralized" to any downstream consumer without physical merging. The
one-time compaction pass described below is the only point where physical merging should happen. This mirrors
how the raw snapshot itself is organized — many partitioned files, not one 725GB file — which is the standard
big-data columnar convention (same idea behind Hive partitioning / Delta Lake / Iceberg table formats, worth
knowing by name even without adopting them here).

## Workflow (shard-by-shard, matches Phase 1 responsibilities)

For each shard (one `part_NNNN.parquet` file under one `updated_date=` partition):

1. Fetch the shard if not already local (resume from `manifest.json`'s file list; only ever hold ~1 shard's
   worth of raw data at a time, worst case ~1.3 GB for the largest multi-part days).
2. Read with DuckDB, project to the compact schema (field list above), strip ID prefixes, move
   topic/source display names out to dimension tables.
3. Write compact output to `data/processed/works/updated_date=YYYY-MM-DD/part_NNNN.parquet`
   (mirrors source partitioning for resumability/auditability).
4. Validate: compact row count matches raw row count (or logs the filter reason/count if rows were dropped).
5. Append an entry to `extraction_manifest.json` (source shard, extraction date, rows processed/kept, byte
   sizes, schema version).
6. Delete the raw shard file once (3) and (5) are confirmed written.
7. Repeat.

Because compact output is ~7-9% of raw size, running this over the already-downloaded 385 GB should reclaim
the large majority of that space almost immediately, before any further raw shards are fetched.

Once all shards are processed, run a compaction pass (e.g. `COPY ... TO ... (PARTITION_BY publication_year)`
in DuckDB) to merge the many small per-shard compact files into fewer, larger row-group-optimized files —
`updated_date` partitioning is useful for resumable ingestion but not a useful downstream query key;
`publication_year` is more useful for later phases (e.g. "recent influential papers" queries in Phase 4).

## Known code/data mismatch to reconcile

`python/src/scholar_rank/ingest/fetch_data.py` currently scaffolds a per-ID REST `DataFetcher`
(`/works/{id}?api_key=...`), which does not match how the data on disk was actually obtained (bulk S3 snapshot).
The extraction pipeline described above should be the primary ingestion path; the REST client, if kept, should
be scoped to incremental updates/point lookups only, and should read its API key from an environment variable
rather than any hardcoded value.
