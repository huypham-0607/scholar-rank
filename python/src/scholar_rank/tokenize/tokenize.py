"""Tokenize document, generate a posting list.

"""

import re
import duckdb as db

from pathlib import Path

class Tokenizer:

    def __init__(
        self,
        db_path: Path,

    ):
        self.DB_PATH = db_path

    def get_doc(self):
        con = db.connect()

        rel = con.sql(f"""
            INSTALL fts;
            LOAD fts;
            SELECT
                regexp_replace(id, 'W', '')::BIGINT AS id,
                list_transform(
                    regexp_extract_all(
                        lower(concat_ws(' ',
                            coalesce(title, ''),
                            list_aggregate(list_transform(topics, t -> t.display_name), 'string_agg', ' '),
                            list_aggregate(list_transform(topics, t -> t.subfield_display_name), 'string_agg', ' '),
                            list_aggregate(list_transform(topics, t -> t.field_display_name), 'string_agg', ' '),
                            list_aggregate(list_transform(topics, t -> t.domain_display_name), 'string_agg', ' '),
                            list_aggregate(list_transform(keywords, k -> k.display_name), 'string_agg', ' ')
                        )),
                        '[a-z0-9]+'
                    ), 
                    token -> stem(token,'english')
                ) AS tokens
            FROM read_parquet('{self.DB_PATH}/**/*.parquet')
        """)

        dictionary = con.sql(f"""
            WITH t AS (
                SELECT unnest(tokens) AS token FROM rel
            )
            SELECT token, count(token) AS freq FROM t
            GROUP BY token
            ORDER BY freq desc  
        """)

        print(rel.fetchone())
        print(con.sql(f"""
            SELECT count(*) as distinct_token_count FROM dictionary
        """).fetchone())
        for i in range(0,10):
            print(dictionary.fetchone())





def main():
    DB_PATH = Path('/data')
    
    label = 'math_english'

    tokenizer = Tokenizer(DB_PATH/label)
    tokenizer.get_doc()

if __name__ == "__main__":
    main()