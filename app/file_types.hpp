#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

inline const QStringList& text_file_suffixes() {
  static const QStringList suffixes{QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("txt")};
  return suffixes;
}

inline bool is_text_file_name(const QString& path) {
  return text_file_suffixes().contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

inline QString text_file_patterns() {
  QStringList patterns;
  for (const QString& suffix : text_file_suffixes()) patterns.append(QStringLiteral("*.") + suffix);
  return patterns.join(QLatin1Char(' '));
}
