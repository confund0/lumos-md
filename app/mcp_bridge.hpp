#pragma once

#include <QByteArray>
#include <QDir>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

inline QString mcp_socket_in(const QString& dir) {
  return QDir(dir).filePath(QStringLiteral("lumos-md-mcp.sock"));
}

using AskWindow = std::function<std::optional<QJsonObject>(const QJsonObject& request)>;

QByteArray handle_mcp_line(const QByteArray& line, const AskWindow& ask);

std::optional<QJsonObject> ask_window(const QString& server_name, const QJsonObject& request, int connect_ms = 2000,
                                      int answer_ms = 5000);

int run_mcp_bridge(const QString& server_name);
