# lumos-md features

lumos-md edits Markdown as source text: the format characters (`**`,
`#`, `-`) stay visible, with syntax colours. Every command is a text
edit, and one Undo takes it back.

## Files and tabs

- Files open in tabs of one window. A second start of lumos-md opens
  its files in the running window.
- The tabs of the last session open at the next start.
- File -> Reopen closed tab (Ctrl+Shift+T) opens the last closed files
  again.
- File -> Open Recent lists the last files.
- The status bar shows the full path of the current tab. A right click
  on a tab offers Copy Path.
- A few seconds after an edit, the unsaved text goes to a draft file.
  After a crash, the next start offers to recover it.
- When an open file changes on disk, lumos-md loads it again as one
  undo step. A tab with unsaved changes asks first, and Show diff shows
  both texts side by side to merge them.
- A file without write access opens for edits; Save then opens Save As.
- Edit -> Read-only stops all edits in the current tab.
- File -> Properties counts bytes, characters, words, lines, headings,
  links, pictures, tables and tasks.

## Editing

- Enter in a list or a quote starts the next item or quote line. Enter
  on an empty item ends the list.
- Tab and Shift+Tab move list items in and out.
- Ordered lists renumber after each edit; only the first number is
  yours to choose.
- Format: bold (Ctrl+B), italic (Ctrl+I), strikethrough (Ctrl+Shift+X),
  code (Ctrl+E), headings 1 to 6 (Ctrl+1 to Ctrl+6, paragraph
  Ctrl+Shift+N), bullet list (Ctrl+Shift+8), ordered list
  (Ctrl+Shift+7), task list (Ctrl+Shift+9), quote (Ctrl+Shift+Q). Each
  command turns the format on or off.
- Insert: link (Ctrl+K), picture, horizontal line, code block, table.
- Tables: Tab and Shift+Tab go to the next or previous cell, Enter to
  the next row. The last cell gets a new row. Table -> Format table lines
  up the columns.
- Right click, Make it text: Markdown characters in the selection stay
  as plain text.
- Edit -> Paste from HTML (Ctrl+Alt+V) pastes a copy from a web page as
  Markdown.
- Drag and drop of text inside the editor and from or to other apps.
  Middle click pastes the selected text (Linux).
- Input methods (IME) and right-to-left text.

## Find and replace

- Edit -> Find (Ctrl+F) and Replace (Ctrl+H): plain text or a regular
  expression, case-sensitive or not, whole words.
- Replace All is one undo step.

## View

- View -> Show Preview shows the current tab as HTML next to the editor.
  It follows the text and the scroll position. A click on a link opens
  it.
- Pictures show below their Markdown image line.
- Ctrl+click on a link opens it.
- Themes (Default, Light grey, Gruvbox, Dark, Dark blue-grey, Dark
  slate), Paper look (light themes only), line numbers, a limited text
  width, font and zoom (Ctrl+Plus, Ctrl+Minus, Ctrl+0).

## AI agents

With Tools -> Enable MCP Server on, an AI agent can list and read the open
files with their unsaved changes, and show you a file or a line in it.
It gets only files in the folders of Tools -> MCP Allowed Folders, and it
shows only `*.md`, `*.markdown` and `*.txt` files. See Help -> MCP Server.

## Not supported

- Wiki links (`[[note]]`), callouts (`> [!note]`), footnotes and math
  (`$x$`) show as plain text.
- Raw HTML in a note does not show in the preview, and pictures load
  only from local files.
- Rich mode (formatted text without the Markdown characters) is not
  there yet; lumos-md always shows the source.
- Overwrite mode (the Insert key).
- Export to HTML or PDF.
- Windows and macOS builds.

## Limitations

- A wide character (Chinese, Japanese, Korean, or an emoji) in a table
  takes two columns, so Format table does not line up its column.
- A format command does nothing where its result would not be valid
  Markdown, for example bold on `"q"` inside `a"q"b`.
- Bold or italic on part of a bold text removes the bold from all of
  it; it does not split it.
- A new list always uses `-`; a new code block gets no language name.
  The format commands do not check or uncheck a task box.
- A tab character after a list marker counts as one column.
- After a double-click or a triple-click, a drag selects by characters,
  not by words or lines.
- The cursor moves in logical order, not in screen order, in mixed
  left-to-right and right-to-left text.
- Bytes that are not UTF-8 show as `�`; an edit next to such a byte
  does not apply.
- Notes of many megabytes work, but each edit gets slower: lumos-md is
  made for notes, not for large data files.
