#include "app_log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string_view>

#include "lumos/log.hpp"

namespace {

struct LogState {
  std::mutex mutex;
  LogOptions options;
  std::unique_ptr<QFile> file;
};

LogState& state() {
  static LogState s;
  return s;
}

void write_stderr(std::string_view text) {
  std::fwrite(text.data(), 1, text.size(), stderr);
  std::fputc('\n', stderr);
}

bool open_file(LogState& s) {
  s.file = std::make_unique<QFile>(s.options.path);
  if (!s.file->open(QIODevice::WriteOnly | QIODevice::Append)) {
    s.file.reset();
    return false;
  }
  return true;
}

void rotate_or_reopen(LogState& s) {
  const QFileInfo info(s.options.path);
  if (info.exists() && info.size() <= s.options.max_bytes && s.file->size() == info.size()) return;
  s.file.reset();
  rotate_log_if_large(s.options.path, s.options.max_bytes);
  if (!open_file(s)) write_stderr("lumos-md: the log file cannot be opened again");
}

void sink(lumos::LogLevel, std::string_view line) {
  LogState& s = state();
  const std::lock_guard lock(s.mutex);
  if (s.options.also_stderr || !s.file) write_stderr(line);
  if (!s.file) return;
  s.file->write(line.data(), static_cast<qint64>(line.size()));
  s.file->write("\n", 1);
  s.file->flush();
  rotate_or_reopen(s);
}

}  

bool rotate_log_if_large(const QString& path, qint64 max_bytes) {
  const QFileInfo info(path);
  if (!info.exists() || info.size() <= max_bytes) return false;
  const QByteArray from = QFile::encodeName(path);
  const QByteArray to = QFile::encodeName(path + QStringLiteral(".1"));
  return std::rename(from.constData(), to.constData()) == 0;
}

bool install_log(const LogOptions& options) {
  LogState& s = state();
  bool opened = false;
  {
    const std::lock_guard lock(s.mutex);
    s.options = options;
    QDir().mkpath(QFileInfo(options.path).absolutePath());
    rotate_log_if_large(options.path, options.max_bytes);
    opened = open_file(s);
  }
  lumos::set_log_sink(sink);
  if (!opened) write_stderr("lumos-md: the log file cannot be opened; the log goes to stderr only");
  return opened;
}

void uninstall_log() {
  lumos::set_log_sink(nullptr);
  LogState& s = state();
  const std::lock_guard lock(s.mutex);
  s.file.reset();
}
