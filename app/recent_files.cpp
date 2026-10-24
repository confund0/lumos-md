#include "recent_files.hpp"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace {

QString clean(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }

}  

QStringList add_recent_file(QStringList list, const QString& path, int max) {
  list = remove_recent_file(std::move(list), path);
  list.prepend(clean(path));
  while (list.size() > max) list.removeLast();
  return list;
}

QStringList remove_recent_file(QStringList list, const QString& path) {
  list.removeAll(clean(path));
  return list;
}

QString recent_file_label(int n, const QString& path, const QString& home) {
  QString shown = path;
  if (!home.isEmpty() && shown.startsWith(home + QLatin1Char('/'))) shown = QLatin1Char('~') + shown.mid(home.size());
  shown = QDir::toNativeSeparators(shown).replace(QLatin1Char('&'), QLatin1String("&&"));
  return n < 10 ? QStringLiteral("&%1 %2").arg(n).arg(shown) : QStringLiteral("%1 %2").arg(n).arg(shown);
}
