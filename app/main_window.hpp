#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QList>
#include <QString>

#include <optional>

#include "lumos/commands.hpp"

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QTimer;
class QToolButton;
class FindBar;

namespace lumos::qt {
class SourceEditor;
}

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(const QString& settings_file);

  bool load(const QString& path, bool keep_position = false);

 protected:
  void closeEvent(QCloseEvent* e) override;

 private:
  void new_file();
  void open_dialog();
  void open_recent(const QString& path);
  void fill_recent_menu();
  void set_recent_files(const QStringList& list);
  void revert();
  void about();
  void update_actions();
  void update_marks();
  void update_position();
  void update_words();
  void context_menu(const QPoint& at);
  void toggle(lumos::InlineKind kind);
  void toggle(lumos::BlockKind kind);
  void set_heading(int level);
  void insert_table(int rows, int cols);
  void table_move(std::optional<lumos::CommandResult> result);
  void toggle_link();
  void insert_picture();
  void show_properties();
  bool save();
  bool save_as();
  bool write(const QString& path);
  bool maybe_save();
  void update_title();
  void set_paper_look(bool on);
  void choose_font();
  void set_zoom(double zoom);
  void apply_font();
  bool eventFilter(QObject* watched, QEvent* e) override;

  QSettings settings_;
  QString font_family_;
  double font_size_ = 0;  
  double zoom_ = 1.0;
  int wheel_delta_ = 0;  
  bool paper_look_ = false;
  QAction* zoom_in_ = nullptr;
  QAction* zoom_out_ = nullptr;
  QAction* zoom_reset_ = nullptr;
  QLabel* zoom_label_ = nullptr;
  lumos::qt::SourceEditor* editor_ = nullptr;
  FindBar* find_bar_ = nullptr;
  QAction* undo_ = nullptr;
  QAction* redo_ = nullptr;
  QAction* cut_ = nullptr;
  QAction* copy_ = nullptr;
  QAction* paste_ = nullptr;
  QAction* read_only_ = nullptr;
  QAction* revert_ = nullptr;
  QMenu* recent_menu_ = nullptr;
  QAction* bold_ = nullptr;
  QAction* italic_ = nullptr;
  QAction* strike_ = nullptr;
  QAction* code_ = nullptr;
  QAction* link_ = nullptr;
  QAction* picture_ = nullptr;
  QComboBox* heading_ = nullptr;
  QAction* bullet_ = nullptr;
  QAction* ordered_ = nullptr;
  QAction* task_ = nullptr;
  QAction* quote_ = nullptr;
  QAction* code_block_ = nullptr;
  QAction* thematic_break_ = nullptr;
  QAction* renumber_ = nullptr;
  QToolButton* table_button_ = nullptr;
  QAction* table_action_ = nullptr;
  QAction* format_table_ = nullptr;
  QList<QAction*> table_moves_;
  QLabel* block_state_ = nullptr;
  QLabel* position_label_ = nullptr;
  QLabel* words_label_ = nullptr;
  QTimer* words_timer_ = nullptr;
  QString path_;
};
