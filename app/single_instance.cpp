#include "single_instance.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QTimer>

#include <string>

#include "lumos/log.hpp"

namespace {
constexpr std::string_view kComp = "app";
constexpr qint64 kMaxMessageBytes = 1024 * 1024;
constexpr int kMessageTimeoutMs = 5000;
constexpr qsizetype kMaxTokenBytes = 4096;
constexpr char kHeader[] = "lumos-md-open 2";
constexpr char kTokenKey[] = "token=";

bool token_ok(const QByteArray& token) {
  if (token.size() > kMaxTokenBytes) return false;
  for (const char c : token) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
  }
  return true;
}
}  

QString instance_server_name(const QString& state_dir, const QString& runtime_dir, const QString& prefix) {
  const QString cleaned = QDir::cleanPath(QDir(state_dir).absolutePath());
  const QByteArray hash = QCryptographicHash::hash(cleaned.toUtf8(), QCryptographicHash::Sha256);
  const QString name = prefix + QString::fromLatin1(hash.toHex().left(16));
  if (!runtime_dir.isEmpty() && QDir(runtime_dir).exists()) return QDir(runtime_dir).filePath(name);
  return name;
}

SendResult send_to_running(const QString& name, const QStringList& paths, const QString& activation_token,
                           int timeout_ms) {
  for (const QString& path : paths) {
    if (path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r'))) return SendResult::failed;
  }

  QElapsedTimer clock;
  clock.start();
  const auto remaining = [&] { return static_cast<int>(timeout_ms - clock.elapsed()); };

  QLocalSocket socket;
  socket.connectToServer(name);
  if (!socket.waitForConnected(timeout_ms)) return SendResult::no_server;

  QByteArray token = activation_token.toUtf8();
  if (!token_ok(token)) token.clear();
  QByteArray message = QByteArray(kHeader) + '\n' + kTokenKey + token + '\n';
  for (const QString& path : paths) message += path.toUtf8() + '\n';
  message += '\n';
  socket.write(message);
  while (socket.bytesToWrite() > 0) {
    const int left = remaining();
    if (left <= 0 || !socket.waitForBytesWritten(left)) return SendResult::failed;
  }

  QByteArray reply;
  while (!reply.contains('\n')) {
    const int left = remaining();
    if (left <= 0) return SendResult::failed;
    if (!socket.waitForReadyRead(left)) return SendResult::failed;
    reply += socket.readAll();
  }
  return reply == "ok\n" ? SendResult::sent : SendResult::failed;
}

InstanceServer::InstanceServer(OnPaths on_paths) : on_paths_(std::move(on_paths)) {
  QObject::connect(&server_, &QLocalServer::newConnection, &server_, [this] { handle_new_connection(); });
}

bool InstanceServer::listen(const QString& name) {
  QLocalServer::removeServer(name);
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server_.listen(name)) {
    lumos::log(lumos::LogLevel::debug, kComp, "instance_listen",
               {{"ok", "0"}, {"error", std::to_string(static_cast<int>(server_.serverError()))}});
    return false;
  }
  lumos::log(lumos::LogLevel::debug, kComp, "instance_listen", {{"ok", "1"}});
  return true;
}

void InstanceServer::close() { server_.close(); }

void InstanceServer::handle_new_connection() {
  while (QLocalSocket* socket = server_.nextPendingConnection()) {
    socket->setParent(&server_);
    auto* timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->start(kMessageTimeoutMs);
    QObject::connect(timer, &QTimer::timeout, socket, [this, socket, timer] { reject(socket, timer, "timeout"); });
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket, timer] { read_socket(socket, timer); });
    QObject::connect(socket, &QLocalSocket::disconnected, socket,
                      [this, socket] { buffers_.remove(socket); socket->deleteLater(); });
  }
}

void InstanceServer::read_socket(QLocalSocket* socket, QTimer* timer) {
  QByteArray& buffer = buffers_[socket];
  buffer += socket->readAll();
  const qsizetype end = buffer.indexOf("\n\n");
  if (end < 0) {
    if (buffer.size() > kMaxMessageBytes) reject(socket, timer, "too_large");
    return;
  }
  const QList<QByteArray> lines = buffer.left(end).split('\n');
  if (lines.size() < 2 || lines[0] != kHeader) {
    reject(socket, timer, "header");
    return;
  }
  const QByteArray token = lines[1].mid(qsizetype{sizeof kTokenKey} - 1);
  if (!lines[1].startsWith(kTokenKey) || !token_ok(token)) {
    reject(socket, timer, "token");
    return;
  }
  QStringList paths;
  for (int i = 2; i < lines.size(); ++i) {
    const QString path = QString::fromUtf8(lines[i]);
    if (!QDir::isAbsolutePath(path)) {
      reject(socket, timer, "not_absolute");
      return;
    }
    paths.append(path);
  }
  finish(socket, timer, paths, QString::fromUtf8(token));
}

void InstanceServer::reject(QLocalSocket* socket, QTimer* timer, const char* reason) {
  lumos::log(lumos::LogLevel::debug, kComp, "instance_bad_message", {{"reason", reason}});
  timer->stop();
  buffers_.remove(socket);
  socket->disconnectFromServer();
}

void InstanceServer::finish(QLocalSocket* socket, QTimer* timer, const QStringList& paths,
                            const QString& activation_token) {
  timer->stop();
  buffers_.remove(socket);
  lumos::log(lumos::LogLevel::debug, kComp, "instance_message",
             {{"paths", std::to_string(paths.size())}, {"token", activation_token.isEmpty() ? "0" : "1"}});
  socket->write("ok\n");
  socket->disconnectFromServer();
  on_paths_(paths, activation_token);
}
