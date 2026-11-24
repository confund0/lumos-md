#pragma once

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QString>

class MainWindow;
class QLocalSocket;
class QTimer;

QByteArray answer_window_request(const QByteArray& line, MainWindow& window);

class McpServer {
 public:
  explicit McpServer(MainWindow& window);

  bool listen(const QString& name);
  void close();
  bool is_listening() const;
  const QString& name() const { return name_; }

 private:
  void handle_new_connection();
  void read_socket(QLocalSocket* socket, QTimer* timer);
  void fail(QLocalSocket* socket, QTimer* timer);
  void finish(QLocalSocket* socket, QTimer* timer, const QByteArray& line);

  MainWindow& window_;
  QLocalServer server_;
  QString name_;
  QHash<QLocalSocket*, QByteArray> buffers_;
};
