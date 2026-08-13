"""Orchestrate build posting process.

"""

from pathlib import Path
from scholar_rank import get_logger, Tokenizer, scholar_rank_cpp


class PostingBuildler:
    # File name / Folder name conventions
    LOOKUP_FOLDER = "lookup"
    LOOKUP_FILE_NAME = "doc_id_lookup.bin"
    TOKEN_STREAM_FOLDER = "token_stream"
    POSTING_FOLDER = "posting"
    PARTIAL_FOLDER = "posting/partial"

    @staticmethod
    def get_token_stream_file_name(idx: int):
        return f"token_{idx:04d}.bin"

    def __init__(
        self,
        corpus_path: Path,
        out_path: Path,
    ) -> None:
        self.corpus_path = corpus_path
        self.out_path = out_path

        self.lookup_path = self.out_path / self.LOOKUP_FOLDER
        self.token_stream_path = self.out_path / self.TOKEN_STREAM_FOLDER
        self.posting_path = self.out_path / self.POSTING_FOLDER
        self.partial_path = self.out_path / self.PARTIAL_FOLDER


    def build(self) -> None:
        tokenizer = Tokenizer(
            self.corpus_path,
            self.token_stream_path,
            self.lookup_path,
            self.get_token_stream_file_name,
            self.LOOKUP_FILE_NAME,
        )
        tokenizer.get_token()