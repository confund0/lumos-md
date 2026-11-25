#include "mcp_server.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QStringDecoder>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <string>

#include "document_tab.hpp"
#include "file_types.hpp"
#include "mcp_access.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "main_window.hpp"

namespace {
constexpr std::string_view kComp = "mcp";
constexpr qsizetype kMaxLineBytes = qsizetype{64} * 1024;
constexpr int kRequestTimeoutMs = 5000;

QByteArray to_line(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QJsonObject bad_request() { return QJsonObject{{"error", "bad_request"}}; }

bool is_valid_line(const QJsonValue& v) {
  if (!v.isDouble()) return false;
  const double d = v.toDouble();
  return d == std::floor(d) && d >= 1 && d <= 2147483647.0;
}

bool buffer_allowed(const BufferInfo& buffer, const QStringList& folders) {
  if (buffer.path.isEmpty() || !mcp_path_allowed(mcp_path_key(buffer.path), folders)) return false;
  return std::all_of(buffer.sources.begin(), buffer.sources.end(),
                     [&](const QString& source) { return mcp_path_allowed(source, folders); });
}

QJsonObject list_answer(const MainWindow& window, qsizetype& file_count, qsizetype& hidden_count) {
  const QStringList folders = window.mcp_effective_folders();
  QJsonArray files;
  qsizetype hidden = 0;
  for (const BufferInfo& buffer : window.list_buffers()) {
    if (!buffer_allowed(buffer, folders)) {
      ++hidden;
      continue;
    }
    files.append(QJsonObject{{"path", buffer.path},
                              {"name", buffer.name},
                              {"modified", buffer.modified},
                              {"current", buffer.current}});
  }
  file_count = files.size();
  hidden_count = hidden;
  return QJsonObject{{"files", files}};
}

QJsonObject read_answer(const MainWindow& window, const QString& path, std::string_view& result, qsizetype& size) {
  const std::optional<quint64> id = window.find_buffer(path);
  const std::optional<QByteArray> bytes = id ? window.read_buffer(*id) : std::nullopt;
  if (!id || !bytes) {
    result = "not_open";
    return QJsonObject{{"open", false}};
  }
  const quint64 found = *id;
  const QList<BufferInfo> buffers = window.list_buffers();
  const auto buffer =
      std::find_if(buffers.begin(), buffers.end(), [found](const BufferInfo& b) { return b.id == found; });
  if (buffer == buffers.end() || !buffer_allowed(*buffer, window.mcp_effective_folders())) {
    result = "not_allowed";
    return QJsonObject{{"open", false}};
  }
  QStringDecoder decoder(QStringDecoder::Utf8);
  const QString text = decoder(*bytes);
  if (decoder.hasError()) {
    result = "not_utf8";
    return QJsonObject{{"open", true}, {"utf8", false}};
  }
  result = "ok";
  size = bytes->size();
  return QJsonObject{{"open", true}, {"text", text}};
}

QJsonObject show_answer(MainWindow& window, const QString& path, std::string_view& result,
                         std::optional<int> line) {
  if (QApplication::activeModalWidget() != nullptr) {
    result = "busy";
    return QJsonObject{{"shown", false}, {"reason", "busy"}};
  }
  const QString target = QFileInfo(path).canonicalFilePath();
  const QString checked = target.isEmpty() ? path : target;
  if (!mcp_path_allowed(checked, window.mcp_effective_folders())) {
    result = "not_allowed";
    return QJsonObject{{"shown", false}, {"reason", "not_allowed"}};
  }
  if (target.isEmpty() ? !window.find_buffer(path) : !QFileInfo(target).isFile()) {
    result = "not_file";
    return QJsonObject{{"shown", false}, {"reason", "not_file"}};
  }
  if (!is_text_file_name(checked)) {
    result = "unsupported_type";
    return QJsonObject{{"shown", false}, {"reason", "unsupported_type"}};
  }
  QString error;
  if (!window.open_buffer(checked, &error)) {
    result = "cannot_open";
    return QJsonObject{{"shown", false}, {"reason", "cannot_open"}, {"detail", error}};
  }
  if (window.isMinimized()) window.showNormal();
  window.raise();
  QApplication::alert(&window);
  result = "ok";
  if (!line) return QJsonObject{{"shown", true}};
  lumos::qt::SourceEditor* editor = window.current_tab() ? window.current_tab()->editor() : nullptr;
  if (!editor) return QJsonObject{{"shown", true}};
  const int shown_line = std::min(*line, static_cast<int>(editor->line_count()));
  const std::size_t index = static_cast<std::size_t>(*line - 1);
  QTimer::singleShot(0, editor, [editor, index] { editor->go_to_line(index); });
  return QJsonObject{{"shown", true}, {"line", shown_line}};
}

}  

QByteArray answer_window_request(const QByteArray& line, MainWindow& window) {
  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &parse_error);
  const bool is_object = parse_error.error == QJsonParseError::NoError && doc.isObject();
  const QJsonObject request = is_object ? doc.object() : QJsonObject();
  const bool envelope_ok = is_object && request.value("v") == QJsonValue(1);
  const QString op = envelope_ok ? request.value("op").toString() : QString();

  if (envelope_ok && op == QStringLiteral("list")) {
    qsizetype file_count = 0;
    qsizetype hidden_count = 0;
    const QJsonObject answer = list_answer(window, file_count, hidden_count);
    lumos::log(lumos::LogLevel::debug, kComp, "server_request",
               {{"op", "list"},
                {"result", "ok"},
                {"files", std::to_string(file_count)},
                {"hidden", std::to_string(hidden_count)}});
    return to_line(answer);
  }
  if (envelope_ok && op == QStringLiteral("read")) {
    const QJsonValue path_v = request.value("path");
    if (!path_v.isString()) {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "read"}, {"result", "bad_request"}});
      return to_line(bad_request());
    }
    std::string_view result = "bad_request";
    qsizetype size = 0;
    const QJsonObject answer = read_answer(window, path_v.toString(), result, size);
    if (result == "ok") {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request",
                 {{"op", "read"}, {"result", "ok"}, {"size", std::to_string(size)}});
    } else {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "read"}, {"result", result}});
    }
    return to_line(answer);
  }
  if (envelope_ok && op == QStringLiteral("show")) {
    const QJsonValue path_v = request.value("path");
    const QJsonValue line_v = request.value("line");
    const bool has_line = !line_v.isUndefined() && !line_v.isNull();
    if (!path_v.isString() || !QDir::isAbsolutePath(path_v.toString()) || (has_line && !is_valid_line(line_v))) {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "show"}, {"result", "bad_request"}});
      return to_line(bad_request());
    }
    std::string_view result = "bad_request";
    const std::optional<int> req_line =
        has_line ? std::make_optional(static_cast<int>(line_v.toDouble())) : std::nullopt;
    const QJsonObject answer = show_answer(window, QDir::cleanPath(path_v.toString()), result, req_line);
    const std::string line_str = has_line ? std::to_string(*req_line) : std::string();
    if (has_line) {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request",
                 {{"op", "show"}, {"result", result}, {"line", line_str}});
    } else {
      lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "show"}, {"result", result}});
    }
    return to_line(answer);
  }
  lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "bad"}, {"result", "bad_request"}});
  return to_line(bad_request());
}

McpServer::McpServer(MainWindow& window) : window_(window) {
  QObject::connect(&server_, &QLocalServer::newConnection, &server_, [this] { handle_new_connection(); });
}

bool McpServer::listen(const QString& name) {
  name_ = name;
  const QFileInfo existing(name);
  if (QDir::isAbsolutePath(name) && (existing.isSymLink() || existing.isFile() || existing.isDir())) {
    lumos::log(lumos::LogLevel::debug, kComp, "server_listen", {{"ok", "0"}, {"reason", "not_socket"}});
    return false;
  }
  QLocalServer::removeServer(name);
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  const bool ok = server_.listen(name);
  lumos::log(lumos::LogLevel::debug, kComp, "server_listen", {{"ok", ok ? "1" : "0"}});
  return ok;
}

void McpServer::close() { server_.close(); }

bool McpServer::is_listening() const { return server_.isListening(); }

void McpServer::handle_new_connection() {
  while (QLocalSocket* socket = server_.nextPendingConnection()) {
    socket->setParent(&server_);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->start(kRequestTimeoutMs);
    QObject::connect(timer, &QTimer::timeout, socket, [this, socket, timer] { fail(socket, timer); });
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket, timer] { read_socket(socket, timer); });
    QObject::connect(socket, &QLocalSocket::disconnected, socket,
                      [this, socket] { buffers_.remove(socket); socket->deleteLater(); });
  }
}

void McpServer::read_socket(QLocalSocket* socket, QTimer* timer) {
  QByteArray& buffer = buffers_[socket];
  buffer += socket->readAll();
  const qsizetype end = buffer.indexOf('\n');
  if (end < 0) {
    if (buffer.size() > kMaxLineBytes) fail(socket, timer);
    return;
  }
  const QByteArray line = buffer.left(end);
  finish(socket, timer, line);
}

void McpServer::fail(QLocalSocket* socket, QTimer* timer) {
  timer->stop();
  buffers_.remove(socket);
  if (socket->state() == QLocalSocket::ConnectedState) socket->write(to_line(bad_request()) + '\n');
  socket->disconnectFromServer();
}

void McpServer::finish(QLocalSocket* socket, QTimer* timer, const QByteArray& line) {
  timer->stop();
  buffers_.remove(socket);
  socket->write(answer_window_request(line, window_) + '\n');
  socket->disconnectFromServer();
}
