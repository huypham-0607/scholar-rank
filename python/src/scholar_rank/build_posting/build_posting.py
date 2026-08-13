"""Orchestrate build posting process.

"""

from pathlib import Path
from scholar_rank.utils import get_logger
from scholar_rank.tokenizer.tokenizer import Tokenizer







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

        self.lookup_path = self.out_path / LOOKUP_FOLDER
        self.token_stream_path = self.out_path / TOKEN_STREAM_FOLDER
        self.posting_path = self.out_path / POSTING_FOLDER
        self.partial_path = self.out_path / PARTIAL_FOLDER


    def build(self) -> None:
        tokenizer = Tokenizer(
            self.corpus_path,
            self.token_stream_path,
            self.lookup_path,
            get_token_stream_file_name,
            LOOKUP_FILE_NAME,
        )
        tokenizer.build_doc_id_lookup()
        tokenizer.get_token()




    
    