#pragma once

#include <QString>
#include <QStringList>

struct OpenTabsRead {
  QStringList paths;
  QString active;
};

OpenTabsRead read_open_tabs(const QString& file);

bool write_open_tabs(const QString& file, const QStringList& paths, const QString& active);

QString draft_entry_id(const QString& entry);
