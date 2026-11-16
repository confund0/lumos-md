#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QLockFile>
#include <QString>
#include <QStringList>

#include <memory>

struct Draft {
  bool untitled = false;
  QString path;
  QByteArray saved_sha256;  
  QByteArray text;
};

enum class DraftState { ok, damaged, unreadable, missing };

struct DraftRead {
  DraftState state = DraftState::missing;
  Draft draft;
  QByteArray bytes;
  QDateTime written;  
};

QByteArray sha256_hex(QByteArrayView data);

QString path_draft_id(const QString& path);

QString new_untitled_draft_id();

bool is_draft_id(const QString& id);

QByteArray encode_draft(const Draft& d);

DraftRead decode_draft(const QByteArray& bytes);

bool write_draft(const QString& dir, const QString& id, const Draft& d);

DraftRead read_draft(const QString& dir, const QString& id);

bool remove_draft(const QString& dir, const QString& id);

QStringList list_draft_ids(const QString& dir);

QStringList path_draft_ids(const QString& dir, const QString& path);

std::unique_ptr<QLockFile> lock_draft(const QString& dir, const QString& id);

QString claim_path_draft_id(const QString& dir, const QString& path, std::unique_ptr<QLockFile>* lock);
