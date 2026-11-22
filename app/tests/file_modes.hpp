#pragma once

#include <QFile>
#include <QFileDevice>
#include <QTemporaryDir>

namespace {

inline bool file_modes_apply() {
  QTemporaryDir dir;
  if (!dir.isValid()) return false;
  const QString path = dir.filePath(QStringLiteral("probe"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.close();
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner)) return false;
  const bool writable = file.open(QIODevice::WriteOnly);
  file.close();
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return !writable;
}

}  
