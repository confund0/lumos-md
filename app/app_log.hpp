#pragma once

#include <QString>
#include <QtGlobal>

inline constexpr qint64 kLogMaxBytes = 1024 * 1024;

struct LogOptions {
  QString path;
  bool also_stderr = false;  
  qint64 max_bytes = kLogMaxBytes;
};

bool rotate_log_if_large(const QString& path, qint64 max_bytes);

bool install_log(const LogOptions& options);

void uninstall_log();
