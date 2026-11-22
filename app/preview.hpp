#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTextBrowser>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/md/html_renderer.hpp"
#include "lumos/tools.hpp"

class QMenu;
class QToolButton;

class PreviewView : public QTextBrowser {
 public:
  explicit PreviewView(QWidget* parent = nullptr);

  void show_document(const lumos::Document& doc, const QString& note_path);
  void scroll_to_offset(std::size_t offset);
  void set_link_clicked(std::function<void(const lumos::LinkTarget&)> callback);
  void set_look(const QColor& background, const QColor& lines, const QColor& code);

  int render_count() const { return render_count_; }
  const std::vector<std::pair<std::size_t, int>>& anchors();

  QMenu* make_context_menu();

 protected:
  QVariant loadResource(int type, const QUrl& name) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  std::optional<std::string> resolve_url(lumos::md::UrlKind kind, std::string_view url);
  void on_anchor_clicked(const QUrl& url);
  void rebuild_anchors();

  QString note_path_;
  std::vector<std::string> links_;
  std::function<void(const lumos::LinkTarget&)> link_clicked_;
  int render_count_ = 0;
  std::size_t note_size_ = 0;
  std::vector<std::pair<std::size_t, int>> anchors_;
  bool anchors_stale_ = true;
  struct CachedImage {
    QDateTime modified;
    QImage image;
  };
  std::map<QString, CachedImage> image_cache_;
};

class PreviewSplitterHandle;

class PreviewSplitter : public QSplitter {
 public:
  PreviewSplitter(QWidget* editor_side, QWidget* preview_side, QWidget* parent = nullptr);

  void show_preview_full();
  void show_editor_full();
  void restore_split();
  double preview_share() const { return share_; }
  void set_preview_share(double share);
  bool preview_full() const;
  bool editor_full() const;

  void set_changed(std::function<void()> callback);

 protected:
  QSplitterHandle* createHandle() override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void remember_split();
  void apply_share();
  void notify();

  PreviewSplitterHandle* handle_ = nullptr;
  double share_ = 0.5;
  std::function<void()> changed_;
};

class PreviewSplitterHandle : public QSplitterHandle {
 public:
  PreviewSplitterHandle(Qt::Orientation orientation, QSplitter* parent);

  void refresh();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  QToolButton* preview_full_button_;
  QToolButton* editor_full_button_;
};
