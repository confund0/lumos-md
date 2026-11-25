#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

inline QString mcp_path_key(const QString& path) {
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

inline bool mcp_path_allowed(const QString& key, const QStringList& folders) {
  if (key.isEmpty()) return false;
  for (const QString& folder : folders) {
    if (folder.isEmpty()) continue;
    if (key == folder) return true;
    const QString prefix = folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/');
    if (key.startsWith(prefix)) return true;
  }
  return false;
}
