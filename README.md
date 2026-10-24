# lumos

A Markdown editor library in C++20, with a Qt 6 editor widget.

lumos keeps the Markdown source as the only truth: every edit is a text
edit, and the syntax tree follows it. The parser is incremental: after an
edit it parses only the changed blocks again, and the result is the same
tree as a full parse.

## Features

- Own CommonMark 0.31.2 and GitHub Flavored Markdown parser. All 703
  examples of the CommonMark spec, the GFM spec extensions and the GFM
  extension tests pass. The HTML output is byte-equal to cmark and
  cmark-gfm.
- Edit commands as pure functions (document and selection in, edits
  out): lists and quotes on Enter, list indent and outdent, bold, italic,
  strikethrough and code toggles, links, headings, block quotes, code
  blocks, GFM tables with Word-like Tab and Enter keys, automatic
  renumbering of ordered lists.
- Find and replace: literal or regex, Unicode case folding, whole words,
  replace all as one undo step.
- Document tools: counts, link targets, diagnostics.
- HTML renderer with host hooks for URLs and nodes.
- Qt 6 widget `SourceEditor`: source mode with syntax highlighting, own
  text layout, IME, bidi text, drag and drop, find marks, line numbers,
  a centred text column.
- No toolkit types in the core library. Strings are UTF-8, positions are
  byte offsets.
- The core logs to a sink that the host sets, one `key=value` line per
  event. Log lines never contain document text.

## Build

See [docs/BUILD.md](docs/BUILD.md). In short:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The editor app is `build/app/lumos-md [file.md]`. It logs to
`~/.local/state/lumos-md/lumos-md.log`. For debug lines, add
`debug=true` under `[log]` in `~/.config/lumos-md/lumos-md.ini`.

## Use

See [docs/API.md](docs/API.md). The public headers in `include/lumos/`
and `qt/include/lumos/qt/` describe every function.

## License

GNU Affero General Public License v3.0 (AGPL-3.0-only); see
[LICENSE](LICENSE). Third-party data: [THIRD_PARTY.md](THIRD_PARTY.md).

A commercial license for use in closed-source products is available.
Open an issue titled "Commercial license" and you will get a private
contact.
