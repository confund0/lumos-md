#include "open_tabs.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <string>

#include "drafts.hpp"
#include "lumos/log.hpp"

namespace {

constexpr std::string_view kComp = "app";
constexpr QChar kActiveMark = QLatin1Char('*');
constexpr QLatin1String kDraftPrefix("draft:");

bool has_control(const QString& path) {
  for (const QChar c : path) {
    if (c.category() == QChar::Other_Control) return true;
  }
  return false;
}

bool unfit(const QString& entry) {
  if (entry.startsWith(kDraftPrefix)) return draft_entry_id(entry).isEmpty();
  return entry.isEmpty() || !QDir::isAbsolutePath(entry) || has_control(entry);
}

}  

QString draft_entry_id(const QString& entry) {
  if (!entry.startsWith(kDraftPrefix)) return QString();
  const QString id = entry.mid(kDraftPrefix.size());
  return is_draft_id(id) ? id : QString();
}

OpenTabsRead read_open_tabs(const QString& file) {
  OpenTabsRead read;
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) {
    lumos::log(lumos::LogLevel::debug, kComp, "open_tabs_read", {{"count", "0"}, {"left_out", "0"}});
    return read;
  }
  const QString text = QString::fromUtf8(f.readAll());
  const QStringList lines = text.split(QLatin1Char('\n'));
  int left_out = 0;
  for (const QString& line : lines) {
    if (line.isEmpty()) continue;
    const bool marked = line.startsWith(kActiveMark);
    const QString path = marked ? line.mid(1) : line;
    if (unfit(path) || read.paths.contains(path)) {
      ++left_out;
      continue;
    }
    read.paths.append(path);
    if (marked && read.active.isEmpty()) read.active = path;
  }
  lumos::log(lumos::LogLevel::debug, kComp, "open_tabs_read",
             {{"count", std::to_string(read.paths.size())}, {"left_out", std::to_string(left_out)}});
  return read;
}

bool write_open_tabs(const QString& file, const QStringList& paths, const QString& active) {
  QByteArray bytes;
  for (const QString& path : paths) {
    if (unfit(path)) continue;
    if (path == active) bytes.append('*');
    bytes.append(path.toUtf8());
    bytes.append('\n');
  }
  QDir().mkpath(QFileInfo(file).absolutePath());
  QSaveFile f(file);
  const bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
  lumos::log(lumos::LogLevel::debug, kComp, "open_tabs_write",
             {{"count", std::to_string(paths.size())}, {"ok", ok ? "1" : "0"}});
  return ok;
}
