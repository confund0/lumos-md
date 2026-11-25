#pragma once

#include <QByteArray>
#include <QLockFile>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>

#include "file_watch.hpp"

class FindBar;
class QTimer;

namespace lumos {
struct LinkTarget;
}

namespace lumos::qt {
class SourceEditor;
}

class DocumentTab : public QWidget {
 public:
  explicit DocumentTab(QWidget* parent = nullptr);
  ~DocumentTab() override;

  lumos::qt::SourceEditor* editor() const { return editor_; }
  FindBar* find_bar() const { return find_bar_; }

  quint64 buffer_id() const { return buffer_id_; }
  void set_buffer_id(quint64 id) { buffer_id_ = id; }

  const QString& path() const { return path_; }
  const QStringList& text_sources() const { return text_sources_; }
  const QByteArray& saved_sha256() const { return saved_sha256_; }
  const QByteArray& base() const { return base_; }
  const QByteArray& kept_sha256() const { return kept_sha256_; }
  void set_kept_sha256(const QByteArray& sha256) { kept_sha256_ = sha256; }
  DiskState disk_pending() const { return disk_pending_; }
  void set_disk_pending(DiskState state) { disk_pending_ = state; }
  bool gone_kept() const { return gone_kept_; }
  void set_gone_kept(bool kept) { gone_kept_ = kept; }
  bool read_only_file() const { return read_only_file_; }
  bool update_read_only_file();
  QString name() const;
  bool is_blank() const;

  bool load(const QString& path, bool keep_position, QString* error);
  bool write(const QString& path, QString* error);
  void set_path(const QString& path);

  void open_link(const lumos::LinkTarget& target);

  void set_link_open_failed(std::function<void(const QString&)> callback);

  void set_drafts_dir(const QString& dir);
  void set_draft_changed(std::function<void()> callback);
  void set_draft_failed(std::function<void()> callback);
  const QString& draft_id() const { return draft_id_; }
  void write_draft_now();
  void drop_draft();
  bool adopt_draft(const QString& id, std::unique_ptr<QLockFile> lock);

  void reload_from_disk(const QByteArray& bytes);

  void use_merge(const QByteArray& result, const QByteArray& seen);

 private:
  void load_image(const std::string& url);
  void update_marks();
  void clear_draft();

  lumos::qt::SourceEditor* editor_ = nullptr;
  FindBar* find_bar_ = nullptr;
  quint64 buffer_id_ = 0;
  QString path_;
  QStringList text_sources_;
  std::function<void(const QString&)> link_open_failed_;

  QString drafts_dir_;
  QString draft_id_;
  std::unique_ptr<QLockFile> draft_lock_;
  QTimer* draft_timer_ = nullptr;
  QByteArray base_;
  QByteArray saved_sha256_;
  QByteArray kept_sha256_;
  DiskState disk_pending_ = DiskState::same;
  bool gone_kept_ = false;
  bool read_only_file_ = false;
  bool drafts_off_ = false;
  std::function<void()> draft_changed_;
  std::function<void()> draft_failed_;
};
