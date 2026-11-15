#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <string>

class FindBar;

namespace lumos::qt {
class SourceEditor;
}

class DocumentTab : public QWidget {
 public:
  explicit DocumentTab(QWidget* parent = nullptr);

  lumos::qt::SourceEditor* editor() const { return editor_; }
  FindBar* find_bar() const { return find_bar_; }

  const QString& path() const { return path_; }
  QString name() const;
  bool is_blank() const;

  bool load(const QString& path, bool keep_position, QString* error);
  bool write(const QString& path, QString* error);
  void set_path(const QString& path);

  void set_link_open_failed(std::function<void(const QString&)> callback);

 private:
  void load_image(const std::string& url);
  void update_marks();

  lumos::qt::SourceEditor* editor_ = nullptr;
  FindBar* find_bar_ = nullptr;
  QString path_;
  std::function<void(const QString&)> link_open_failed_;
};
