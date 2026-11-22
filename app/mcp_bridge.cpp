#include "mcp_bridge.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

#include <cstdio>
#include <iostream>
#include <string>

#include "lumos/log.hpp"

namespace {
constexpr std::string_view kComp = "mcp";

const char kInstructions[] =
    "lumos-md is the user's Markdown editor. Before you change a file, call read_open_file with its absolute path. "
    "If the file is open, it may have unsaved edits: make your change on the text that read_open_file gives, then "
    "write the whole file. lumos-md shows the change to the user, who accepts or picks parts of it. If the file is "
    "not open, edit it on disk as usual.";

const char kNewestProtocolVersion[] = "2025-11-25";

bool known_protocol_version(const QString& version) {
  return version == "2024-11-05" || version == "2025-03-26" || version == "2025-06-18" || version == "2025-11-25";
}

QJsonValue error_id(const QJsonObject& request) {
  const QJsonValue id = request.value("id");
  if (id.isDouble() || id.isString()) return id;
  return QJsonValue();
}

QByteArray to_line(const QJsonObject& response) { return QJsonDocument(response).toJson(QJsonDocument::Compact); }

QByteArray make_error(const QJsonValue& id, int code, const QString& message) {
  const QJsonObject error{{"code", code}, {"message", message}};
  return to_line(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", error}});
}

QByteArray make_result(const QJsonValue& id, const QJsonValue& result) {
  return to_line(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

QJsonObject tool_result(const QString& text, bool is_error) {
  const QJsonArray content{QJsonObject{{"type", "text"}, {"text", text}}};
  return QJsonObject{{"content", content}, {"isError", is_error}};
}

QJsonObject tools_list_result() {
  const QJsonObject list_open_files{
      {"name", "list_open_files"},
      {"description", "Lists the files open in the user's lumos-md editor, in tab order: path (null for an "
                       "Untitled tab), name, modified (has unsaved edits), current (the tab in front)."},
      {"inputSchema", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
      {"annotations", QJsonObject{{"readOnlyHint", true}}}};
  const QJsonObject read_open_file{
      {"name", "read_open_file"},
      {"description", "Gives the text of a file as it is open in lumos-md, with the user's unsaved edits. Call it "
                       "before you change a file. If the file is open, make your change on this text and write the "
                       "whole file."},
      {"inputSchema", QJsonObject{{"type", "object"},
                                   {"properties", QJsonObject{{"path", QJsonObject{{"type", "string"},
                                                                                    {"description",
                                                                                     "Absolute path of the file."}}}}},
                                   {"required", QJsonArray{"path"}}}},
      {"annotations", QJsonObject{{"readOnlyHint", true}}}};
  return QJsonObject{{"tools", QJsonArray{list_open_files, read_open_file}}};
}

QJsonObject call_list_open_files(const AskWindow& ask) {
  const std::optional<QJsonObject> answer = ask(QJsonObject{{"v", 1}, {"op", "list"}});
  if (!answer || !answer->value("files").isArray()) {
    lumos::log(lumos::LogLevel::debug, kComp, "bridge_call", {{"tool", "list"}, {"result", "no_answer"}});
    return tool_result("lumos-md is not running, or Tools, Enable MCP Server is off. Read and edit the file on disk.",
                        true);
  }
  lumos::log(lumos::LogLevel::debug, kComp, "bridge_call", {{"tool", "list"}, {"result", "ok"}});
  const QByteArray files_json = QJsonDocument(answer->value("files").toArray()).toJson(QJsonDocument::Compact);
  return tool_result(QString::fromUtf8(files_json).trimmed(), false);
}

QJsonObject call_read_open_file(const AskWindow& ask, const QJsonObject& arguments) {
  const QJsonValue path_v = arguments.value("path");
  if (!path_v.isString() || !QDir::isAbsolutePath(path_v.toString())) {
    lumos::log(lumos::LogLevel::debug, kComp, "bridge_call", {{"tool", "read"}, {"result", "bad_path"}});
    return tool_result("Give an absolute path.", true);
  }
  const QString path = path_v.toString();
  const QString name = QFileInfo(path).fileName();
  const std::optional<QJsonObject> answer = ask(QJsonObject{{"v", 1}, {"op", "read"}, {"path", path}});
  const char* result = "no_answer";
  QString text;
  bool is_error = true;
  if (answer) {
    const QJsonValue open_v = answer->value("open");
    if (open_v.isBool() && !open_v.toBool()) {
      result = "not_open";
      text = name + " is not open in lumos-md. Read and edit the file on disk.";
    } else if (open_v.isBool() && open_v.toBool()) {
      const QJsonValue utf8_v = answer->value("utf8");
      const QJsonValue text_v = answer->value("text");
      if (utf8_v.isBool() && !utf8_v.toBool()) {
        result = "not_utf8";
        text = "The tab text of " + name + " is not valid UTF-8; lumos-md cannot pass it on. Edit the file on disk.";
      } else if (text_v.isString()) {
        result = "ok";
        text = text_v.toString();
        is_error = false;
      }
    }
  }
  if (std::string_view(result) == "no_answer") {
    text = "lumos-md is not running, or Tools, Enable MCP Server is off. Read and edit the file on disk.";
  }
  lumos::log(lumos::LogLevel::debug, kComp, "bridge_call", {{"tool", "read"}, {"result", result}});
  return tool_result(text, is_error);
}

QByteArray handle_tools_call(const QJsonValue& id, const QJsonObject& params, const AskWindow& ask) {
  const QJsonValue name_v = params.value("name");
  if (!name_v.isString()) return make_error(id, -32602, "Invalid params");
  const QString name = name_v.toString();
  const QJsonObject arguments = params.value("arguments").toObject();
  if (name == "list_open_files") return make_result(id, call_list_open_files(ask));
  if (name == "read_open_file") return make_result(id, call_read_open_file(ask, arguments));
  return make_error(id, -32602, "Unknown tool: " + name);
}

}  

QByteArray handle_mcp_line(const QByteArray& line, const AskWindow& ask) {
  if (line.trimmed().isEmpty()) return QByteArray();

  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) return make_error(QJsonValue(), -32700, "Parse error");
  if (!doc.isObject()) return make_error(QJsonValue(), -32600, "Invalid Request");

  const QJsonObject request = doc.object();
  const QJsonValue jsonrpc = request.value("jsonrpc");
  const QJsonValue method_v = request.value("method");
  if (!jsonrpc.isString() || jsonrpc.toString() != "2.0" || !method_v.isString()) {
    return make_error(error_id(request), -32600, "Invalid Request");
  }

  if (!request.contains("id")) return QByteArray();

  const QJsonValue id = error_id(request);
  const QString method = method_v.toString();
  const QJsonObject params = request.value("params").toObject();

  if (method == "initialize") {
    const QString requested = params.value("protocolVersion").toString();
    const QString version = known_protocol_version(requested) ? requested : QString(kNewestProtocolVersion);
    const QJsonObject result{{"protocolVersion", version},
                              {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
                              {"serverInfo", QJsonObject{{"name", "lumos-md"}, {"version", LUMOS_MD_VERSION}}},
                              {"instructions", kInstructions}};
    return make_result(id, result);
  }
  if (method == "ping") return make_result(id, QJsonObject{});
  if (method == "tools/list") return make_result(id, tools_list_result());
  if (method == "tools/call") {
    if (!request.value("params").isObject()) return make_error(id, -32602, "Invalid params");
    return handle_tools_call(id, params, ask);
  }
  return make_error(id, -32601, "Method not found");
}

std::optional<QJsonObject> ask_window(const QString& server_name, const QJsonObject& request, int connect_ms,
                                      int answer_ms) {
  QLocalSocket socket;
  socket.connectToServer(server_name);
  if (!socket.waitForConnected(connect_ms)) return std::nullopt;

  const QByteArray message = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
  socket.write(message);

  QElapsedTimer clock;
  clock.start();
  const auto remaining = [&] { return static_cast<int>(answer_ms - clock.elapsed()); };
  while (socket.bytesToWrite() > 0) {
    const int left = remaining();
    if (left <= 0 || !socket.waitForBytesWritten(left)) return std::nullopt;
  }

  QByteArray reply;
  while (!reply.contains('\n')) {
    const int left = remaining();
    if (left <= 0) return std::nullopt;
    if (!socket.waitForReadyRead(left)) return std::nullopt;
    reply += socket.readAll();
  }

  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(reply.left(reply.indexOf('\n')), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  return doc.object();
}

int run_mcp_bridge(const QString& server_name) {
  lumos::log(lumos::LogLevel::debug, kComp, "bridge_start");

  const AskWindow ask = [&server_name](const QJsonObject& request) { return ask_window(server_name, request); };

  std::string raw;
  while (std::getline(std::cin, raw)) {
    QByteArray line = QByteArray::fromStdString(raw);
    if (line.endsWith('\r')) line.chop(1);
    const QByteArray answer = handle_mcp_line(line, ask);
    if (!answer.isEmpty()) {
      std::fwrite(answer.constData(), 1, static_cast<size_t>(answer.size()), stdout);
      std::fputc('\n', stdout);
      std::fflush(stdout);
    }
  }
  return 0;
}
