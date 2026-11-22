#include "file_watch.hpp"

#include <QFile>

#include "drafts.hpp"

DiskCheck check_disk(const QString& path, const QByteArray& saved_sha256, const QByteArray& kept_sha256) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {DiskState::gone, QByteArray()};
  const QByteArray sha = sha256_hex(f.readAll());
  const bool same = sha == saved_sha256 || (!kept_sha256.isEmpty() && sha == kept_sha256);
  return {same ? DiskState::same : DiskState::changed, sha};
}
