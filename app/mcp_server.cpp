#include "mcp_server.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QStringDecoder>
#include <QTimer>

#include <string>

#include "lumos/log.hpp"
#include "main_window.hpp"

namespace {
constexpr std::string_view kComp = "mcp";
constexpr qsizetype kMaxLineBytes = 64 * 1024;
constexpr int kRequestTimeoutMs = 5000;

QByteArray to_line(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QJsonObject bad_request() { return QJsonObject{{"error", "bad_request"}}; }

QJsonObject list_answer(const MainWindow& window, qsizetype& file_count) {
  QJsonArray files;
  for (const BufferInfo& buffer : window.list_buffers()) {
    files.append(QJsonObject{{"path", buffer.path.isEmpty() ? QJsonValue() : QJsonValue(buffer.path)},
                              {"name", buffer.name},
                              {"modified", buffer.modified},
                              {"current", buffer.current}});
  }
  file_count = files.size();
  return QJsonObject{{"files", files}};
}

QJsonObject read_answer(const MainWindow& window, const QString& path, std::string_view& result, qsizetype& size) {
  const std::optional<quint64> id = window.find_buffer(path);
  const std::optional<QByteArray> bytes = id ? window.read_buffer(*id) : std::nullopt;
  if (!bytes) {
    result = "not_open";
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

}  

QByteArray answer_window_request(const QByteArray& line, const MainWindow& window) {
  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &parse_error);
  const bool is_object = parse_error.error == QJsonParseError::NoError && doc.isObject();
  const QJsonObject request = is_object ? doc.object() : QJsonObject();
  const bool envelope_ok = is_object && request.value("v") == QJsonValue(1);
  const QString op = envelope_ok ? request.value("op").toString() : QString();

  if (envelope_ok && op == QStringLiteral("list")) {
    qsizetype file_count = 0;
    const QJsonObject answer = list_answer(window, file_count);
    lumos::log(lumos::LogLevel::debug, kComp, "server_request",
               {{"op", "list"}, {"result", "ok"}, {"files", std::to_string(file_count)}});
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
  lumos::log(lumos::LogLevel::debug, kComp, "server_request", {{"op", "bad"}, {"result", "bad_request"}});
  return to_line(bad_request());
}

McpServer::McpServer(const MainWindow& window) : window_(window) {
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
