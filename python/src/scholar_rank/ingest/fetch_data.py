"""Shard-by-shard ingestion pipeline for the OpenAlex bulk S3 snapshot.

Streams shards from the public S3 snapshot, projects each one down to ScholarRank's
compact schema via DuckDB, validates the compact output against the shard's own
declared manifest stats, and deletes the raw shard once validated — so the full raw
snapshot never needs to be held on disk at once. See docs/data_pipeline.md for the
full design and disk-budget reasoning.

Entry point: EntityIngestor.orchestrate(). Raw shards live under data/openalex/,
compact output under data/compact/. Includes a resumability mechanism
(list_local_and_remote_shards) so a re-run doesn't re-fetch/re-extract shards a prior
run already finished.

Structure: EntityIngestor is an abstract base class holding everything that isn't
entity-specific (manifest handling, fetch/delete, logging, the orchestration loop).
A concrete subclass — currently only WorksIngestor — supplies the entity's schema
(extracted_columns/columns/entity as class attributes) and its three entity-specific
behaviors (extract_compact, validate_compact_data, validate_compact_shard) as
abstract-method overrides. Adding Authors/Sources/Topics later means adding another
subclass, not touching EntityIngestor. See "Multi-entity architecture" in
docs/data_pipeline.md.

No shared config system exists yet, so paths (upstream_prefix/raw_path/compact_path)
are constructor arguments rather than class attributes or module constants — each
instance is told where its own directories live. Revisit once the config refactor
lands (see CLAUDE.md's "Python stack refactor" notes).
"""

import boto3
import duckdb
import json
import os

from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from scholar_rank.utils import get_logger, PROJECT_ROOT, get_current_time
from botocore import UNSIGNED
from botocore.client import Config

logger = get_logger(__name__)

# Module-level defaults for WorksIngestor's paths - not config, just where this repo's
# data currently lives. See module docstring re: no config system yet.
UPSTREAM_PREFIX = Path("data/parquet")
RAW_PATH = PROJECT_ROOT/"data"/"openalex"
COMPACT_PATH = PROJECT_ROOT/"data"/"compact"


class EntityIngestor(ABC):
    """Base class for OpenAlex bulk-snapshot ingestion, one subclass per entity.

    Factors out everything that isn't entity-specific: manifest download/parsing,
    the local-vs-remote shard split (resumability), raw-shard fetch/delete, the
    extraction log, and the orchestration loop itself. A subclass only needs to
    supply:

    - Class attributes ``extracted_columns``, ``columns``, ``entity`` — the entity's
      schema and OpenAlex entity name.
    - ``extract_compact`` / ``validate_compact_data`` / ``validate_compact_shard`` —
      entity-specific behavior. All three are abstract, including
      ``validate_compact_shard`` even though its own logic *looks* generic at a
      glance: its measurement helper (``WorksIngestor._get_shard_validation_result``)
      hardcodes the Works-specific ``referenced_works``/``referenced_works_count``
      column names for the link-integrity check, so it isn't actually
      schema-agnostic. A future entity without that field needs its own version, not
      a shared default.
    """

    extracted_columns: list[str]
    columns: list[str]
    entity: str
    registry: dict[str, type["EntityIngestor"]] = {}

    @dataclass(kw_only=True)
    class ManifestData:
        """One entry from an entity's manifest.json — S3 key plus the raw shard's declared stats.

        key is stripped of the 's3://openalex/data/parquet/' prefix, so it doubles as the
        relative path fragment used under both raw_path and compact_path (e.g.
        'works/updated_date=2016-06-24/part_0000.parquet'). content_length/record_count are
        OpenAlex's own declared values for the *raw* shard — used as ground truth by
        the entity's shard-validation-result helper rather than re-measured from the raw
        file. Nested here (not module-level) since it's the manifest-entry type every
        EntityIngestor subclass shares, not something meaningful outside this hierarchy.
        """
        key: Path               # Key (directory) of a file on S3
        content_length: int     # File size
        record_count: int       # No of records in shard

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        EntityIngestor.registry[cls.entity] = cls

    def __init__(self, upstream_prefix: Path, raw_path: Path, compact_path: Path):
        self.upstream_prefix = upstream_prefix
        self.raw_path = raw_path
        self.compact_path = compact_path

    @property
    @abstractmethod
    def entity(self) -> str:
        raise NotImplementedError

    def get_manifest_data(self, manifest_path: Path) -> list:
        """Parse a local manifest.json into a list of ManifestData.

        Strips the 's3://openalex/data/parquet/' prefix from each file's url, leaving a
        key relative to the bucket root that doubles as the local path fragment under both
        raw_path and compact_path — useful both for fetching with boto3 and as a local
        file-directory lookup. Entity-agnostic — every OpenAlex entity's manifest.json has
        this same shape, so no subclass ever needs to override this.

        Concerns:
            Hardcodes manifest.json's structure (top-level "files" list, each with
            "url"/"meta.content_length"/"meta.record_count") — will raise KeyError if
            OpenAlex changes this shape. Keep in mind for future maintenance.

        Args:
            manifest_path: Path to a locally downloaded manifest.json.

        Returns:
            A list of ManifestData, one per shard listed in the manifest.

        Raises:
            FileNotFoundError: manifest_path doesn't exist.
            json.JSONDecodeError: manifest_path isn't valid JSON.
            KeyError: an entry is missing the expected "url"/"meta" fields.
            FileNotFoundError/JSONDecodeError are logged as a warning here, then re-raised.
        """

        manifest_data = None
        try:
            with open(manifest_path, 'r') as f:
                manifest_data = json.load(f)
        except Exception as e:
            logger.warning(f"Unable to open manifest.json: {e}")
            raise

        # Getting all files metadata in manifest.json, and stripping s3 prefix.
        lst = [self.ManifestData(
            key = Path(obj["url"].replace('s3://openalex/data/parquet/', '')) if ("url" in obj)
                    else obj["key"],
            content_length = obj['meta']['content_length'],
            record_count = obj['meta']['record_count']
        ) for obj in manifest_data["files"]]

        return lst

    def get_manifest(self, upstream_path, dest_path: Path) -> None:
        """Download this entity's manifest.json from the OpenAlex S3 snapshot.

        Fetches the upstream manifest (the file listing every shard for this entity, with
        per-shard record_count/content_length) via anonymous S3 access and writes it to
        dest_path. Always the first step of orchestrate() — every other method in this
        class that reads a manifest reads it back from dest_path, not from S3 directly.

        Concerns:
            Does not create dest_path's parent directory — assumes it already exists.

        Args:
            upstream_path: Key of manifest.json in the 'openalex' S3 bucket.
            dest_path: Local path to write the downloaded manifest.json to.

        Returns:
            None

        Raises:
            botocore.exceptions.ClientError: S3-side failure (missing key, access denied).
            boto3.exceptions.RetriesExceededError: transient network errors exhausted retries.
            OSError: local write failure (e.g. dest_path's parent directory missing, disk
                full) — unwrapped, since the underlying write is a bare open().
            All of the above are logged as a warning here, then re-raised to the caller.
        """

        s3 = boto3.client('s3', config=Config(signature_version=UNSIGNED))
        try:
            s3.download_file('openalex', str(upstream_path), str(dest_path))
        except Exception as e:
            logger.warning(f"Failed to fetch {upstream_path}.")
            raise

        logger.info(f"Fetched {upstream_path} successfully.")

    def list_local_and_remote_shards(self, manifest_path: Path, raw_path: Path) -> (list, list):
        """Split this entity's manifest entries into what's already downloaded vs. not.

        Checks, for every ManifestData in the manifest, whether raw_path/key exists as a
        local file. This is the resumability mechanism referenced in the module docstring —
        orchestrate() uses this split to skip re-fetching shards across runs.

            raw_path should be the parent folder of all entity folders:
            {raw_path}
                /works
                /authors
                /sources

        Args:
            manifest_path: pathlib.Path to manifest.json locally.
            raw_path: pathlib.Path to the raw data directory corresponding to manifest_path.

        Returns:
            (local_shard_list, remote_shard_list) — two lists of ManifestData.

        Raises:
            Propagates whatever get_manifest_data raises (FileNotFoundError,
            json.JSONDecodeError, KeyError).
        """

        data = self.get_manifest_data(manifest_path)

        remote_shard_list = []
        local_shard_list = []

        for obj in data:
            # Extracting only the directory of a shard (Hardcoded, subject to edits)
            if (not Path(raw_path/obj.key).is_file()):
                remote_shard_list.append(obj)
            else:
                local_shard_list.append(obj)

        return (local_shard_list, remote_shard_list)

    def fetch_shard(self, upstream_path, dest_path: Path) -> None:
        """Download a single raw shard from the OpenAlex S3 snapshot.

        Concerns:
            Does not create dest_path's parent directory — orchestrate() relies on
            raw_path/entity/... already existing from a prior download.

        Args:
            upstream_path: Key of the shard on S3 (ManifestData.key).
            dest_path: Local path to save the shard to.

        Returns:
            None

        Raises:
            botocore.exceptions.ClientError: S3-side failure.
            boto3.exceptions.RetriesExceededError: transient network errors exhausted retries.
            OSError: local write failure (e.g. dest_path's parent directory missing, disk
                full) — unwrapped, since the underlying write is a bare open().
            All of the above are logged as a warning here, then re-raised to the caller.
        """

        dest_path.parent.mkdir(parents=True, exist_ok=True)
        s3 = boto3.client('s3', config=Config(signature_version=UNSIGNED))
        try:
            s3.download_file('openalex', str(upstream_path), str(dest_path))
        except Exception as e:
            logger.warning(f"Failed to fetch {upstream_path}.")
            raise

    def show_shard(self, shard_path: Path):
        """Debug helper: print a shard's contents to stdout via DuckDB's relation display.

        Not part of the orchestrated pipeline — used for ad-hoc inspection during
        development (e.g. from a notebook).

        Args:
            shard_path: Path to any parquet file, raw or compact.

        Returns:
            None

        Raises:
            Propagates any DuckDB error unhandled (e.g. shard_path doesn't exist or isn't
            valid parquet) — no try/except here.
        """
        db = duckdb.connect()
        rel = db.read_parquet(shard_path)

        rel.show()

    def delete_shard(self, shard_path: Path) -> None:
        """Delete a shard file (raw or compact) to reclaim disk space.

        In orchestrate(), only ever called on a raw shard, and only after that shard's
        compact output has passed validate_compact_shard with zero errors — this is the
        one irreversible step in the pipeline (raw shards are not retained, per
        docs/initialization.md's Phase 1 anti-pattern warning). Callers must not call this
        before validation has actually passed.

        Args:
            shard_path: Path to the shard to delete.

        Returns:
            None

        Raises:
            missing_ok=True means a missing file is silently a no-op, not an error.
            Other OSError (e.g. permission denied) still propagates.
        """

        shard_path.unlink(missing_ok=True)

    def init_extraction_log(self, extraction_log_path: Path) -> None:
        """Create (or overwrite) the per-run extraction log with a timestamp header.

        Called once at the start of orchestrate(), before any shard is processed — resets
        the log to record only the current orchestration pass's errors, not history from
        prior runs. See append_extraction_log for the per-shard entries written into it.

        Concerns:
            Overwriting on every orchestrate() call means log history from a previous
            (possibly incomplete) run is lost the moment the next run starts — intentional
            (this log is "this run's errors", not a durable cross-run audit trail), not an
            oversight, but worth knowing if you're trying to review a past failed run.
            Assumes extraction_log_path's parent directory already exists — will raise on
            a truly first-ever run, before any shard has gone through extract_compact's
            own mkdir call, since nothing else creates compact_path/entity/ first.

        Args:
            extraction_log_path: Path to the log file to create/overwrite.

        Returns:
            None

        Raises:
            OSError (e.g. FileNotFoundError if the parent directory doesn't exist) —
            logged as a warning here, then re-raised.
        """
        try:
            with open(extraction_log_path, "w", encoding="utf-8") as f:
                f.write(f"Time created: {get_current_time()}\n")
        except Exception as e:
            logger.warning(f"Failed to open extraction_log.txt: {e}")
            raise

    def append_extraction_log(self, extraction_log_path: Path, message: str) -> None:
        """Append one message to the extraction log, without touching existing content.

        Args:
            extraction_log_path: Path to the log file (must already exist —
                see init_extraction_log).
            message: Text to append; a trailing newline is added automatically.

        Returns:
            None

        Raises:
            OSError (e.g. FileNotFoundError if extraction_log_path doesn't exist) —
            logged as a warning here, then re-raised.
        """
        try:
            with open(extraction_log_path, "a", encoding="utf-8") as f:
                f.write(f"{message}\n")
        except Exception as e:
            logger.warning(f"Failed to open extraction_log.txt: {e}")
            raise

    @abstractmethod
    def extract_compact(self, shard_path, out_path: Path):
        """Project one raw shard down to ScholarRank's compact schema via DuckDB.

        Entity-specific: the projection (which columns, how each is reshaped/cleaned)
        differs per entity. See WorksIngestor.extract_compact for the Works version.

        Args:
            shard_path: Local shard path for extraction.
            out_path: Output file path after extracting metadata (parent directories are
                created if missing).

        Returns:
            None
        """
        raise NotImplementedError

    @abstractmethod
    def validate_compact_data(self, manifest_path, compact_path: Path):
        """Full-corpus audit across every compact shard currently on disk for this entity.

        Entity-specific: which cross-shard integrity checks make sense (e.g. dangling
        reference detection) depends on the entity's own fields. See
        WorksIngestor.validate_compact_data for the Works version.

        Args:
            manifest_path: Path to the entity's locally downloaded manifest.json.
            compact_path: Root directory of compact output.

        Returns:
            None
        """
        raise NotImplementedError

    @abstractmethod
    def validate_compact_shard(self, file: ManifestData) -> list[str]:
        """Validate one already-extracted compact shard, returning a list of errors.

        Entity-specific (see class docstring for why this is abstract despite looking
        generic) — see WorksIngestor.validate_compact_shard for the Works version.

        Args:
            file: The ManifestData entry for the shard to validate.

        Returns:
            A list of error message strings — empty if the shard passed every check.
            Callers should treat an empty list as the pass signal (orchestrate() checks
            len(errors) == 0).
        """
        raise NotImplementedError

    def orchestrate(self, forced_fetch: bool = False):
        """Run one full ingestion pass for this entity: fetch, extract, validate, delete, per shard.

        Downloads the manifest, splits shards into already-local vs. remote (resumability),
        then for every shard: extract -> validate -> either delete the raw copy (validation
        passed) or log the failure and keep the raw copy (validation failed, so nothing
        unsafe is ever deleted). Remote shards whose compact output already exists are
        skipped entirely, on the assumption a prior run already validated them (see
        Concerns). Once every shard's per-shard checks are done, runs validate_compact_data
        as a final full-corpus audit — but only if invalid_shard_count is 0, since a
        dangling-reference/uniqueness sweep isn't very meaningful over a dataset already
        known to have per-shard problems.

        Escalation: if invalid_shard_count reaches invalid_shard_limit (50), raises
        RuntimeError immediately rather than continuing — bounds how much raw data can pile
        up undeleted (the actual motivating constraint, given the project's disk budget),
        and incidentally also bounds how long a systemic extraction bug could run before
        someone notices.

        Concerns:
            - The remote_shards skip check (raw absent + compact present => treat as
              already validated) assumes every existing compact file was produced by this
              validated pipeline. Any compact file written by an earlier, ungated process
              (e.g. a manual notebook cell calling extract_compact directly) would be
              silently trusted here without ever having actually passed validation.
            - invalid_shard_count is cumulative across the whole run, not consecutive — 50
              failures scattered across many shards reads very differently from 50 in a
              row (the latter being a much stronger signal of a systemic bug), but both
              hit the same threshold identically.

        Args:
            forced_fetch: boolean, if True then files are refetched regardless if compact
            shard is present.

        Returns:
            None. Side effects: downloads/deletes files under raw_path, writes files under
            compact_path (extraction_log.txt always; integrity_report.txt and
            compact_manifest.json only on a fully clean run, via validate_compact_data).

        Raises:
            RuntimeError: invalid_shard_count reached invalid_shard_limit.
            Propagates any other unhandled exception from get_manifest, fetch_shard,
            extract_compact, or the validation chain — intentional: an unexpected
            exception here should halt the run loudly rather than be silently swallowed.
            Only the validation-failure path is deliberately handled as skip-and-continue.
        """
        upstream_manifest_path = self.upstream_prefix/self.entity/"manifest.json"
        manifest_path = self.raw_path/self.entity/"manifest.json"

        Path(self.raw_path/self.entity).mkdir(parents=True, exist_ok=True)
        Path(self.compact_path/self.entity).mkdir(parents=True, exist_ok=True)

        self.get_manifest(upstream_manifest_path, manifest_path)

        local_shards, remote_shards = self.list_local_and_remote_shards(manifest_path, self.raw_path)

        extraction_log_path = self.compact_path/self.entity/"extraction_log.txt"

        # Setting a limit for how many invalid shards before raising exception.
        invalid_shard_limit = 50
        invalid_shard_count = 0

        self.init_extraction_log(extraction_log_path)

        logger.info(f"Initiating shard extraction for entity {self.entity}")

        for file in local_shards:
            logger.info(f"Current shard: {file.key} [local].")
            logger.info(f"Extracting shard {file.key}...")

            self.extract_compact(self.raw_path/file.key, self.compact_path/file.key)
            logger.info(f"Extracted shard {file.key} successsfully, validating shard...")

            errors = self.validate_compact_shard(file)

            if len(errors) != 0:
                invalid_shard_count += 1
                for error in errors:
                    logger.warning(error)
                    self.append_extraction_log(extraction_log_path, error)
            else:
                self.delete_shard(self.raw_path/file.key)
                logger.info(f"Validated shard {file.key} successfully: No errors found.")

            if invalid_shard_count >= invalid_shard_limit:
                logger.warning(f"Invalid shard count exceeded limit {invalid_shard_limit}, raising...")
                raise RuntimeError(f"Invalid shard limit exceeded (limit: {invalid_shard_limit}).")

        for file in remote_shards:
            # Raw files not on local & compact file exist implies shard passed validation check.
            if (not forced_fetch and Path(self.compact_path/file.key).is_file()):
                continue

            logger.info(f"Current shard: {file.key} [remote].")
            logger.info(f"Fetching shard {file.key}...")

            self.fetch_shard(self.upstream_prefix/file.key, self.raw_path/file.key)
            logger.info(f"Fetched shard {file.key} successfully, extracting shard...")

            self.extract_compact(self.raw_path/file.key, self.compact_path/file.key)
            logger.info(f"Extracted shard {file.key} successsfully, validating shard...")

            errors = self.validate_compact_shard(file)

            if len(errors) != 0:
                invalid_shard_count += 1
                for error in errors:
                    logger.warning(error)
                    self.append_extraction_log(extraction_log_path, error)
            else:
                self.delete_shard(self.raw_path/file.key)
                logger.info(f"Validated shard {file.key} successfully: No errors found.")

            if invalid_shard_count >= invalid_shard_limit:
                logger.warning(f"Invalid shard count exceeded limit {invalid_shard_limit}, raising...")
                raise RuntimeError(f"Invalid shard limit exceeded (limit: {invalid_shard_limit}).")

        if invalid_shard_count != 0:
            logger.info(f"""
                Orchestration completed - localized shard errors detected
                (invalid shards: {invalid_shard_count})").\n
                Please navigate to {extraction_log_path} for more details.
            """)
            return

        logger.info(f"Finished shard extraction, validating all compact data...")
        self.validate_compact_data(manifest_path, self.compact_path)
        logger.info(f"""
            Orchestration completed - no localized shard errors.\n
            Please navigate to {self.compact_path/self.entity/"integrity_report.txt"} for comprehensive analysis.
        """)


class WorksIngestor(EntityIngestor):
    """Works-entity ingestion — the only entity implemented so far."""

    # Columns being extracted from raw shards
    extracted_columns = [
        "id",
        "doi",
        "title",
        "authorships",
        "abstract_inverted_index",
        "type",
        "language",
        "primary_location",
        "publication_year",
        "publication_date",
        "referenced_works",
        "referenced_works_count",
        "cited_by_count",
        "topics",
        "keywords"
    ]

    # Columns present in final compact shard
    columns = [
        "id",
        "doi",
        "title",
        "authorships",
        "authorships_truncated",
        "abstract_inverted_index",
        "type",
        "language",
        "primary_location",
        "publication_year",
        "publication_date",
        "referenced_works",
        "referenced_works_count",
        "cited_by_count",
        "topics",
        "keywords"
    ]

    entity = "works"

    @dataclass(kw_only=True)
    class _ShardValidationResult:
        """Raw-vs-compact stats for one shard, for validate_compact_shard to interpret.

        raw_* fields come from the manifest (OpenAlex's own declared values); the rest is
        measured directly against the compact output on disk by
        _get_shard_validation_result. This class only holds data — pass/fail
        interpretation lives in validate_compact_shard. Private (leading underscore) and
        nested here rather than module-level: it's an implementation detail of how
        WorksIngestor validates a shard, not part of the public interface.
        """
        raw_content_length: int
        compact_content_length: int
        raw_record_count: int
        compact_record_count: int
        link_mismatch_count: int
        is_valid_schema: bool
        missing_columns: list[str]
        redundant_columns: list[str]

    def extract_compact(self, shard_path, out_path: Path):
        """Project one raw Works shard down to ScholarRank's compact schema via DuckDB.

        Reads shard_path, strips the 'https://openalex.org/'/'https://doi.org/' prefixes
        from every embedded ID, truncates authorships to the first two plus the last
        (flagging authorships_truncated when the original list had more than 3), reshapes
        topics to keep id/display_name/score plus the subfield/field/domain hierarchy, and
        writes the result to out_path as Parquet+zstd. Full compact column list: see the
        `columns` class attribute. Field-level details: data_reference.md.

        Verified (2026-07-16, against a real shard where 106/2439 works had >3 authors):
            authorships_truncated correctly reads true for exactly those 106 rows.
            DuckDB's `* REPLACE(...)` scoping resolves the trailing authorships_truncated
            expression against the original (pre-truncation) authorships column, not the
            already-replaced one, despite both appearing in the same SELECT list. Previously
            flagged here as an open question ("need to confirm if this has 100% false
            rate") — confirmed correct, not a bug.

        Concerns:
            No validation happens here — this method always writes output regardless of
            whether the projection is actually correct; correctness is checked separately,
            after the fact, by _get_shard_validation_result / validate_compact_shard.

        Args:
            shard_path: Local shard path for extraction.
            out_path: Output file path after extracting metadata (parent directories are
                created if missing).

        Returns:
            None

        Raises:
            Propagates any DuckDB error unhandled (e.g. malformed/unexpected raw schema,
            disk full during the final COPY) — no try/except here.
        """

        db = duckdb.connect()
        rel = db.read_parquet(shard_path).select(*self.extracted_columns)

        # Number of records is an invariant after the transform. Count here for cheaper computation.
        record_count = rel.aggregate("count(*)").fetchone()[0]
        rel = rel.select("""
            * REPLACE (
                replace(id, 'https://openalex.org/', '') AS id,

                list_transform(
                    CASE WHEN len(authorships) <= 3
                        THEN authorships
                        ELSE authorships[1:2] || [authorships[-1]]
                    END,
                    a -> {
                        'id': replace(a.author.id, 'https://openalex.org/', ''),
                        'display_name': a.author.display_name,
                        'raw_author_name': a.raw_author_name
                    }
                ) AS authorships,

                CASE WHEN primary_location.source IS NULL THEN NULL
                    ELSE {
                        'id': replace(primary_location.source.id, 'https://openalex.org/', ''),
                        'display_name': primary_location.source.display_name,
                        'type': primary_location.source.type,
                        'is_oa': primary_location.is_oa
                    }
                END AS primary_location,

                list_transform(topics, a -> {
                    'id': replace(a.id, 'https://openalex.org/', ''),
                    'display_name': a.display_name,
                    'score': a.score,
                    'subfield_id': split_part(a.subfield.id, '/', -1),
                    'subfield_display_name': a.subfield.display_name,
                    'field_id': split_part(a.field.id, '/', -1),
                    'field_display_name': a.field.display_name,
                    'domain_id': split_part(a.domain.id, '/', -1),
                    'domain_display_name': a.domain.display_name
                }) AS topics,

                replace(doi, 'https://doi.org/', '') AS doi,

                list_transform(keywords, a -> {
                    'id': replace(a.id, 'https://openalex.org/', ''),
                    'display_name': a.display_name,
                    'score': a.score
                }) AS keywords,

                list_transform(referenced_works, a -> replace(a, 'https://openalex.org/', ''))
                    AS referenced_works,
            ),
            coalesce(len(authorships), 0) > 3 AS authorships_truncated,
        """)

        out_path.parent.mkdir(parents = True, exist_ok = True)

        db.sql(f"""
            COPY (SELECT * FROM rel)
            TO '{out_path}'
            (FORMAT parquet, COMPRESSION zstd)
        """)

    def _get_shard_validation_result(self, shard_path: Path, content_length, record_count: int):
        """Measure one compact Works shard's stats and package them into a _ShardValidationResult.

        Computes: compact row count, compact file size, count of rows where
        referenced_works_count disagrees with len(referenced_works) (NULL counts as a
        mismatch), and the compact schema's column set vs. self.columns. Does not decide
        pass/fail itself — that's validate_compact_shard's job, working from the result
        this returns.

        Works-specific: the referenced_works/referenced_works_count link-integrity check
        hardcodes those column names, so this isn't reusable as-is for an entity without
        that field — see EntityIngestor's class docstring.

        Concerns:
            No check yet for the file-size-ratio sanity signal described in
            docs/data_pipeline.md's validation methodology (raw vs. compact byte ratio) —
            raw_content_length/compact_content_length are captured here but not compared
            against each other or against the expected ~6-9% ratio.

        Args:
            shard_path: Path to the compact shard being validated.
            content_length: Declared raw file size, from the shard's manifest entry.
            record_count: Declared raw record count, from the shard's manifest entry.

        Returns:
            A _ShardValidationResult.

        Raises:
            Propagates any DuckDB error (e.g. shard_path isn't valid parquet) or OSError
            from os.path.getsize (e.g. shard_path doesn't exist) — no try/except here.
        """

        db = duckdb.connect()

        rel = db.read_parquet(shard_path)
        cols = rel.columns
        compact_record_count = rel.aggregate("count(*) AS total").fetchone()[0]
        compact_content_length = os.path.getsize(shard_path)

        # referenced_works_count vs actual list length - NULL counts as a mismatch
        link_mismatch_count = rel.aggregate(
            "count(*) FILTER (WHERE referenced_works_count IS DISTINCT FROM len(referenced_works)) AS bad"
        ).fetchone()[0]

        # column-set check against the expected schema
        expected_columns = self.columns
        missing_columns   = [c for c in expected_columns if c not in cols]
        redundant_columns = [c for c in cols if c not in expected_columns]

        res = self._ShardValidationResult(
            raw_content_length = content_length,
            compact_content_length = compact_content_length,
            raw_record_count = record_count,
            compact_record_count = compact_record_count,
            link_mismatch_count = link_mismatch_count,
            is_valid_schema = (len(missing_columns) == 0 and len(redundant_columns) == 0),
            missing_columns = missing_columns,
            redundant_columns = redundant_columns,
        )

        return res

    def validate_compact_shard(self, file: EntityIngestor.ManifestData) -> list[str]:
        """Run _get_shard_validation_result for one shard and turn it into pass/fail errors.

        The three checks — record count parity, referenced_works length integrity, schema
        match — are independent; any one failing appends a human-readable message to the
        returned list rather than stopping early, so a single shard's log entry can report
        every problem found, not just the first one hit.

        Concerns:
            Only covers 3 of the 4 checks named in docs/data_pipeline.md's validation
            methodology — the file-size-ratio check isn't included here, so it currently
            can't fail a shard (matches the earlier design decision to treat size ratio as
            an informational signal rather than a blocking one).

        Args:
            file: The ManifestData entry for the shard to validate (its .key locates the
                already-extracted compact file under self.compact_path).

        Returns:
            A list of error message strings — empty if the shard passed every check.
            Callers should treat an empty list as the pass signal (orchestrate() checks
            len(errors) == 0).

        Raises:
            Propagates whatever _get_shard_validation_result raises (DuckDB errors, OSError
            from a missing compact file) — no try/except here.
        """
        val_result = self._get_shard_validation_result(
            self.compact_path/file.key,
            file.content_length,
            file.record_count
        )

        errors = []

        if val_result.raw_record_count != val_result.compact_record_count:
            message = f"""
                Shard {file.key} - Mismatched record_count detected.
                raw_record_count is {val_result.raw_record_count},
                compact_record_count is {val_result.compact_record_count}.
            """
            errors.append(message)

        if val_result.link_mismatch_count != 0:
            message = f"""
                Shard {file.key} - Mismatched referenced_work length detected
                (Count: {val_result.link_mismatch_count}).
            """
            errors.append(message)

        if not val_result.is_valid_schema:
            message = f"""
                Shard {file.key} - Mismatched schema detected.
                missing_columns: {val_result.missing_columns};
                redundant_columns: {val_result.redundant_columns}.
            """
            errors.append(message)

        return errors

    def validate_compact_data(self, manifest_path, compact_path: Path):
        """Full-corpus audit across every compact Works shard currently on disk.

        Unlike _get_shard_validation_result (one shard, called per-shard inline in
        orchestrate), this runs the checks that can only be computed with every shard
        present at once: work_id uniqueness and referenced_works dangling-reference /
        duplicate-link detection, both via DuckDB anti-joins over the whole compact_path
        directory. Also rebuilds compact_manifest.json (a manifest.json-shaped mirror of
        the compact dataset) from scratch each run, rather than maintaining it incrementally.

            compact_path should be the parent folder of all entity folders:
            {compact_path}
                /works
                /authors
                /sources

        This method produces two files:
        - {compact_path}/{self.entity}/integrity_report.txt: validation report across all shards.
        - {compact_path}/{self.entity}/compact_manifest.json: manifest.json mirror for compact shards.

        Concerns:
            - Meaningful only once every shard you intend to keep is actually present —
              run mid-backfill, it reports shards that simply haven't been processed yet as
              though they were corpus gaps.
            - Does not create compact_path/self.entity/ before writing its two output files —
              assumes it already exists from prior extract_compact calls (true in the
              current orchestrate() flow; not guaranteed if called standalone before any
              shard has been extracted).
            - A nonzero dangling-reference count is expected, not necessarily a bug —
              OpenAlex references can legitimately point to merged/deprecated/out-of-scope
              works (see initialization.md's Phase 2 validation goals).

        Args:
            manifest_path: Path to the entity's locally downloaded manifest.json — source
                of truth for which raw shards exist and their declared stats.
            compact_path: Root directory of compact output.

        Returns:
            None. Writes the two files described above as a side effect; also prints a
            summary to stdout.

        Raises:
            Propagates any DuckDB error, or OSError from os.path.getsize / the two output
            file writes (e.g. compact_path/self.entity/ doesn't exist) — no try/except here.
        """

        files = self.get_manifest_data(manifest_path)

        compact_shard_paths = [compact_path/file.key for file in files]

        total_raw_bytes = sum([file.content_length for file in files])

        con = duckdb.connect()

        # __ PASS 1: collect all ids (needed before any reference check) _____________

        compact_manifest = {
            "format": "parquet",
            "entity": self.entity,
            "record_count": 0,
            "content_length": 0,
            "files": []
        }
        total_bytes = 0
        for path in compact_shard_paths:
            p = str(path)

            # Getting record count & size per shard
            record_count = con.execute(
                "SELECT count(*) FROM read_parquet(?)", [p]
            ).fetchone()[0]
            size = os.path.getsize(p)

            compact_manifest["files"].append({
                "key": str(Path(p).relative_to(compact_path)),
                "meta": {
                    "content_length": size,
                    "record_count": record_count
                }
            })
            compact_manifest["record_count"] += record_count
            compact_manifest["content_length"] += size
            total_bytes += size

        shard_paths = [str(p) for p in compact_shard_paths]

        # __ CHECK 1: Work ID uniqueness — list of (id, occurrences) __________________
        logger.info("Validating Work ID uniqueness...")
        duplicate_ids = con.sql("""
            SELECT id, count(*) AS n
            FROM read_parquet(?)
            GROUP BY id
            HAVING count(*) > 1
        """, params=[shard_paths]).fetchall()

        # __ CHECK 2a: dangling references — list of (work_id, referenced_work_id) ___
        logger.info("Computing dangling references...")
        dangling_refs = con.sql(f"""
            SELECT w.id AS work_id, w.ref AS referenced_work_id
            FROM (
                SELECT id, unnest(referenced_works) AS ref
                FROM read_parquet(?)
            ) w
            ANTI JOIN read_parquet(?) a ON a.id = w.ref
        """, params=[shard_paths, shard_paths]).fetchall()

        # __ CHECK 2b: duplicate links within a record ________________________________
        # Two-phase: first find candidate works with an internal duplicate via a cheap
        # length comparison (no unnest, full corpus scan but no row explosion), then
        # unnest only those candidates for per-reference detail. Unnesting all ~2.37B
        # referenced_works entries corpus-wide up front OOMs (verified); this doesn't,
        # since only works that already have a known duplicate ever get unnested.
        logger.info("Checking for duplicate links...")
        dup_link_detail = con.sql("""
            WITH candidates AS (
                SELECT id, referenced_works
                FROM read_parquet(?)
                WHERE len(referenced_works) <> len(list_distinct(referenced_works))
            )
            SELECT id AS work_id, ref AS referenced_work_id, count(*) AS occurrences
            FROM (
                SELECT id, unnest(referenced_works) AS ref
                FROM candidates
            )
            GROUP BY id, ref
            HAVING count(*) > 1
            ORDER BY work_id, occurrences DESC
        """, params=[shard_paths]).fetchall()

        # Derived from dup_link_detail, not a second corpus scan.
        dup_link_works = sorted({work_id for work_id, _, _ in dup_link_detail})

        # __ REPORT _________________________________________________________________
        print(f"Shards:               {len(compact_shard_paths)}")
        print(f"Total raw size:       {total_raw_bytes:,} bytes ({total_raw_bytes/1e9:.2f} GB)")
        print(f"Total size:           {total_bytes:,} bytes ({total_bytes/1e9:.2f} GB)")
        print(f"Size reduction:       {float(total_bytes)/total_raw_bytes*100:.2f}% of raw size")
        print(f"Duplicate work ids:   {len(duplicate_ids)}")
        print(f"Dangling references:  {len(dangling_refs)}")
        print(f"Works w/ dup links:   {len(dup_link_works)}")

        with open(compact_path/self.entity/"integrity_report.txt", "w", encoding="utf-8") as f:
            f.write(f"Shards: {len(compact_shard_paths)}\n")
            f.write(f"Total raw size: {total_raw_bytes:,} bytes ({total_raw_bytes/1e9:.2f} GB)")
            f.write(f"Total size: {total_bytes:,} bytes ({total_bytes/1e9:.2f} GB)\n\n")
            f.write(f"Size reduction: {float(total_bytes)/total_raw_bytes*100:.2f}% of raw size")

            f.write(f"Duplicate work ids ({len(duplicate_ids)}):\n")
            for wid, n in duplicate_ids:
                f.write(f"  {wid}\t{n}\n")

            f.write(f"\nDangling references ({len(dangling_refs)}):\n")
            for work_id, ref_id in dangling_refs:
                f.write(f"  {work_id} -> {ref_id}\n")

            f.write(f"\nWorks with duplicate links ({len(dup_link_works)}):\n")
            for wid in dup_link_works:
                f.write(f"  {wid}\n")

            f.write(f"\nDuplicate link detail ({len(dup_link_detail)}):\n")
            for work_id, ref_id, n in dup_link_detail:
                f.write(f"  {work_id} -> {ref_id} (x{n})\n")

        with open(compact_path/self.entity/"compact_manifest.json", "w") as f:
            json.dump(compact_manifest, f)


def main():
    logger.info("Started creating index for db")
    entity = WorksIngestor.entity
    con = duckdb.connect(str(Path('/data')/(entity + ".db")))
    con.sql(f"""
        CREATE INDEX idx_id ON {entity} (id)
    """)
    logger.info("Finished creating index for db")
    con.close()

if __name__ == "__main__":
    main()
