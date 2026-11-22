#pragma once

#include <QHash>
#include <QLocalServer>
#include <QString>
#include <QStringList>

#include <functional>

class QLocalSocket;
class QTimer;

QString instance_server_name(const QString& state_dir, const QString& runtime_dir,
                              const QString& prefix = QStringLiteral("lumos-md-"));

enum class SendResult { sent, no_server, failed };

SendResult send_to_running(const QString& name, const QStringList& paths, const QString& activation_token,
                           int timeout_ms);

class InstanceServer {
 public:
  using OnPaths = std::function<void(const QStringList& paths, const QString& activation_token)>;
  explicit InstanceServer(OnPaths on_paths);

  bool listen(const QString& name);
  void close();

 private:
  void handle_new_connection();
  void read_socket(QLocalSocket* socket, QTimer* timer);
  void reject(QLocalSocket* socket, QTimer* timer, const char* reason);
  void finish(QLocalSocket* socket, QTimer* timer, const QStringList& paths, const QString& activation_token);

  OnPaths on_paths_;
  QLocalServer server_;
  QHash<QLocalSocket*, QByteArray> buffers_;
};
