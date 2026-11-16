#include "drafts.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/log.hpp"

namespace {

constexpr std::string_view kComp = "app";
constexpr qsizetype kMaxHeaderScan = 65536;  
constexpr auto kMagic = "lumos-md-draft 1";
constexpr auto kDraftSuffix = ".draft";
constexpr int kMaxPathDrafts = 100;  

const char* state_name(DraftState state) {
  switch (state) {
    case DraftState::ok:
      return "ok";
    case DraftState::damaged:
      return "damaged";
    case DraftState::unreadable:
      return "unreadable";
    case DraftState::missing:
      return "missing";
  }
  return "unknown";
}

bool is_hex64(const QByteArray& value) {
  if (value.size() != 64) return false;
  for (const char c : value) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool is_decimal(const QByteArray& value) {
  if (value.isEmpty()) return false;
  for (const char c : value) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

void set_dir_mode(const QString& path) {
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}
void set_file_mode(const QString& path) { QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner); }

void ensure_drafts_dir(const QString& dir) {
  QDir().mkpath(dir);
  set_dir_mode(dir);
}

QString draft_file(const QString& dir, const QString& id) { return QDir(dir).filePath(id + QLatin1String(kDraftSuffix)); }

QString path_draft_id_n(const QString& base, int n) {
  return n == 1 ? base : base + QLatin1Char('-') + QString::number(n);
}

}  

QByteArray sha256_hex(QByteArrayView data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(); }

QString path_draft_id(const QString& path) {
  const QFileInfo info(path);
  QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty()) canonical = QDir::cleanPath(info.absoluteFilePath());
  return QStringLiteral("p-") + QString::fromLatin1(sha256_hex(canonical.toUtf8()).left(16));
}

QString new_untitled_draft_id() {
  const quint64 bits = QRandomGenerator::system()->generate64();
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(bits));
  return QStringLiteral("u-") + QString::fromLatin1(hex);
}

bool is_draft_id(const QString& id) {
  if (id.isEmpty() || id.size() > 40) return false;
  for (const QChar qc : id) {
    const uint u = qc.unicode();
    const bool ok = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-';
    if (!ok) return false;
  }
  return true;
}

QByteArray encode_draft(const Draft& d) {
  QByteArray out = QByteArray(kMagic) + '\n';
  out += d.untitled ? "kind=untitled\n" : "kind=path\n";
  if (!d.untitled) {
    out += "path=" + QUrl::toPercentEncoding(d.path, "/") + '\n';
    out += "saved-sha256=" + d.saved_sha256 + '\n';
  }
  out += "size=" + QByteArray::number(static_cast<qint64>(d.text.size())) + '\n';
  out += "text-sha256=" + sha256_hex(d.text) + '\n';
  out += '\n';
  out += d.text;
  return out;
}

DraftRead decode_draft(const QByteArray& bytes) {
  const auto unreadable = [&] {
    DraftRead r;
    r.state = DraftState::unreadable;
    r.bytes = bytes;
    return r;
  };

  const qsizetype blank = bytes.indexOf("\n\n");
  if (blank < 0 || blank >= kMaxHeaderScan) return unreadable();

  const QByteArray header = bytes.left(blank);
  const QByteArray body = bytes.mid(blank + 2);
  const QList<QByteArray> lines = header.split('\n');
  if (lines.isEmpty() || lines.first() != kMagic) return unreadable();

  std::vector<std::pair<QByteArray, QByteArray>> fields;
  const auto field = [&](const char* key) -> const QByteArray* {
    for (const auto& [k, v] : fields) {
      if (k == key) return &v;
    }
    return nullptr;
  };
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const qsizetype eq = lines[i].indexOf('=');
    if (eq < 0) return unreadable();
    fields.emplace_back(lines[i].left(eq), lines[i].mid(eq + 1));
  }

  const QByteArray* kind = field("kind");
  const QByteArray* size_field = field("size");
  const QByteArray* text_hash = field("text-sha256");
  if (!kind || !size_field || !text_hash) return unreadable();

  Draft draft;
  if (*kind == "path") {
    draft.untitled = false;
    const QByteArray* path_field = field("path");
    const QByteArray* saved_hash = field("saved-sha256");
    if (!path_field || !saved_hash || !is_hex64(*saved_hash)) return unreadable();
    draft.path = QUrl::fromPercentEncoding(*path_field);
    draft.saved_sha256 = *saved_hash;
  } else if (*kind == "untitled") {
    draft.untitled = true;
  } else {
    return unreadable();
  }

  if (!is_decimal(*size_field) || !is_hex64(*text_hash)) return unreadable();
  bool ok = false;
  const qint64 declared_size = size_field->toLongLong(&ok);
  if (!ok) return unreadable();

  draft.text = body;
  DraftRead result;
  result.draft = std::move(draft);
  result.state = (body.size() == declared_size && sha256_hex(body) == *text_hash) ? DraftState::ok : DraftState::damaged;
  return result;
}

bool write_draft(const QString& dir, const QString& id, const Draft& d) {
  ensure_drafts_dir(dir);
  const QByteArray bytes = encode_draft(d);
  const QString path = draft_file(dir, id);
  QSaveFile f(path);
  bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
  if (ok) set_file_mode(path);
  lumos::log(lumos::LogLevel::debug, kComp, "draft_write",
             {{"id", id.toStdString()}, {"bytes", std::to_string(bytes.size())}, {"ok", ok ? "1" : "0"}});
  return ok;
}

DraftRead read_draft(const QString& dir, const QString& id) {
  const QString path = draft_file(dir, id);
  QFile f(path);
  DraftRead result;
  if (!f.open(QIODevice::ReadOnly)) {
    lumos::log(lumos::LogLevel::debug, kComp, "draft_read", {{"id", id.toStdString()}, {"state", "missing"}});
    return result;
  }
  const QByteArray bytes = f.readAll();
  result = decode_draft(bytes);
  result.written = QFileInfo(f).lastModified();
  lumos::log(lumos::LogLevel::debug, kComp, "draft_read", {{"id", id.toStdString()}, {"state", state_name(result.state)}});
  return result;
}

bool remove_draft(const QString& dir, const QString& id) {
  const QString path = draft_file(dir, id);
  if (QFile::exists(path)) QFile::remove(path);
  const bool ok = !QFile::exists(path);
  lumos::log(lumos::LogLevel::debug, kComp, "draft_remove", {{"id", id.toStdString()}, {"ok", ok ? "1" : "0"}});
  return ok;
}

QStringList list_draft_ids(const QString& dir) {
  std::vector<std::pair<QDateTime, QString>> found;
  const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files);
  for (const QFileInfo& info : entries) {
    const QString name = info.fileName();
    if (!name.endsWith(QLatin1String(kDraftSuffix))) continue;
    const QString id = name.left(name.size() - static_cast<int>(std::string_view(kDraftSuffix).size()));
    if (!is_draft_id(id)) continue;
    found.emplace_back(info.lastModified(), id);
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  QStringList ids;
  for (const auto& [when, id] : found) ids.append(id);
  return ids;
}

QStringList path_draft_ids(const QString& dir, const QString& path) {
  const QString base = path_draft_id(path);
  QStringList ids;
  for (const QString& id : list_draft_ids(dir)) {
    if (id == base) {
      ids.append(id);
      continue;
    }
    if (!id.startsWith(base + QLatin1Char('-'))) continue;
    bool ok = false;
    const int n = id.mid(base.size() + 1).toInt(&ok);
    if (ok && n >= 2 && id == path_draft_id_n(base, n)) ids.append(id);
  }
  return ids;
}

std::unique_ptr<QLockFile> lock_draft(const QString& dir, const QString& id) {
  ensure_drafts_dir(dir);
  auto lock = std::make_unique<QLockFile>(QDir(dir).filePath(id + QStringLiteral(".lock")));
  lock->setStaleLockTime(0);
  if (!lock->tryLock(0)) return nullptr;
  return lock;
}

QString claim_path_draft_id(const QString& dir, const QString& path, std::unique_ptr<QLockFile>* lock) {
  const QString base = path_draft_id(path);
  for (int n = 1; n <= kMaxPathDrafts; ++n) {
    const QString id = path_draft_id_n(base, n);
    if (std::unique_ptr<QLockFile> l = lock_draft(dir, id)) {
      if (lock) *lock = std::move(l);
      return id;
    }
  }
  lumos::log(lumos::LogLevel::debug, kComp, "draft_claim_failed", {{"tries", std::to_string(kMaxPathDrafts)}});
  return QString();
}
