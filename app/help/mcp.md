# MCP server

An AI agent can work with lumos-md through the Model Context Protocol
(MCP). The agent can:

- list the files open in lumos-md;
- read an open file with your unsaved changes, so it changes the text
  you see, not an old copy on disk;
- show you a file or a line in it: lumos-md opens the file in a tab and
  raises the window.

The agent never writes through lumos-md. It writes the file with its own
tools. lumos-md then sees the change on disk: an unchanged tab loads it
as one undo step, a tab with unsaved changes asks you first and can show
both texts side by side.

## Turn it on

1. In lumos-md: Tools, Enable MCP Server. It is off by default. While it
   is on, any program that runs as your user can read the open files,
   also text that is not saved.
2. Tell your agent about lumos-md: copy the text below into its prompt.
   The agent adds the server to its own settings, or tells you the
   steps for your setup.

```text
lumos-md is my Markdown editor, and it has an MCP server for AI agents.
Please add it to your MCP settings: a stdio server named "lumos-md" that
runs the command `lumos-md --mcp`. If you cannot change your settings
yourself, tell me the exact steps for the program you run in, and
whether it needs a restart. If you run in a sandbox with its own
XDG_RUNTIME_DIR (bwrap, Flatpak, a container), you cannot see the
default socket: then tell me to pick a folder that we both can see in
lumos-md, Tools, MCP Socket Folder, and run `lumos-md --mcp --mcp-dir
<that folder>` instead.

How to use it: before you change a file, call read_open_file with its
absolute path. If the file is open, it may have unsaved edits: make your
change on that text, then write the whole file. If it is not open, edit
it on disk as usual. To show me a file or a line in it, call show_file.
```

## Settings for an agent

Most agents read MCP servers from a JSON file in this form:

```json
{
  "mcpServers": {
    "lumos-md": {
      "command": "lumos-md",
      "args": ["--mcp"]
    }
  }
}
```

With a socket folder, the args are `["--mcp", "--mcp-dir", "/path/to/folder"]`.

## Tools

- `list_open_files`: the open tabs in tab order, each with `path`
  (`null` for an Untitled tab), `name`, `modified` (unsaved changes) and
  `current` (the tab in front).
- `read_open_file`, argument `path` (absolute): the text as it is open
  in lumos-md, with unsaved changes.
- `show_file`, arguments `path` (absolute) and optional `line` (from 1):
  opens the file in a tab or picks its open tab, makes it current and
  raises the window. With `line`, the cursor goes to that line. The
  keyboard focus does not move, and the file does not change. On
  Wayland the desktop may only mark the window as urgent.

## Try it by hand

`lumos-md --mcp` reads one JSON-RPC message per line on standard input
and writes one answer per line. Each line below is one message:

```sh
lumos-md --mcp <<'EOF'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_open_files","arguments":{}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"show_file","arguments":{"path":"/home/me/notes/todo.md","line":12}}}
EOF
```

The answer to the list call has the tabs as JSON text:

```json
{"id":2,"jsonrpc":"2.0","result":{"content":[{"text":"[{\"current\":true,\"modified\":true,\"name\":\"todo.md\",\"path\":\"/home/me/notes/todo.md\"}]","type":"text"}],"isError":false}}
```

When lumos-md is not running or the server is off, each tool call
answers with `"isError":true` and the text "lumos-md is not running, or
Tools, Enable MCP Server is off."
