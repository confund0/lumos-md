#pragma once

#include <QWidget>

#include <optional>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/types.hpp"

class QLineEdit;
class QCheckBox;
class QLabel;
class QToolButton;
class QPushButton;
class QKeyEvent;

namespace lumos::qt {
class SourceEditor;
}

class FindBar : public QWidget {
 public:
  explicit FindBar(lumos::qt::SourceEditor* editor, QWidget* parent = nullptr);

  void open_find();
  void open_replace();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  lumos::FindOptions options() const;
  void run_search();
  void move_to(bool forward);
  void do_replace();
  void do_replace_all();
  void on_document_changed();
  void close_bar();
  void recompute_matches();
  std::optional<std::size_t> nearest(std::size_t pos, bool forward, bool wrap) const;
  void update_marks();
  void update_count_label();

  lumos::qt::SourceEditor* editor_ = nullptr;

  QLineEdit* pattern_ = nullptr;
  QLineEdit* replacement_ = nullptr;
  QCheckBox* case_sensitive_ = nullptr;
  QCheckBox* whole_word_ = nullptr;
  QCheckBox* regex_ = nullptr;
  QToolButton* previous_ = nullptr;
  QToolButton* next_ = nullptr;
  QPushButton* replace_ = nullptr;
  QPushButton* replace_all_ = nullptr;
  QLabel* count_ = nullptr;
  QToolButton* close_ = nullptr;

  std::vector<lumos::ByteRange> matches_;
  std::optional<std::size_t> current_;
};
