# Spec test files

Test data for the Markdown parser. The files are copies; do not edit
them. To update one, copy the new file from its source and change the commit
below.

| File | Source | Commit | License |
|---|---|---|---|
| `commonmark-0.31.2.txt` | `spec.txt` of https://github.com/commonmark/commonmark-spec (CommonMark Spec 0.31.2, 2024-01-28) | `8a5f7646e9f67c3004f023d262b3e6b596afc2ed` | CC BY-SA 4.0 |
| `gfm-0.29.txt` | `test/spec.txt` of https://github.com/github/cmark-gfm (GitHub Flavored Markdown Spec 0.29-gfm, 2019-04-06) | `27d942c8b0a62d192f616e5bf3578f4b6a89e180` | CC BY-SA 4.0 |
| `extensions.txt` | `test/extensions.txt` of https://github.com/github/cmark-gfm ("Extensions test", 2016-08-31) | `27d942c8b0a62d192f616e5bf3578f4b6a89e180` | CC BY-SA 4.0 |

License: each file names the Creative Commons Attribution-ShareAlike 4.0
license in its header (https://creativecommons.org/licenses/by-sa/4.0/). The
CommonMark spec is Copyright (C) 2014-16 John MacFarlane. The share-alike
terms apply to these files and to changed copies of them, not to the code
that reads them.

## Examples

| File | Examples | Used with |
|---|---|---|
| `commonmark-0.31.2.txt` | 655 | CommonMark only (no GFM extensions) |
| `gfm-0.29.txt` | 672: 648 core, 11 `autolink`, 8 `table`, 2 `strikethrough`, 1 `tagfilter`, 2 `disabled` | only the 22 examples with an extension tag; `disabled` is skipped; the core examples are CommonMark 0.29 and are covered by the 0.31.2 file |
| `extensions.txt` | 30 | all GFM extensions; the three footnote sections are skipped (footnotes are not planned yet), and so is the example with the output `<IGNORE>` (it only checks for a crash): 26 run |

Format: an example starts with a line of 32 backticks and ` example`
(plus extension names in the GFM file), then the Markdown, a line with
`.`, the expected HTML, and a line of 32 backticks. `→` stands for a tab.
The runner (`lumos_spec`, tests/test_spec.cpp) compares the HTML byte for
byte, as cmark does (`spec_tests.py --no-normalize`), with raw HTML allowed
(cmark `--unsafe`).

`expected_failures.txt` is not a copy: it lists the examples that may fail.
