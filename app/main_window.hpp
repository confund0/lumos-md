#pragma once

#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QString>

#include <functional>
#include <optional>

#include "lumos/commands.hpp"

class DocumentTab;
class QAction;
class QComboBox;
class QFont;
class QLabel;
class QMenu;
class QTabWidget;
class QTimer;
class QToolButton;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(const QString& settings_file, const QString& open_tabs_file = QString());

  bool open_file(const QString& path);
  void restore_tabs();
  void new_tab();
  bool close_tab(int index);
  void reopen_closed_tab();
  void step_tab(int step);
  int tab_count() const;
  DocumentTab* current_tab() const;
  DocumentTab* tab_at(int index) const;
  const QStringList& closed_tabs() const { return closed_tabs_; }

 protected:
  void closeEvent(QCloseEvent* e) override;

 private:
  bool open_path(const QString& path, bool quiet);
  void update_closed_tabs_actions();
  void open_dialog();
  void open_recent(const QString& path);
  void fill_recent_menu();
  void set_recent_files(const QStringList& list);
  void revert();
  void about();
  void update_actions();
  void update_position();
  void update_words();
  void context_menu(DocumentTab* tab, const QPoint& at);
  void toggle(lumos::InlineKind kind);
  void toggle(lumos::BlockKind kind);
  void set_heading(int level);
  void insert_table(int rows, int cols);
  void table_move(std::optional<lumos::CommandResult> result);
  void toggle_link();
  void insert_picture();
  void show_properties();
  bool save(DocumentTab* tab);
  bool save_as(DocumentTab* tab);
  bool write_tab(DocumentTab* tab, const QString& path);
  bool maybe_save(DocumentTab* tab);
  void update_title();
  void set_paper_look(bool on);
  void choose_font();
  void set_zoom(double zoom);
  void apply_font();
  bool eventFilter(QObject* watched, QEvent* e) override;
  QFont current_font() const;
  DocumentTab* make_tab();
  void for_each_tab(const std::function<void(DocumentTab*)>& fn);
  void update_tab_text(DocumentTab* tab);
  void refresh_for_current_tab();
  void fill_window_menu();

  QSettings settings_;
  QString open_tabs_file_;
  QStringList closed_tabs_;
  QToolButton* reopen_button_ = nullptr;
  QAction* reopen_action_ = nullptr;
  QString font_family_;
  double font_size_ = 0;  
  double zoom_ = 1.0;
  int wheel_delta_ = 0;  
  bool paper_look_ = false;
  bool line_numbers_ = true;
  bool limit_width_ = true;
  bool accept_drops_ = true;
  QTabWidget* tabs_ = nullptr;
  QMenu* window_menu_ = nullptr;
  QList<QAction*> window_tab_actions_;
  QAction* zoom_in_ = nullptr;
  QAction* zoom_out_ = nullptr;
  QAction* zoom_reset_ = nullptr;
  QLabel* zoom_label_ = nullptr;
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
};
