#pragma once

#include <QByteArray>
#include <QLockFile>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>

class FindBar;
class QTimer;

namespace lumos::qt {
class SourceEditor;
}

class DocumentTab : public QWidget {
 public:
  explicit DocumentTab(QWidget* parent = nullptr);
  ~DocumentTab() override;

  lumos::qt::SourceEditor* editor() const { return editor_; }
  FindBar* find_bar() const { return find_bar_; }

  const QString& path() const { return path_; }
  const QByteArray& saved_sha256() const { return saved_sha256_; }
  QString name() const;
  bool is_blank() const;

  bool load(const QString& path, bool keep_position, QString* error);
  bool write(const QString& path, QString* error);
  void set_path(const QString& path);

  void set_link_open_failed(std::function<void(const QString&)> callback);

  void set_drafts_dir(const QString& dir);
  void set_draft_changed(std::function<void()> callback);
  void set_draft_failed(std::function<void()> callback);
  const QString& draft_id() const { return draft_id_; }
  void write_draft_now();
  void drop_draft();
  bool adopt_draft(const QString& id, std::unique_ptr<QLockFile> lock);

 private:
  void load_image(const std::string& url);
  void update_marks();
  void clear_draft();

  lumos::qt::SourceEditor* editor_ = nullptr;
  FindBar* find_bar_ = nullptr;
  QString path_;
  std::function<void(const QString&)> link_open_failed_;

  QString drafts_dir_;
  QString draft_id_;
  std::unique_ptr<QLockFile> draft_lock_;
  QTimer* draft_timer_ = nullptr;
  QByteArray saved_sha256_;
  bool drafts_off_ = false;
  std::function<void()> draft_changed_;
  std::function<void()> draft_failed_;
};
