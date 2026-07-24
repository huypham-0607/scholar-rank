# Notebook

## Overview

This folder contains relevant notebooks for exploratory data analysis. Typically contains functions and reports to verify corpus-related facts.

## List of notebooks



| Notebook                      | Last run      | Desc                                  |
| -------------                 | ------------- | ------------------------------------- |
| `null_rate_analysis.ipynb`    | 7/17/2026     | NULL rate for compact corpus per column. Output result to `null_analysis.txt` |
| `keyword_field_fix.ipynb`     | 7/19/2026     | In prior version of `fetch_data.py`, `keywords` field retains prefix `'https://openalex.org/'`. This notebook striped the prefix corpus-wide, avoiding refetch with the patched `fetch_data.py` |
