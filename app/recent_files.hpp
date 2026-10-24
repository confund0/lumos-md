#pragma once

#include <QString>
#include <QStringList>

inline constexpr int kRecentFilesMax = 10;

QStringList add_recent_file(QStringList list, const QString& path, int max = kRecentFilesMax);

QStringList remove_recent_file(QStringList list, const QString& path);

QString recent_file_label(int n, const QString& path, const QString& home);
