#pragma once

#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "lumos/commands.hpp"

class DocumentTab;
class McpServer;
class PreviewSplitter;
class PreviewView;
class QAction;
class QComboBox;
class QFileSystemWatcher;
class QFont;
class QFrame;
class QLabel;
class QMenu;
class QTabBar;
class QTabWidget;
class QTimer;
class QToolButton;

struct BufferInfo {
  quint64 id = 0;
  QString path;  
  QString name;  
  bool modified = false;
  bool current = false;
  QStringList sources;  
};

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(const QString& settings_file, const QString& open_tabs_file = QString(),
                       const QString& drafts_dir = QString());
  ~MainWindow() override;

  bool open_file(const QString& path);
  void restore_tabs();
  void open_from_other_start(const QStringList& paths, const QString& activation_token = {});
  void new_tab();
  bool close_tab(int index);
  void reopen_closed_tab();
  void step_tab(int step);
  int tab_count() const;
  DocumentTab* current_tab() const;
  DocumentTab* tab_at(int index) const;
  const QStringList& closed_tabs() const { return closed_tabs_; }
  QTabWidget* tabs_for_tests() const { return tabs_; }
  PreviewView* preview_for_tests() const { return preview_; }
  PreviewSplitter* preview_splitter_for_tests() const { return splitter_; }
  void check_files();

  QList<BufferInfo> list_buffers() const;
  std::optional<QByteArray> read_buffer(quint64 id) const;
  std::optional<quint64> find_buffer(const QString& path) const;
  std::optional<quint64> open_buffer(const QString& path, QString* error);

  void set_mcp_server_name(const QString& name);
  bool mcp_listening() const;

  QStringList mcp_allowed_folders() const;
  QStringList set_mcp_allowed_folders(const QStringList& folders);
  QStringList mcp_effective_folders() const;

 protected:
  void closeEvent(QCloseEvent* e) override;
  void changeEvent(QEvent* e) override;

 private:
  bool open_path(const QString& path, bool quiet);
  DocumentTab* load_path_tab(const QString& path, bool* loaded, QString* error, bool add_recent = true);
  void open_untitled_draft(const QString& id);
  void offer_path_draft(DocumentTab* tab);
  void drain_other_start_queue();
  void schedule_other_start_drain(int delay_ms);
  void schedule_tabs_write();
  void write_tabs_now();
  void update_closed_tabs_actions();
  void open_dialog();
  void open_recent(const QString& path);
  void fill_recent_menu();
  void set_recent_files(const QStringList& list);
  void revert();
  enum class NoticeKind { info, warning };
  void show_notice(NoticeKind kind, const QString& text);
  void sync_watch();
  void handle_current();
  enum class ChangedAnswer { saved, merged, loaded, cancel };
  ChangedAnswer ask_changed(DocumentTab* tab, bool from_save);
  void open_help(const QString& name);
  void update_actions();
  void update_position();
  void update_words();
  void context_menu(DocumentTab* tab, const QPoint& at);
  void tab_context_menu(QTabBar* bar, const QPoint& at);
  void toggle(lumos::InlineKind kind);
  void toggle(lumos::BlockKind kind);
  void set_heading(int level);
  void insert_table(int rows, int cols);
  void table_move(const std::optional<lumos::CommandResult>& result);
  void paste_from_html();
  void toggle_link();
  void insert_picture();
  void show_properties();
  bool save(DocumentTab* tab);
  bool save_as(DocumentTab* tab);
  bool write_tab(DocumentTab* tab, const QString& path);
  bool maybe_save(DocumentTab* tab);
  void update_title();
  bool paper_look_shown() const;
  void apply_paper_look();
  void set_theme(const QString& id);
  void choose_font();
  void set_zoom(double zoom);
  void apply_font();
  void apply_preview_look();
  void set_preview_shown(bool on);
  void render_preview();
  void on_preview_size_changed();
  bool eventFilter(QObject* watched, QEvent* e) override;
  QFont current_font() const;
  DocumentTab* make_tab();
  DocumentTab* tab_by_id(quint64 id) const;
  void for_each_tab(const std::function<void(DocumentTab*)>& fn);
  void update_tab_text(DocumentTab* tab);
  void refresh_for_current_tab();
  void fill_window_menu();
  void update_mcp_server();
  QString mcp_listen_name() const;
  void set_mcp_use_socket_dir(bool on);
  void choose_mcp_socket_dir();
  void refresh_mcp_socket_actions();
  QStringList mcp_protected_dirs() const;
  bool mcp_folder_holds_protected(const QString& folder) const;
  void choose_mcp_allowed_folders();
  void refresh_mcp_status();

  QSettings settings_;
  QString open_tabs_file_;
  QString drafts_dir_;
  QStringList closed_tabs_;
  quint64 next_buffer_id_ = 1;
  QToolButton* reopen_button_ = nullptr;
  QAction* reopen_action_ = nullptr;
  QStringList other_start_queue_;
  QString other_start_token_;
  bool other_start_draining_ = false;
  bool other_start_retry_pending_ = false;
  bool tabs_write_pending_ = false;
  bool tabs_closed_ = false;  
  QString font_family_;
  double font_size_ = 0;  
  double zoom_ = 1.0;
  int wheel_delta_ = 0;  
  bool paper_look_ = false;  
  QAction* paper_look_action_ = nullptr;
  QString theme_;  
  bool line_numbers_ = true;
  bool limit_width_ = true;
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
  QAction* paste_html_ = nullptr;
  bool clipboard_html_ = false;
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
  QLabel* path_label_ = nullptr;  
  QLabel* position_label_ = nullptr;
  QLabel* words_label_ = nullptr;
  QTimer* words_timer_ = nullptr;
  QLabel* notice_label_ = nullptr;
  QTimer* notice_timer_ = nullptr;
  QFileSystemWatcher* file_watcher_ = nullptr;
  QTimer* watch_timer_ = nullptr;
  bool disk_dialog_open_ = false;
  QString mcp_server_name_;
  bool mcp_enabled_ = false;
  QString mcp_socket_dir_;
  bool mcp_use_socket_dir_ = false;
  QAction* mcp_default_dir_action_ = nullptr;
  QAction* mcp_picked_dir_action_ = nullptr;
  QStringList mcp_allowed_folders_;
  QFrame* mcp_status_frame_ = nullptr;
  QLabel* mcp_status_label_ = nullptr;
  std::unique_ptr<McpServer> mcp_server_;
  PreviewSplitter* splitter_ = nullptr;
  PreviewView* preview_ = nullptr;
  QWidget* preview_box_ = nullptr;  
  QAction* show_preview_ = nullptr;
  QTimer* preview_timer_ = nullptr;
  bool preview_stale_ = true;
};
