#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vector>

#include "mcp_bridge.hpp"

namespace {

struct FakeWindow {
  std::vector<QJsonObject> calls;
  std::optional<QJsonObject> answer;

  AskWindow ask() {
    return [this](const QJsonObject& request) {
      calls.push_back(request);
      return answer;
    };
  }
};

QJsonObject result_of(const QByteArray& line) { return QJsonDocument::fromJson(line).object().value("result").toObject(); }

QString content_text(const QJsonObject& result) {
  return result.value("content").toArray().at(0).toObject().value("text").toString();
}

QByteArray call_read_open_file(const char* path_json) {
  return QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read_open_file",)"
                     R"("arguments":{"path":)") +
         path_json + "}}}";
}

QByteArray call_show_file(const char* path_json) {
  return QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"show_file",)"
                     R"("arguments":{"path":)") +
         path_json + "}}}";
}

QByteArray call_show_file_line(const char* path_json, const char* line_json) {
  return QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"show_file",)"
                     R"("arguments":{"path":)") +
         path_json + R"(,"line":)" + line_json + "}}}";
}

const char kInstructions[] =
    "lumos-md is the user's Markdown editor. Before you change a file, call read_open_file with its absolute path. "
    "If the file is open, it may have unsaved edits: make your change on the text that read_open_file gives, then "
    "write the whole file. lumos-md shows the change to the user, who accepts or picks parts of it. If the file is "
    "not open, edit it on disk as usual. To show the user a file or a line in it, call show_file.";

}  

TEST_CASE("handle_mcp_line: initialize echoes a known protocol version, else the newest", "[mcp_bridge]") {
  FakeWindow win;
  const QJsonObject known = result_of(handle_mcp_line(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})", win.ask()));
  CHECK(known.value("protocolVersion").toString() == "2025-06-18");
  CHECK(known.value("serverInfo").toObject().value("name").toString() == "lumos-md");

  const QJsonObject unknown = result_of(handle_mcp_line(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})", win.ask()));
  CHECK(unknown.value("protocolVersion").toString() == "2025-11-25");

  const QJsonObject missing =
      result_of(handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})", win.ask()));
  CHECK(missing.value("protocolVersion").toString() == "2025-11-25");

  CHECK(missing.value("instructions").toString() == QString::fromUtf8(kInstructions));
}

TEST_CASE("handle_mcp_line: a notification gets no answer, whatever its method", "[mcp_bridge]") {
  FakeWindow win;
  CHECK(handle_mcp_line(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", win.ask()).isEmpty());
  CHECK(handle_mcp_line(R"({"jsonrpc":"2.0","method":"not_a_known_method"})", win.ask()).isEmpty());
  CHECK(win.calls.empty());
}

TEST_CASE("handle_mcp_line: ping gives an empty result; the id is echoed as is", "[mcp_bridge]") {
  FakeWindow win;
  const QByteArray out = handle_mcp_line(R"({"jsonrpc":"2.0","id":"abc","method":"ping"})", win.ask());
  const QJsonObject obj = QJsonDocument::fromJson(out).object();
  CHECK(obj.value("id").toString() == "abc");
  CHECK(obj.value("result").toObject().isEmpty());
}

TEST_CASE("handle_mcp_line: tools/list gives the three tools, in order", "[mcp_bridge]") {
  FakeWindow win;
  const QJsonArray tools =
      result_of(handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", win.ask())).value("tools").toArray();
  REQUIRE(tools.size() == 3);
  CHECK(tools.at(0).toObject().value("name").toString() == "list_open_files");
  CHECK(tools.at(1).toObject().value("name").toString() == "read_open_file");
  CHECK(tools.at(2).toObject().value("name").toString() == "show_file");
  CHECK(tools.at(0).toObject().value("annotations").toObject().value("readOnlyHint").toBool() == true);
  CHECK(tools.at(1).toObject().value("annotations").toObject().value("readOnlyHint").toBool() == true);
  const QJsonObject show_annotations = tools.at(2).toObject().value("annotations").toObject();
  CHECK(show_annotations.value("readOnlyHint") == QJsonValue(false));
  CHECK(show_annotations.value("destructiveHint") == QJsonValue(false));
  for (const int i : {1, 2}) {
    const QJsonArray required = tools.at(i).toObject().value("inputSchema").toObject().value("required").toArray();
    REQUIRE(required.size() == 1);
    CHECK(required.at(0).toString() == "path");
  }
  const QJsonObject line_schema =
      tools.at(2).toObject().value("inputSchema").toObject().value("properties").toObject().value("line").toObject();
  CHECK(line_schema.value("type").toString() == "integer");
  CHECK(line_schema.value("minimum") == QJsonValue(1));
  CHECK(line_schema.value("description").toString() ==
        "Line to show; 1 is the first line of the text that read_open_file gives. lumos-md moves the cursor to "
        "the start of the line and scrolls it to the middle of the view. A line after the end gives the last "
        "line.");
}

TEST_CASE("handle_mcp_line: list_open_files asks the window and gives its files", "[mcp_bridge]") {
  FakeWindow win;
  const QJsonArray files{
      QJsonObject{{"path", QJsonValue()}, {"name", "Untitled"}, {"modified", false}, {"current", true}}};
  win.answer = QJsonObject{{"files", files}};

  const QJsonObject result = result_of(
      handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_open_files"}})",
                      win.ask()));

  REQUIRE(win.calls.size() == 1);
  CHECK(win.calls.at(0) == QJsonObject{{"v", 1}, {"op", "list"}});
  CHECK(result.value("isError").toBool() == false);
  CHECK(content_text(result) == QString::fromUtf8(QJsonDocument(files).toJson(QJsonDocument::Compact)));
}

TEST_CASE("handle_mcp_line: read_open_file gives the exact tab text", "[mcp_bridge]") {
  FakeWindow win;
  const QString text = QStringLiteral("line one\nline é two \"quoted\"");
  win.answer = QJsonObject{{"open", true}, {"text", text}};

  const QByteArray out = call_read_open_file(R"("/a/b.md")");
  const QByteArray line_out = handle_mcp_line(out, win.ask());
  const QJsonObject result = result_of(line_out);

  REQUIRE(win.calls.size() == 1);
  CHECK(win.calls.at(0) == QJsonObject{{"v", 1}, {"op", "read"}, {"path", "/a/b.md"}});
  CHECK_FALSE(line_out.contains('\n'));
  CHECK(result.value("isError").toBool() == false);
  CHECK(content_text(result) == text);
}

TEST_CASE("handle_mcp_line: read_open_file error texts", "[mcp_bridge]") {
  FakeWindow win;

  win.answer = QJsonObject{{"open", false}};
  QJsonObject result = result_of(handle_mcp_line(call_read_open_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "b.md is not open in lumos-md. Read and edit the file on disk.");

  win.answer = QJsonObject{{"open", true}, {"utf8", false}};
  result = result_of(handle_mcp_line(call_read_open_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "The tab text of b.md is not valid UTF-8; lumos-md cannot pass it on. Edit the file on disk.");

  win.answer = std::nullopt;
  result = result_of(handle_mcp_line(call_read_open_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md is not running, or Tools -> Enable MCP Server is off. Read and edit the file on disk.");

  win.answer = QJsonObject{{"error", "bad_request"}};
  result = result_of(handle_mcp_line(call_read_open_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md is not running, or Tools -> Enable MCP Server is off. Read and edit the file on disk.");

  win.answer = std::nullopt;
  result = result_of(handle_mcp_line(
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_open_files"}})", win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md is not running, or Tools -> Enable MCP Server is off. Read and edit the file on disk.");
}

TEST_CASE("handle_mcp_line: read_open_file bad_path, the window is not asked", "[mcp_bridge]") {
  FakeWindow win;

  QJsonObject result = result_of(handle_mcp_line(call_read_open_file(R"("b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "Give an absolute path.");

  result = result_of(handle_mcp_line(
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read_open_file","arguments":{}}})",
      win.ask()));
  CHECK(content_text(result) == "Give an absolute path.");

  result = result_of(handle_mcp_line(call_read_open_file("5"), win.ask()));
  CHECK(content_text(result) == "Give an absolute path.");

  CHECK(win.calls.empty());
}

TEST_CASE("handle_mcp_line: show_file asks the window to show the path", "[mcp_bridge]") {
  FakeWindow win;
  win.answer = QJsonObject{{"shown", true}};

  const QJsonObject result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));

  REQUIRE(win.calls.size() == 1);
  CHECK(win.calls.at(0) == QJsonObject{{"v", 1}, {"op", "show"}, {"path", "/a/b.md"}});
  CHECK(result.value("isError").toBool() == false);
  CHECK(content_text(result) == "b.md is the current tab in lumos-md.");
}

TEST_CASE("handle_mcp_line: show_file error texts", "[mcp_bridge]") {
  FakeWindow win;

  win.answer = QJsonObject{{"shown", false}, {"reason", "busy"}};
  QJsonObject result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md has a dialog open. Ask the user to close it, then call show_file again.");

  win.answer = QJsonObject{{"shown", false}, {"reason", "not_allowed"}};
  result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) ==
        "/a/b.md is outside the folders the user allows for agents. Ask the user to add its folder in "
        "lumos-md: Tools -> MCP Allowed Folders.");

  win.answer = QJsonObject{{"shown", false}, {"reason", "not_file"}};
  result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "/a/b.md does not exist or is not a file.");

  win.answer = QJsonObject{{"shown", false}, {"reason", "unsupported_type"}};
  result = result_of(handle_mcp_line(call_show_file(R"("/a/key")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md shows only files named *.md *.markdown *.txt (a link: its target's name).");

  win.answer = QJsonObject{{"shown", false}, {"reason", "cannot_open"}, {"detail", "Permission denied"}};
  result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md cannot open b.md: Permission denied");

  win.answer = QJsonObject{{"shown", false}, {"reason", "some_new_reason"}};
  result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
  CHECK(result.value("isError").toBool() == true);
  CHECK(content_text(result) == "lumos-md refused: some_new_reason.");

  for (const std::optional<QJsonObject>& answer :
       {std::optional<QJsonObject>(), std::optional<QJsonObject>(QJsonObject{{"error", "bad_request"}})}) {
    win.answer = answer;
    result = result_of(handle_mcp_line(call_show_file(R"("/a/b.md")"), win.ask()));
    CHECK(result.value("isError").toBool() == true);
    CHECK(content_text(result) == "lumos-md is not running, or Tools -> Enable MCP Server is off.");
  }
}

TEST_CASE("handle_mcp_line: show_file bad_path, the window is not asked", "[mcp_bridge]") {
  FakeWindow win;
  for (const char* path : {R"("b.md")", "5", R"("")"}) {
    const QJsonObject result = result_of(handle_mcp_line(call_show_file(path), win.ask()));
    CHECK(result.value("isError").toBool() == true);
    CHECK(content_text(result) == "Give an absolute path.");
  }
  CHECK(win.calls.empty());
}

TEST_CASE("handle_mcp_line: show_file with line asks the window and gives the line", "[mcp_bridge]") {
  FakeWindow win;
  win.answer = QJsonObject{{"shown", true}, {"line", 5}};

  const QJsonObject result = result_of(handle_mcp_line(call_show_file_line(R"("/a/b.md")", "5"), win.ask()));

  REQUIRE(win.calls.size() == 1);
  CHECK(win.calls.at(0) == QJsonObject{{"v", 1}, {"op", "show"}, {"path", "/a/b.md"}, {"line", 5}});
  CHECK(result.value("isError").toBool() == false);
  CHECK(content_text(result) == "b.md is the current tab in lumos-md, at line 5.");
}

TEST_CASE("handle_mcp_line: show_file with a line past the end names the last line", "[mcp_bridge]") {
  FakeWindow win;
  win.answer = QJsonObject{{"shown", true}, {"line", 3}};

  const QJsonObject result = result_of(handle_mcp_line(call_show_file_line(R"("/a/b.md")", "100"), win.ask()));

  CHECK(result.value("isError").toBool() == false);
  CHECK(content_text(result) == "b.md is the current tab in lumos-md, at line 3, its last line.");
}

TEST_CASE("handle_mcp_line: show_file bad line, the window is not asked", "[mcp_bridge]") {
  FakeWindow win;
  for (const char* line : {"0", "-1", "1.5", R"("3")"}) {
    const QJsonObject result = result_of(handle_mcp_line(call_show_file_line(R"("/a/b.md")", line), win.ask()));
    CHECK(result.value("isError").toBool() == true);
    CHECK(content_text(result) == "Give line as a whole number from 1.");
  }
  CHECK(win.calls.empty());
}

TEST_CASE("handle_mcp_line: JSON-RPC level errors", "[mcp_bridge]") {
  FakeWindow win;

  QJsonObject obj = QJsonDocument::fromJson(
      handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nope"}})", win.ask()))
      .object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32602);

  obj = QJsonDocument::fromJson(
            handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{}})", win.ask()))
            .object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32602);

  obj = QJsonDocument::fromJson(handle_mcp_line(R"({"jsonrpc":"2.0","id":1,"method":"nonexistent"})", win.ask()))
            .object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32601);

  obj = QJsonDocument::fromJson(handle_mcp_line("not json", win.ask())).object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32700);
  CHECK(obj.value("id").isNull());

  obj = QJsonDocument::fromJson(handle_mcp_line(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])", win.ask())).object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32600);

  obj = QJsonDocument::fromJson(handle_mcp_line(R"({"jsonrpc":"1.0","id":1,"method":"ping"})", win.ask())).object();
  CHECK(obj.value("error").toObject().value("code").toInt() == -32600);

  CHECK(win.calls.empty());
}
