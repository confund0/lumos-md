#pragma once

#include <QByteArray>
#include <QString>

enum class DiskState { same, changed, gone };

struct DiskCheck {
  DiskState state = DiskState::same;
  QByteArray sha256;
};

DiskCheck check_disk(const QString& path, const QByteArray& saved_sha256, const QByteArray& kept_sha256);
