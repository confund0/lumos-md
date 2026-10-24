#pragma once

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QList>
#include <QMimeData>
#include <QRect>
#include <QString>
#include <QTextLayout>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/qt/style_table.hpp"
#include "lumos/qt/text_map.hpp"

namespace lumos::qt {

class SourceEditor : public QAbstractScrollArea {
  Q_OBJECT

 public:
  explicit SourceEditor(QWidget* parent = nullptr);
  explicit SourceEditor(const Dialect& dialect, QWidget* parent = nullptr);
  ~SourceEditor() override;

  void set_text(std::string text, bool keep_position = false);
  void wipe();
  std::string text() const { return doc_.text(); }
  std::size_t length() const { return doc_.size(); }
  void replace_text(std::string_view text);
  Document& document() { return doc_; }
  const Document& document() const { return doc_; }

  bool is_modified() const { return doc_.modified(); }
  void mark_saved();

  bool canUndo() const;
  bool canRedo() const;

  bool is_read_only() const { return read_only_; }
  void set_read_only(bool on);

  std::size_t cursor_position() const { return cursor_; }
  std::size_t anchor_position() const { return anchor_; }
  bool has_selection() const { return cursor_ != anchor_; }
  std::string selected_text() const;
  void set_cursor_position(std::size_t pos, bool keep_anchor = false);
  void select(std::size_t anchor, std::size_t cursor);

  void insert_text(std::string_view utf8);

  Selection selection() const { return {anchor_, cursor_}; }
  const std::string& newline() const { return newline_; }
  bool apply_command(const std::optional<CommandResult>& result);

  void set_marks(int layer, std::vector<ByteRange> ranges, const QTextCharFormat& format);
  std::vector<ByteRange> marks(int layer) const;

  std::size_t offset_at(QPoint point) const;

  const StyleTable& style_table() const { return styles_; }
  void set_style_table(StyleTable table);

  int max_text_width() const { return max_text_width_; }
  void set_max_text_width(int chars);
  bool page_centred() const { return page_centred_; }
  void set_page_centred(bool on);
  bool line_numbers() const { return line_numbers_; }
  void set_line_numbers(bool on);

  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

  QRect cursor_rect() const;
  std::size_t line_count() const { return lines_.size(); }
  int visual_line_count() const;
  QString line_text(std::size_t line) const;
  QList<QTextLayout::FormatRange> line_formats(std::size_t line) const;

 public slots:
  void undo();
  void redo();
  void cut();
  void copy();
  void paste();
  void selectAll();

 signals:
  void textChanged();
  void modificationChanged(bool modified);
  void cursorPositionChanged();
  void canUndoChanged(bool can);
  void canRedoChanged(bool can);

 protected:
  bool event(QEvent* e) override;
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void changeEvent(QEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void inputMethodEvent(QInputMethodEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;
  void timerEvent(QTimerEvent* e) override;
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dragLeaveEvent(QDragLeaveEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  void scrollContentsBy(int dx, int dy) override;
  bool focusNextPrevChild(bool next) override;

 private:
  struct Line {
    LineSpan span;
    Utf16Map map;
    QList<QTextLayout::FormatRange> formats;
    bool background = false;
    std::unique_ptr<QTextLayout> layout;  
    int y = 0;
    int height = 0;
  };
  enum class Group { none, typing, deleting };
  enum class Move { left, right, word_left, word_right, up, down, line_start, line_end, page_up, page_down,
                    doc_start, doc_end };

  void on_change(const Change& change);
  void rebuild_lines();
  void update_lines(const TextRegion& region);
  void restyle();
  void restyle(std::size_t first, std::size_t last);
  void ensure_layout() const;
  void layout_line(std::size_t index) const;
  void update_scrollbar() const;
  void check_modified();
  void check_undo_state();
  void use_default_styles();

  std::size_t line_index(std::size_t pos) const;
  std::size_t line_at_y(int y) const;
  int to_utf16(const Line& line, std::size_t pos) const;
  std::size_t snap(std::size_t pos) const;
  std::size_t hit_test(QPointF doc_point) const;
  std::size_t move_target(Move move, qreal* x = nullptr);
  QRectF caret_doc_rect() const;
  int scroll_y() const;
  int gutter_width() const;
  int page_left() const;
  qreal left_margin() const;
  int layout_width() const;
  int top_space() const;

  void move_cursor(std::size_t pos, bool keep_anchor);
  void ensure_cursor_visible();
  void replace(std::size_t from, std::size_t to, std::string_view text);
  Selection renumber(const ListShape& before, std::size_t from, std::size_t to, Selection sel);
  void type_text(std::string text);
  void delete_range(std::size_t from, std::size_t to, Group group);
  void begin_group(Group group);
  void close_group();
  std::string normalize_newlines(std::string text) const;
  void select_word_at(std::size_t pos);
  void restart_blink();
  void clear_preedit();
  void update_preedit_line();
  void move_marks(const Change& change);
  void relayout(const std::vector<ByteRange>& ranges);
  void navigate(Move move, bool keep_anchor);

  void start_drag();
  std::optional<std::string> drop_mime_text(const QMimeData& mime) const;
  void update_autoscroll(QPointF viewport_pos);

  Document doc_;
  StyleTable styles_;
  bool custom_styles_ = false;
  int max_text_width_ = 100;
  bool line_numbers_ = true;
  bool page_centred_ = true;
  struct MarkLayer {
    std::vector<ByteRange> ranges;  
    QTextCharFormat format;
  };
  std::map<int, MarkLayer> marks_;
  mutable std::vector<Line> lines_;
  mutable bool layout_dirty_ = true;
  mutable int layout_width_ = -1;
  mutable int total_height_ = 0;

  std::size_t cursor_ = 0;
  std::size_t anchor_ = 0;
  qreal desired_x_ = -1;  
  std::string newline_ = "\n";  

  Group group_ = Group::none;
  std::uint64_t group_version_ = 0;  
  std::size_t group_end_ = 0;
  bool group_last_space_ = false;

  bool modified_ = false;
  bool can_undo_ = false;
  bool can_redo_ = false;
  bool read_only_ = false;
  bool caret_on_ = true;
  bool selecting_ = false;
  qint64 double_click_ms_ = -1;
  QPointF double_click_at_;
  QBasicTimer blink_;

  bool drag_candidate_ = false;
  QPointF drag_press_pos_;           
  std::size_t drag_press_doc_pos_ = 0;  
  std::optional<ByteRange> drag_source_;
  QBasicTimer autoscroll_;
  QPointF autoscroll_pos_;  
  std::size_t drop_pos_ = SIZE_MAX;

  QString preedit_;
  QList<QTextLayout::FormatRange> preedit_formats_;
  int preedit_cursor_ = 0;
  bool preedit_cursor_visible_ = true;
  std::size_t preedit_line_ = SIZE_MAX;
};

}  
