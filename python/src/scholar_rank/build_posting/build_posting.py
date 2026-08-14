"""Orchestrate build posting process.

"""

from pathlib import Path
from scholar_rank import get_logger, Tokenizer, scholar_rank_cpp


class PostingBuildler:
    # Hyper parameters
    MEM_LIMIT = (1<<30)
    K1 = 1.2
    B = 0.75
    BLOCK_SIZE = 128
    SPLIT_SIZE = (1<<30)
    

    @staticmethod
    def get_token_stream_file_name(idx: int):
        return f"token_{idx:04d}.bin"

    def __init__(
        self,
        corpus_path: Path,
        out_path: Path,
        spill_path: Path,
        lookup_folder: str,
        lookup_file_name: str,
        token_stream_folder: str,
        posting_folder: str,
        partial_folder: str
    ) -> None:
        self.corpus_path = corpus_path
        self.out_path = out_path
        self.spill_path = spill_path

        self.lookup_folder = lookup_folder
        self.lookup_file_name = lookup_file_name
        self.token_stream_folder = token_stream_folder
        self.posting_folder = posting_folder
        self.partial_folder = partial_folder

        self.lookup_path = self.out_path / self.lookup_folder
        self.token_stream_path = self.out_path / self.token_stream_folder
        self.posting_path = self.out_path / self.posting_folder
        self.partial_path = self.out_path / self.partial_folder


    def build(self) -> None:
        tokenizer = Tokenizer()
        tokenizer.get_token(
            self.corpus_path,
            self.token_stream_path,
            self.lookup_path,
            self.get_token_stream_file_name,
            self.lookup_file_name,
            self.spill_path,
        )

        scholar_rank_cpp.build_doc_len(self.token_stream_path, self.posting_path)
        scholar_rank_cpp.build_inverted_blocks(
            self.token_stream_path,
            self.partial_path,
            self.MEM_LIMIT
        )
        scholar_rank_cpp.merge_inverted_blocks(
            self.posting_path / scholar_rank_cpp.file_names.DOC_LEN_LIST,
            self.partial_path,
            self.posting_path,
            self.K1,
            self.B,
            self.BLOCK_SIZE,
            self.SPLIT_SIZE
        )