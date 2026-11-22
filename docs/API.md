# lumos API

This page describes the API. The headers are in `include/lumos/` (core)
and `qt/include/lumos/qt/` (Qt widget).

## Layers

| Layer | CMake target | Job |
|---|---|---|
| core | `lumos::core` | text buffer, parser, syntax tree, document, edit commands, find, tools, HTML renderer, log |
| Qt adapter | `lumos::qt` | the `SourceEditor` widget: layout, painting, input, IME, clipboard, selection, undo groups, editing keys, styles |

Rules of the core:

- No toolkit types. Strings are UTF-8; positions and lengths are byte
  offsets.
- Core functions take and return plain values.
- Edit rules are in the core. An adapter maps keys to commands and never
  copies a rule.

## Add lumos to a CMake project

```cmake
add_subdirectory(lumos)
target_link_libraries(my_app PRIVATE lumos::qt)   # or lumos::core
```

`lumos::core` gives C++20 to its users. lumos sets no global flags. When
lumos is not the top-level project, its tests, benchmark and demo are
off; `LUMOS_BUILD_QT` (default on) builds the Qt adapter.

## Basic types (`lumos/types.hpp`)

- `Edit`: `offset`, `removed` bytes, `inserted` text.
- `ByteRange`: `[start, end)`.
- `TextRegion`: `[start, old_end)` in the old text became
  `[start, new_end)` in the new text.

## Document (`lumos/document.hpp`)

`Document` owns the text, the parser and the undo history.

- `load(text)`, `text()`, `size()`.
- `apply(edit)` applies one edit; `begin()` and `commit()` group edits
  into one undo step.
- `undo()`, `redo()`, `can_undo()`, `can_redo()`, `set_max_undo(steps)`.
- `mark_saved()`, `modified()`; `mark_unsaved()` (the file is gone:
  `modified()` stays true until the next `load()` or `mark_saved()`).
- `add_listener(listener)` gives an id for `remove_listener(id)`. A
  listener is called after each change with a `Change` (kind, edits,
  changed tree regions, version).
- `wipe()`: overwrites the text that lumos owns with zeros (buffer, undo
  steps, parser copies); the document is then empty, with no history.

## Parser and syntax tree (`lumos/parser.hpp`, `lumos/syntax.hpp`)

- `make_parser(dialect)` gives the CommonMark and GFM parser.
  `Dialect` switches the GFM parts: `strikethrough`, `task_lists`,
  `tables`, `extended_autolinks`, `front_matter`.
  `Dialect::commonmark()` turns them all off.
- `parse(text)` parses the whole text; `apply_edit(region, text)` follows
  a change and gives the same tree as a full parse.
- `changes()` lists the tree regions the last call replaced.
- `SyntaxTree` holds the nodes in document order with their byte ranges,
  kinds and parents.

A `Document` runs the parser itself; a host reads the tree from it.

## Edit commands (`lumos/commands.hpp`)

A command takes the document and a `Selection` (anchor, cursor) and
returns `std::optional<CommandResult>`: the edits and the new selection.
`std::nullopt` means the command does not apply there, and the host does
its default action for the key. No command changes the document; the
host applies the edits as one undo step.

- Keys: `insert_newline` (Enter), `insert_line_break` (Shift+Enter),
  `indent_list`, `outdent_list` (Tab, Shift+Tab),
  `delete_markup_backward` (Backspace after a marker).
- Tables: `next_cell`, `previous_cell`, `next_row`, `previous_row`,
  `format_table`, `insert_table`. Each writes the whole table aligned.
- Inline: `toggle_inline` (strong, emphasis, strikethrough, code),
  `insert_link`, `insert_image`, `remove_link`.
- Blocks: `set_heading`, `toggle_block` (bullet list, ordered list, task
  list, block quote, code block), `insert_thematic_break`.
- Ordered lists: `list_shape` before an edit and `renumber_lists` after
  it keep the numbers in sequence; `renumber`, `marker_to_text`,
  `out_of_sequence`.
- `format_at`: the format at the selection, for toolbar button states.
- `drop_text`: moves or copies text for drag and drop.

## Find and replace (`lumos/commands.hpp`)

- `find_all(doc, pattern, options)`: a `FindResult`, the matches as
  byte ranges (`hits`) and a `FindStatus`: `ok`, `invalid_pattern` (the
  regex does not compile) or `limit_hit` (the regex hit a PCRE2 limit:
  a valid pattern that is too costly). `hits` is empty unless `ok`.
- `replace_one`: a command result. `replace_all`: a `ReplaceAllResult`,
  the same status and the command (none on a failure or when nothing
  matches); replace all is one undo step.
- `FindOptions`: `case_sensitive`, `whole_word`, `regex`.
- Literal case-insensitive search uses the full Unicode case fold. Regex
  is PCRE2 syntax in UTF mode; its case folding is Unicode simple case
  folding. Each line is its own regex subject: `^` and `$` match at
  each line, and no match crosses a line break. An empty match right
  after a match is left out (`a*` on `baaaac` gives 3 hits). A pattern
  that is not valid, or one that hits the PCRE2 match
  or heap limit, gives std::nullopt. A long match is safe on a thread
  with a small stack.
- The functions keep no state. The host keeps the match list and the
  current match.

## Document tools (`lumos/tools.hpp`)

- `properties(document, options)`: byte, character, word and line
  counts, counts of headings, links, tables and tasks, link targets and
  diagnostics.
- `link_at(document, offset)`: the innermost link, image or autolink at
  a byte offset, as a link target (url, form, node range), or none.
- `url_form(url)`: the class of a link target (relative path, absolute
  path, file URL, URL with a scheme, other).

## HTML (`lumos/md/html_renderer.hpp`)

- `render_html(tree, text, options)`. Without hooks the output is
  byte-equal to cmark and cmark-gfm.
- Safe by default (`HtmlOptions::unsafe = false`, as cmark): raw HTML
  becomes `<!-- raw HTML omitted -->`, and `javascript:`, `vbscript:`,
  `file:` and non-image `data:` URLs become empty.
  `tagfilter` is the GFM tag filter; `hard_breaks` writes soft line
  breaks as `<br />`.
- `HtmlOptions::url_hook` gets each link, autolink and image URL and
  returns the URL to write, or `std::nullopt` to drop the tag and keep
  its content. `is_dangerous_url(url)` is the safe mode rule, for a hook
  that wants it for some URLs.
- `node_hook` is called for each node: it can write the node's tags
  (`replace_tags`) or the whole node (`replace_all`), for example task
  items with the host's own check boxes.
- `output_cap` (16 MiB by default, 0 for none): over the cap the call
  writes nothing. `render_html(tree, text, options, out)` then returns
  false and leaves `out` as it was; the form that returns a string
  gives an empty string. A short note with many reference links to one
  long definition can expand to gigabytes of HTML.
- `source_anchors` writes an empty `<a name="lumos-N"></a>` at the
  start of each paragraph, heading, code block and table row, where N
  is the node's start byte offset. A view finds the place of a source
  offset in the HTML with them.

## Log (`lumos/log.hpp`)

- `set_log_sink(sink)`: the host gets one line per event, for example
  `ts=... level=debug comp=parser event=parse_end nodes=812 lines=240 us=310`.
  With no sink, the core writes nothing.
- `set_log_level(level)`: default `info`.
- Log lines never contain document text, at any level: only sizes,
  offsets, counts and fixed words.

## Qt widget (`lumos/qt/source_editor.hpp`)

`lumos::qt::SourceEditor` is a `QAbstractScrollArea`. One `QTextLayout`
per source line; Qt does shaping, bidi and IME. Long lines wrap at word
boundaries. All positions are UTF-8 byte offsets, as in the core.

- Create: `SourceEditor(parent)` or `SourceEditor(dialect, parent)`.
- Text:
  - `set_text(text, keep_position = false)` replaces the text, clears
    the undo history and counts as saved. It emits no `textChanged`.
    `keep_position` (a reload of the same file) keeps the cursor, the
    selection and the scroll position as far as the new text allows.
  - `replace_text(text)`: the whole text as one undo step; only the
    changed middle is edited, so the cursor stays.
  - `insert_text(text)`: replaces the selection; one undo step.
  - `text()` (a copy, for saving), `length()`.
  - `is_modified()`, `mark_saved()` (keeps the undo history),
    `mark_unsaved()` (the file is gone).
  - `wipe()`: `Document::wipe()` for the editor; the editor is then
    empty. Qt's own copies (layouts, clipboard) are not wiped.
- Line endings: `text()` gives the exact bytes. Enter and paste write
  the line ending of the text (CRLF if its first line ends with CRLF,
  else LF); `newline()` gives it, for `insert_table`.
- Selection: `cursor_position`, `anchor_position`, `has_selection`,
  `selection`, `selected_text`, `set_cursor_position(pos,
  keep_anchor)`, `select(anchor, cursor)`.
- Read-only: `set_read_only`, `is_read_only`. Keys, input method, cut,
  paste, undo and redo do not change the text; moving, selecting and
  copy still work, and so do the API calls.
- Commands: `document()` gives the `Document` for the core commands;
  `apply_command(result)` applies a result as one undo step and sets
  the selection (false if there is no result, or read-only). Ordered
  lists renumber in the same step, as after every edit:

  ```cpp
  editor->apply_command(lumos::toggle_inline(editor->document(),
      editor->selection(), lumos::InlineKind::strong));
  ```

- Undo: use the editor's `undo()` and `redo()`, not the document's:
  they close the open typing group first. `canUndo()`, `canRedo()`.
- Marks: `set_marks(layer, ranges, format)`, `marks(layer)`. Formats on
  byte ranges, in layers; a higher layer draws over a lower one. Layer 2
  is find hits, layer 3 the current find hit. Marks move with edits; an
  edit that touches a mark removes it; `set_text` removes all.
- Look: `set_style_table(table)` (`style_table.hpp`), the font through
  `QWidget::setFont` (also for zoom), `set_max_text_width(chars)` (default 100),
  `set_page_centred(on)` (default on; off puts the page and its line
  numbers at the left page margin), `set_line_numbers(on)` (default on). Plain text, the selection and the
  caret take their colours from the widget palette; `StyleTable` has a
  format per node kind and heading level, the code line background,
  the find hit formats, the page and outside brushes, `page_padding`,
  `page_margin` and the line number colours.
  `StyleTable::make_default(font, palette)` gives a theme that follows
  the palette. Set the table again after a theme change.
- Images: `set_image_max_height(px)` (0, the default, turns them off)
  shows each Markdown image below its source line, at most the text
  width and `px` high, never scaled up. The widget asks for each url
  once with `imageRequested(url)`; the host answers with
  `set_image(url, image)` or `set_image_failed(url)`, at any time.
  Until the answer a row with the alt text stands in its place; a
  failed image keeps that row (`StyleTable::image_pending`,
  `image_failed`). `set_text` clears all answers and asks again;
  `reload_images()` does the same for a note that moved.
- Positions on screen: `offset_at(point)` (for a context menu),
  `cursor_rect()`, both in viewport coordinates.
- Slots: `undo`, `redo`, `cut`, `copy`, `paste`, `selectAll`.
- Signals: `textChanged`, `modificationChanged`, `cursorPositionChanged`,
  `canUndoChanged`, `canRedoChanged`, `linkActivated(target)`,
  `imageRequested(url)`. No state depends on them, so a
  host can block them.

## What the widget does and what the host does

The widget:

- Handles the editing keys: arrows, Home, End (also with Ctrl), Page
  Up, Page Down, Tab, Shift+Tab, Enter, Backspace, Delete and text
  without Ctrl. The Markdown keys are the core commands: Enter continues
  lists and quotes, Shift+Enter is a line break in the same block, Tab
  and Shift+Tab indent and outdent list items and move between table
  cells, Backspace after a marker removes it.
- All other Ctrl keys reach the window first. The widget handles
  Ctrl+Z, Ctrl+C and so on only when the window has no shortcut for
  them.
- Drops take only `text/plain` and `text/uri-list`.
- Ctrl+click on a link, image or autolink emits `linkActivated` with its
  target (`link_at`); the caret does not move. Elsewhere Ctrl+click is
  a plain click.
- Primary selection (X11, Wayland): a selection made by mouse or keys
  goes to the primary selection; a middle click inserts it at the click
  point. Not on a platform without one; the clipboard is not touched.
- Many widgets can be open at once; they share no state.

The host:

- Menus, toolbars and their shortcuts; it calls the core commands and
  `apply_command`, and uses `format_at` for button states.
- The context menu: the widget has none. With `Qt::CustomContextMenu`
  it emits `customContextMenuRequested` (viewport coordinates).
- The find bar: it calls `find_all` and shows the hits with
  `set_marks`.
- Themes: the palette, the font and the `StyleTable`.
- Files, tabs, saving and reloading (`set_text` with `keep_position`).
- No drops: `setAcceptDrops(false)`.
- Loading images: the slot for `imageRequested` reads the file (best
  on a worker thread, scaled down to the max height) and calls
  `set_image` or `set_image_failed`. The widget never reads files.
- Opening a link: the slot for `linkActivated` resolves the url (for
  example a relative path against the note's directory) and opens it.
- HTML views and export: `render_html` with its hooks.

`app/` (the lumos-md editor) is built on this API only; it is the
reference for a host.
