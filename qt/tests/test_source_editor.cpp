
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMetaObject>
#include <QMimeData>
#include <QPalette>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <cmath>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/log.hpp"
#include "lumos/parser.hpp"
#include "lumos/qt/source_editor.hpp"
#include "lumos/qt/text_map.hpp"

using lumos::ByteRange;
using lumos::Edit;
using lumos::InlineKind;
using lumos::format_at;
using lumos::toggle_inline;
using lumos::qt::LineSpan;
using lumos::qt::SourceEditor;
using lumos::qt::Utf16Map;

namespace {

std::string read_file(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QByteArray data = f.readAll();
  return std::string(data.constData(), static_cast<std::size_t>(data.size()));
}

std::string sample_path() { return std::string(LUMOS_TEST_DATA_DIR) + "/sample.md"; }

QString qs(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

void type_key(QWidget* w, const QString& text) {
  QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, text);
  QApplication::sendEvent(w, &press);
}

void key(QWidget* w, Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier) { QTest::keyClick(w, k, mods); }

void check_editor(const SourceEditor& ed) {
  const std::string t = ed.text();
  QCOMPARE(ed.document().buffer().text(), t);
  const auto problem = ed.document().tree().validate(t.size());
  QVERIFY2(!problem, problem.value_or("").c_str());
  auto fresh = lumos::make_parser(ed.document().dialect());
  fresh->parse(t);
  QCOMPARE(ed.document().tree().to_debug_string(), fresh->tree().to_debug_string());
  const auto spans = lumos::qt::split_lines(t);
  QCOMPARE(ed.line_count(), spans.size());
  for (std::size_t i = 0; i < spans.size(); ++i)
    QCOMPARE(ed.line_text(i), Utf16Map(std::string_view(t).substr(spans[i].start, spans[i].length)).text());
  SourceEditor full(ed.document().dialect());
  full.set_text(t);
  for (std::size_t i = 0; i < spans.size(); ++i) {
    if (ed.line_formats(i) != full.line_formats(i)) QFAIL(qPrintable(QString("formats differ on line %1").arg(i)));
  }
}

}  

class TestSourceEditor : public QObject {
  Q_OBJECT

 private slots:
  void split_lines_handles_line_endings();
  void utf16_map_round_trips();
  void utf16_map_inside_code_points();
  void utf16_map_invalid_bytes();
  void load_save_keeps_bytes();
  void wipe_empties_the_editor();
  void typing_and_backspace_over_emoji();
  void delete_removes_graphemes();
  void undo_groups_typing();
  void enter_keeps_crlf();
  void navigation_keys();
  void selection_copy_cut_paste();
  void mouse_click_and_double_click();
  void home_end_on_rows();
  void modification_signals();
  void highlight_formats();
  void word_wrap_and_scrolling();
  void text_width_and_page();
  void bogus_average_width();
  void input_method();
  void random_edits_keep_lines();
  void performance_5000_lines();
  void commands_on_long_list();
  void restyle_each_line_once();
  void render_png();
  void slots_callable_by_name();
  void undo_redo_state();
  void set_text_contract();
  void replace_text_one_step();
  void restyle_emits_no_text_change();
  void undo_to_saved_state();
  void read_only();
  void shortcut_override();
  void window_shortcuts_reach_window();
  void custom_context_menu();
  void theme_from_palette();
  void two_widgets_share_no_state();
  void works_with_signals_blocked();
  void markdown_keys();
  void table_keys();
  void renumber_after_edits();
  void marks_move_with_edits();
  void apply_command_contract();
  void external_drop();
  void drop_ignored_read_only_or_disabled();
  void auto_scroll_on_drag_select();
  void logs_carry_no_text();
};

void TestSourceEditor::split_lines_handles_line_endings() {
  using V = std::vector<LineSpan>;
  QCOMPARE(lumos::qt::split_lines(""), (V{{0, 0, 0}}));
  QCOMPARE(lumos::qt::split_lines("a\nbc"), (V{{0, 1, 1}, {2, 2, 0}}));
  QCOMPARE(lumos::qt::split_lines("a\r\nb\r\n"), (V{{0, 1, 2}, {3, 1, 2}, {6, 0, 0}}));
  QCOMPARE(lumos::qt::split_lines("a\rb\n\r"), (V{{0, 3, 1}, {4, 1, 0}}));
  QCOMPARE(lumos::qt::split_lines("\r\n", 10), (V{{10, 0, 2}, {12, 0, 0}}));
}

void TestSourceEditor::utf16_map_round_trips() {
  struct Case {
    std::string text;
    std::vector<std::size_t> bytes;  
    std::vector<int> units;  
  };
  const std::vector<Case> cases = {
      {"abc", {0, 1, 2, 3}, {0, 1, 2, 3}},
      {"Příliš žluťoučký", {}, {}},
      {"a🎉b", {0, 1, 5, 6}, {0, 1, 3, 4}},
      {"日本語", {0, 3, 6, 9}, {0, 1, 2, 3}},
  };
  for (const Case& c : cases) {
    const Utf16Map map(c.text);
    QCOMPARE(map.text(), qs(c.text));
    QCOMPARE(map.byte_size(), c.text.size());
    for (std::size_t k = 0; k < c.bytes.size(); ++k) {
      QCOMPARE(map.to_utf16(c.bytes[k]), c.units[k]);
      QCOMPARE(map.to_utf8(c.units[k]), c.bytes[k]);
    }
    for (int pos = 0; pos <= map.size(); ++pos) {
      if (pos > 0 && pos < map.size() && map.text().at(pos).isLowSurrogate()) continue;
      QCOMPARE(map.to_utf16(map.to_utf8(pos)), pos);
    }
  }
  const Utf16Map czech("Příliš");
  QCOMPARE(czech.size(), 6);
  QCOMPARE(czech.to_utf8(2), std::size_t{3});  
  QCOMPARE(czech.to_utf16(9), 6);
  QCOMPARE(czech.to_utf16(8), 5);  
  QCOMPARE(czech.to_utf16(100), 6);
  QCOMPARE(czech.to_utf8(100), std::size_t{9});
  QCOMPARE(czech.to_utf8(-3), std::size_t{0});
}

void TestSourceEditor::utf16_map_inside_code_points() {
  const Utf16Map map("x🎉日");
  for (std::size_t b = 1; b < 5; ++b) QCOMPARE(map.to_utf16(b), 1);
  QCOMPARE(map.to_utf16(6), 3);
  QCOMPARE(map.to_utf16(7), 3);
  QCOMPARE(map.to_utf8(2), std::size_t{1});
  QCOMPARE(map.to_utf8(3), std::size_t{5});
  QCOMPARE(map.to_utf8(4), std::size_t{8});
}

void TestSourceEditor::utf16_map_invalid_bytes() {
  const std::string bad = std::string("a\x80") + "\xC0\xAF" + "\xE6\x97" + "b";
  const Utf16Map map(bad);
  QCOMPARE(map.size(), 7);
  QCOMPARE(map.text().at(1), QChar(QChar::ReplacementCharacter));
  QCOMPARE(map.to_utf8(6), std::size_t{6});
  QCOMPARE(map.to_utf8(7), bad.size());
  for (int pos = 0; pos <= map.size(); ++pos) QCOMPARE(map.to_utf16(map.to_utf8(pos)), pos);
}

void TestSourceEditor::load_save_keeps_bytes() {
  const std::string bytes = read_file(qs(sample_path()));
  QVERIFY(!bytes.empty());
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text(bytes);
  QCOMPARE(ed.text(), bytes);
  QVERIFY(!ed.is_modified());

  ed.set_cursor_position(10);
  type_key(&ed, "x");
  QVERIFY(ed.is_modified());
  ed.undo();
  QCOMPARE(ed.text(), bytes);
  QVERIFY(!ed.is_modified());

  const QString out = QDir(QDir::tempPath()).filePath("lumos-roundtrip.md");
  {
    QFile f(out);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(ed.text().data(), static_cast<qint64>(ed.text().size()));
  }
  QCOMPARE(read_file(out), bytes);
  QFile::remove(out);

  const std::string odd = std::string("a\r\nb\rc\xff\n\xe6\x97") + "\r\n";
  ed.set_text(odd);
  QCOMPARE(ed.text(), odd);
  check_editor(ed);
}

void TestSourceEditor::wipe_empties_the_editor() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("# Title\n\nsecret text\n");
  ed.set_cursor_position(15);
  type_key(&ed, "x");
  ed.wipe();
  QCOMPARE(ed.text(), std::string());
  QCOMPARE(ed.cursor_position(), std::size_t{0});
  QVERIFY(!ed.is_modified());
  check_editor(ed);
  type_key(&ed, "a");
  QCOMPARE(ed.text(), std::string("a"));
}

void TestSourceEditor::typing_and_backspace_over_emoji() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("");
  QTest::keyClicks(&ed, "Hi ");
  type_key(&ed, QString::fromUtf8("🎉"));
  type_key(&ed, QString::fromUtf8("č"));
  QCOMPARE(ed.text(), std::string("Hi 🎉č"));
  QCOMPARE(ed.cursor_position(), ed.text().size());
  key(&ed, Qt::Key_Backspace);
  QCOMPARE(ed.text(), std::string("Hi 🎉"));
  key(&ed, Qt::Key_Backspace);  
  QCOMPARE(ed.text(), std::string("Hi "));
  type_key(&ed, QString::fromUtf8("👨‍👩‍👧"));
  key(&ed, Qt::Key_Left);
  QCOMPARE(ed.cursor_position(), std::size_t{3});
  key(&ed, Qt::Key_End);
  key(&ed, Qt::Key_Backspace);
  QCOMPARE(ed.text(), std::string("Hi "));
  check_editor(ed);
}

void TestSourceEditor::delete_removes_graphemes() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("e\xCC\x81x\r\ny");  
  ed.set_cursor_position(0);
  key(&ed, Qt::Key_Delete);
  QCOMPARE(ed.text(), std::string("x\r\ny"));
  key(&ed, Qt::Key_End);
  QCOMPARE(ed.cursor_position(), std::size_t{1});
  key(&ed, Qt::Key_Delete);  
  QCOMPARE(ed.text(), std::string("xy"));
  key(&ed, Qt::Key_Backspace);
  QCOMPARE(ed.text(), std::string("y"));
  check_editor(ed);
}

void TestSourceEditor::undo_groups_typing() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("");
  QTest::keyClicks(&ed, "hello world");
  QCOMPARE(ed.text(), std::string("hello world"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("hello "));
  ed.undo();
  QCOMPARE(ed.text(), std::string(""));
  ed.redo();
  QCOMPARE(ed.text(), std::string("hello "));
  key(&ed, Qt::Key_Y, Qt::ControlModifier);
  QCOMPARE(ed.text(), std::string("hello world"));

  key(&ed, Qt::Key_Left);
  QTest::keyClicks(&ed, "XY");
  key(&ed, Qt::Key_End);
  key(&ed, Qt::Key_Backspace);
  key(&ed, Qt::Key_Backspace);
  QCOMPARE(ed.text(), std::string("hello worlX"));
  key(&ed, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(ed.text(), std::string("hello worlXYd"));
  key(&ed, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
  QCOMPARE(ed.text(), std::string("hello worlX"));
  key(&ed, Qt::Key_Z, Qt::ControlModifier);
  key(&ed, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(ed.text(), std::string("hello world"));
  check_editor(ed);
}

void TestSourceEditor::enter_keeps_crlf() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("a\r\nb");
  ed.set_cursor_position(1);
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.text(), std::string("a\r\n\r\nb"));
  key(&ed, Qt::Key_Tab);
  QCOMPARE(ed.text(), std::string("a\r\n\t\r\nb"));
  ed.set_text("a\nb");
  ed.set_cursor_position(1);
  key(&ed, Qt::Key_Enter);
  QCOMPARE(ed.text(), std::string("a\n\nb"));
  check_editor(ed);
}

void TestSourceEditor::navigation_keys() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("one two\r\nthree\nfour five");
  ed.set_cursor_position(0);
  key(&ed, Qt::Key_Right, Qt::ControlModifier);
  QVERIFY(ed.cursor_position() == 3 || ed.cursor_position() == 4);
  key(&ed, Qt::Key_End);
  QCOMPARE(ed.cursor_position(), std::size_t{7});  
  key(&ed, Qt::Key_Right);
  QCOMPARE(ed.cursor_position(), std::size_t{9});  
  key(&ed, Qt::Key_Left);
  QCOMPARE(ed.cursor_position(), std::size_t{7});
  key(&ed, Qt::Key_Down);
  QCOMPARE(ed.cursor_position(), std::size_t{14});  
  key(&ed, Qt::Key_Down);
  QVERIFY(ed.cursor_position() > 15);
  key(&ed, Qt::Key_Home);
  QCOMPARE(ed.cursor_position(), std::size_t{15});
  key(&ed, Qt::Key_Up);
  QCOMPARE(ed.cursor_position(), std::size_t{9});
  key(&ed, Qt::Key_End, Qt::ControlModifier);
  QCOMPARE(ed.cursor_position(), ed.text().size());
  key(&ed, Qt::Key_Left, Qt::ControlModifier);
  QCOMPARE(ed.cursor_position(), std::size_t{20});  
  key(&ed, Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
  QCOMPARE(ed.cursor_position(), std::size_t{0});
  QCOMPARE(ed.anchor_position(), std::size_t{20});
  key(&ed, Qt::Key_Left);
  QVERIFY(!ed.has_selection());
  key(&ed, Qt::Key_Right, Qt::ShiftModifier);
  key(&ed, Qt::Key_Right, Qt::ShiftModifier);
  QCOMPARE(ed.selected_text(), std::string("on"));
}

void TestSourceEditor::selection_copy_cut_paste() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("alpha beta\r\ngamma");
  ed.select(6, 10);
  QCOMPARE(ed.selected_text(), std::string("beta"));
  key(&ed, Qt::Key_C, Qt::ControlModifier);
  QCOMPARE(QApplication::clipboard()->text(), QString("beta"));
  ed.set_cursor_position(ed.text().size());
  key(&ed, Qt::Key_V, Qt::ControlModifier);
  QCOMPARE(ed.text(), std::string("alpha beta\r\ngammabeta"));
  QApplication::clipboard()->setText("x\ny");
  ed.paste();
  QCOMPARE(ed.text(), std::string("alpha beta\r\ngammabetax\r\ny"));
  ed.selectAll();
  QCOMPARE(ed.selected_text(), ed.text());
  ed.select(0, 6);
  key(&ed, Qt::Key_X, Qt::ControlModifier);
  QCOMPARE(ed.text(), std::string("beta\r\ngammabetax\r\ny"));
  QCOMPARE(QApplication::clipboard()->text(), QString("alpha "));
  ed.select(0, 4);
  type_key(&ed, QString::fromUtf8("ž"));
  QCOMPARE(ed.text(), std::string("ž\r\ngammabetax\r\ny"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("beta\r\ngammabetax\r\ny"));
  check_editor(ed);
}

void TestSourceEditor::mouse_click_and_double_click() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("first line\nsecond words here\nthird");
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  const int lh = QFontMetrics(ed.font()).height();
  const int left = ed.cursor_rect().x();
  const int top = ed.cursor_rect().y();
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(1, top + lh + lh / 2));
  QCOMPARE(ed.cursor_position(), std::size_t{11});
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(500, top + lh / 2));
  QCOMPARE(ed.cursor_position(), std::size_t{10});
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::ShiftModifier, QPoint(1, top + 2 * lh + lh / 2));
  QCOMPARE(ed.selected_text(), std::string("\nsecond words here\n"));
  const int x = left + QFontMetrics(ed.font()).horizontalAdvance("second w");
  QTest::mouseDClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2));
  QCOMPARE(ed.selected_text(), std::string("words"));
  QTest::mousePress(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(1, top + lh / 2));
  const QPointF end(500, top + lh / 2);
  QMouseEvent move(QEvent::MouseMove, end, ed.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &move);
  QTest::mouseRelease(ed.viewport(), Qt::LeftButton, Qt::NoModifier, end.toPoint());
  QCOMPARE(ed.selected_text(), std::string("first line"));
  QTest::mouseDClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2));
  QTest::mousePress(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2));
  QTest::mouseRelease(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2));
  QCOMPARE(ed.selected_text(), std::string("second words here\n"));
  QTest::mouseDClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(left + 2, top + 2 * lh + lh / 2));
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(left + 2, top + 2 * lh + lh / 2));
  QCOMPARE(ed.selected_text(), std::string("third"));
  QTest::mouseDClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2));
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, top + lh + lh / 2), 2000);
  QVERIFY(!ed.has_selection());
}

void TestSourceEditor::home_end_on_rows() {
  SourceEditor ed;
  ed.resize(300, 200);
  std::string text;
  for (int i = 0; i < 30; ++i) text += "word ";
  text += "\n" + std::string(200, 'x') + "\nend";
  ed.set_text(text);
  const std::size_t first_end = text.find('\n');
  key(&ed, Qt::Key_End);
  const std::size_t row_end = ed.cursor_position();
  QVERIFY(row_end > 0 && row_end < first_end);
  QCOMPARE(text[row_end], ' ');
  key(&ed, Qt::Key_End);
  QCOMPARE(ed.cursor_position(), row_end);
  ed.set_cursor_position(row_end + 2);
  key(&ed, Qt::Key_Home);
  QCOMPARE(ed.cursor_position(), row_end + 1);
  key(&ed, Qt::Key_End, Qt::ShiftModifier);
  const std::string row = ed.selected_text();
  QVERIFY(!row.empty() && row.find('\n') == std::string::npos);
  ed.set_cursor_position(first_end - 1);
  key(&ed, Qt::Key_End);
  QCOMPARE(ed.cursor_position(), first_end);
  key(&ed, Qt::Key_Home);
  QVERIFY(ed.cursor_position() > row_end && ed.cursor_position() < first_end);
  const std::size_t word = first_end + 1;
  ed.set_cursor_position(word);
  key(&ed, Qt::Key_End);
  const std::size_t word_end = ed.cursor_position();
  QVERIFY(word_end > word && word_end < word + 200);
  key(&ed, Qt::Key_End);
  QCOMPARE(ed.cursor_position(), word_end);
  key(&ed, Qt::Key_Home);
  QCOMPARE(ed.cursor_position(), word);
  key(&ed, Qt::Key_End, Qt::ControlModifier);
  QCOMPARE(ed.cursor_position(), text.size());
  key(&ed, Qt::Key_Home, Qt::ControlModifier);
  QCOMPARE(ed.cursor_position(), std::size_t{0});
}

void TestSourceEditor::modification_signals() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("abc");
  QSignalSpy modified(&ed, &SourceEditor::modificationChanged);
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  ed.set_cursor_position(3);
  QTest::keyClicks(&ed, "de");
  QCOMPARE(changed.count(), 2);
  QCOMPARE(modified.count(), 1);
  QCOMPARE(modified.at(0).at(0).toBool(), true);
  ed.mark_saved();
  QCOMPARE(modified.count(), 2);
  QCOMPARE(modified.at(1).at(0).toBool(), false);
  ed.undo();
  QCOMPARE(ed.text(), std::string("abc"));
  QCOMPARE(modified.count(), 3);
  ed.redo();
  QCOMPARE(modified.count(), 4);
  QVERIFY(!ed.is_modified());
}

void TestSourceEditor::highlight_formats() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("# Title\n\nSome **bold** and *em* and `code`.\n");
  auto find = [&](std::size_t line, int pos) {
    QTextCharFormat merged;
    for (const auto& r : ed.line_formats(line))
      if (r.start <= pos && pos < r.start + r.length) merged.merge(r.format);
    return merged;
  };
  QVERIFY(find(0, 3).fontWeight() >= QFont::Bold);
  QVERIFY(find(0, 3).fontPointSize() > ed.font().pointSizeF());
  QCOMPARE(find(0, 0).foreground().color(), ed.style_table()[lumos::NodeKind::heading_marker].foreground().color());
  QVERIFY(find(0, 0).foreground().color() != ed.palette().color(QPalette::Text));
  QVERIFY(find(2, 8).fontWeight() >= QFont::Bold);  
  QVERIFY(find(2, 19).fontItalic());  
  QVERIFY(!find(2, 30).fontFamilies().toStringList().isEmpty());  
  ed.set_cursor_position(1);
  key(&ed, Qt::Key_Backspace);
  QVERIFY(find(0, 3).fontWeight() < QFont::Bold);
  check_editor(ed);
}

void TestSourceEditor::word_wrap_and_scrolling() {
  SourceEditor ed;
  ed.resize(300, 200);
  std::string text;
  for (int i = 0; i < 30; ++i) text += "word ";
  text += "\n";
  for (int i = 0; i < 100; ++i) text += "line " + std::to_string(i) + "\n";
  ed.set_text(text);
  QVERIFY(ed.visual_line_count() > static_cast<int>(ed.line_count()));
  QScrollBar* bar = ed.verticalScrollBar();
  QVERIFY(bar->maximum() > 0);
  QCOMPARE(bar->value(), 0);
  key(&ed, Qt::Key_PageDown);
  QVERIFY(bar->value() > 0);
  QVERIFY(ed.cursor_position() > 0);
  key(&ed, Qt::Key_End, Qt::ControlModifier);
  QCOMPARE(bar->value(), bar->maximum());
  key(&ed, Qt::Key_Home, Qt::ControlModifier);
  QCOMPARE(bar->value(), 0);
  key(&ed, Qt::Key_Down);
  QVERIFY(ed.cursor_position() > 0 && ed.cursor_position() < 150);
}

void TestSourceEditor::text_width_and_page() {
  SourceEditor ed;
  ed.resize(2000, 300);
  std::string text;
  for (int i = 0; i < 60; ++i) text += "word ";
  text += "\n\n";
  ed.set_text(text);
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  QCOMPARE(ed.max_text_width(), 100);
  QVERIFY(ed.line_numbers());
  const int limited = ed.visual_line_count();
  QVERIFY(limited >= 1 + 3 + 1);
  lumos::qt::StyleTable styles = ed.style_table();
  const QRect first = ed.cursor_rect();
  const int left = first.x();
  QVERIFY(left > 400);
  QCOMPARE(first.y(), styles.page_margin + styles.page_padding);
  ed.set_line_numbers(false);
  QCOMPARE(ed.cursor_rect().x(), left);
  ed.set_line_numbers(true);
  const int number_x = left - styles.page_padding - 3;
  ed.set_cursor_position(10);
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(number_x, first.center().y()));
  QCOMPARE(ed.cursor_position(), std::size_t{0});
  ed.resize(300, 300);
  QVERIFY(ed.visual_line_count() > limited);
  QVERIFY(ed.cursor_rect().x() > styles.page_margin + styles.page_padding);
  QVERIFY(ed.cursor_rect().x() < left);
  ed.resize(2000, 300);
  ed.set_max_text_width(0);
  QVERIFY(ed.visual_line_count() < limited);

  ed.set_max_text_width(100);
  QCOMPARE(ed.cursor_rect().x(), left);
  styles.page_background = QColor(Qt::red);
  styles.outside_background = QColor(Qt::blue);
  styles.gutter_background = QColor(Qt::green);
  ed.set_style_table(styles);
  const QImage img = ed.viewport()->grab().toImage();
  const int y = img.height() - 5;  
  QCOMPARE(img.pixelColor(2, y), QColor(Qt::blue));
  QCOMPARE(img.pixelColor(number_x, y), QColor(Qt::green));
  QCOMPARE(img.pixelColor(left - styles.page_padding + 1, y), QColor(Qt::red));
  QCOMPARE(img.pixelColor(img.width() - 3, y), QColor(Qt::blue));
  QCOMPARE(img.pixelColor(left, styles.page_margin - 2), QColor(Qt::blue));

  QVERIFY(ed.page_centred());
  ed.set_page_centred(false);
  ed.set_line_numbers(false);
  QCOMPARE(ed.cursor_rect().x(), styles.page_margin + styles.page_padding);
  ed.set_line_numbers(true);
  const QRect leftmost = ed.cursor_rect();
  QVERIFY(leftmost.x() > styles.page_margin + styles.page_padding);
  QVERIFY(leftmost.x() < 200);
  QCOMPARE(ed.visual_line_count(), limited);
  QTest::mouseClick(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(leftmost.x() + 1, leftmost.center().y()));
  QCOMPARE(ed.cursor_position(), std::size_t{0});
  const QImage left_img = ed.viewport()->grab().toImage();
  QCOMPARE(left_img.pixelColor(left_img.width() - 3, y), QColor(Qt::blue));
  QCOMPARE(left_img.pixelColor(leftmost.x() - styles.page_padding - 3, y), QColor(Qt::green));
  ed.set_page_centred(true);
  QCOMPARE(ed.cursor_rect().x(), left);
}

void TestSourceEditor::bogus_average_width() {
  QFont font(QStringLiteral("Terminus"));
  font.setPointSizeF(11);
  const QFontMetricsF fm(font);
  if (QFontInfo(font).family() != QStringLiteral("Terminus") || fm.averageCharWidth() <= fm.maxWidth())
    QSKIP("no font with a bogus average width");
  SourceEditor ed;
  ed.setFont(font);
  ed.resize(2000, 300);
  std::string text;
  for (int i = 0; i < 60; ++i) text += "word ";
  ed.set_text(text + "\n");
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  QVERIFY(ed.visual_line_count() >= 3 + 1);
  const qreal column = 100 * fm.horizontalAdvance(QLatin1Char('0'));
  QVERIFY(std::abs(ed.cursor_rect().x() - (2000 - column) / 2) < 50);
}

void TestSourceEditor::input_method() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("ab");
  ed.set_cursor_position(1);
  {
    QList<QInputMethodEvent::Attribute> attrs{{QInputMethodEvent::Cursor, 1, 1, QVariant()}};
    QInputMethodEvent e(QString::fromUtf8("に"), attrs);
    QApplication::sendEvent(&ed, &e);
  }
  QCOMPARE(ed.text(), std::string("ab"));  
  QVERIFY(ed.visual_line_count() == 1);
  const QRect r = ed.inputMethodQuery(Qt::ImCursorRectangle).toRect();
  QVERIFY(r.height() > 0);
  QCOMPARE(ed.inputMethodQuery(Qt::ImCursorPosition).toInt(), 1);
  QCOMPARE(ed.inputMethodQuery(Qt::ImSurroundingText).toString(), QString("ab"));
  {
    QInputMethodEvent e;
    e.setCommitString(QString::fromUtf8("日本"));
    QApplication::sendEvent(&ed, &e);
  }
  QCOMPARE(ed.text(), std::string("a日本b"));
  QCOMPARE(ed.cursor_position(), std::size_t{7});
  QCOMPARE(ed.inputMethodQuery(Qt::ImCursorPosition).toInt(), 3);
  {
    QInputMethodEvent e;
    e.setCommitString("X", -1, 1);
    QApplication::sendEvent(&ed, &e);
  }
  QCOMPARE(ed.text(), std::string("a日Xb"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("a日本b"));
  check_editor(ed);
}

void TestSourceEditor::random_edits_keep_lines() {
  SourceEditor ed;
  ed.resize(400, 300);
  ed.set_text(read_file(qs(sample_path())));
  std::mt19937 rng(11);
  const std::vector<QString> texts = {"a", " ", "*", "#", "`", "|", QString::fromUtf8("é"), QString::fromUtf8("🎉")};
  for (int step = 0; step < 300; ++step) {
    const int op = std::uniform_int_distribution<int>(0, 11)(rng);
    if (op < 4) {
      type_key(&ed, texts[std::uniform_int_distribution<std::size_t>(0, texts.size() - 1)(rng)]);
    } else if (op == 4) {
      key(&ed, Qt::Key_Return);
    } else if (op == 5) {
      key(&ed, Qt::Key_Backspace);
    } else if (op == 6) {
      key(&ed, Qt::Key_Delete);
    } else if (op == 7) {
      ed.set_cursor_position(std::uniform_int_distribution<std::size_t>(0, ed.text().size())(rng));
    } else if (op == 8) {
      key(&ed, std::uniform_int_distribution<int>(0, 1)(rng) ? Qt::Key_Up : Qt::Key_Down);
    } else if (op == 9) {
      const std::size_t a = std::uniform_int_distribution<std::size_t>(0, ed.text().size())(rng);
      const std::size_t b = std::min(ed.text().size(), a + std::uniform_int_distribution<std::size_t>(0, 30)(rng));
      ed.select(a, b);
      key(&ed, Qt::Key_Backspace);
    } else if (op == 10) {
      ed.undo();
    } else {
      ed.redo();
    }
    check_editor(ed);
    if (QTest::currentTestFailed()) QFAIL(qPrintable(QString("step %1 op %2").arg(step).arg(op)));
  }
  check_editor(ed);
  while (ed.document().can_undo()) ed.undo();
  QCOMPARE(ed.text(), read_file(qs(sample_path())));
  QVERIFY(!ed.is_modified());
}

void TestSourceEditor::performance_5000_lines() {
  const std::string sample = read_file(qs(sample_path()));
  std::string text;
  std::size_t lines = 0;
  const std::size_t want = qEnvironmentVariableIsSet("LUMOS_PERF_LINES")
                               ? static_cast<std::size_t>(qEnvironmentVariableIntValue("LUMOS_PERF_LINES"))
                               : 5000;
  while (lines < want) {
    text += sample;
    lines += static_cast<std::size_t>(std::count(sample.begin(), sample.end(), '\n'));
  }
  SourceEditor ed;
  ed.resize(800, 600);
  QElapsedTimer t;
  t.start();
  ed.set_text(text);
  const qint64 load_ms = t.elapsed();
  t.restart();
  QCOMPARE(ed.visual_line_count() > 0, true);  
  const qint64 layout_ms = t.elapsed();

  ed.set_cursor_position(text.find("Intro with", text.size() / 2) + 5);
  std::vector<std::string> log_lines;
  if (qEnvironmentVariableIsSet("LUMOS_PERF_LOG")) {
    lumos::set_log_level(lumos::LogLevel::debug);
    lumos::set_log_sink([&](lumos::LogLevel, std::string_view l) { log_lines.emplace_back(l); });
  }
  t.restart();
  const int keys = 50;
  for (int i = 0; i < keys; ++i) {
    type_key(&ed, "x");
    (void)ed.visual_line_count();  
  }
  const double key_ms = static_cast<double>(t.elapsed()) / keys;
  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);
  for (std::size_t i = log_lines.size() > 12 ? log_lines.size() - 12 : 0; i < log_lines.size(); ++i)
    qInfo("%s", log_lines[i].c_str());
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  (void)ed.visual_line_count();
  t.restart();
  ed.resize(700, 600);
  QCoreApplication::processEvents();
  QCOMPARE(ed.viewport()->width() < 700, true);
  (void)ed.visual_line_count();
  const qint64 resize_ms = t.elapsed();
  t.restart();
  QImage img(ed.size(), QImage::Format_ARGB32);
  ed.render(&img);
  const qint64 paint_ms = t.elapsed();
  qInfo("perf: lines=%zu bytes=%zu load_ms=%lld layout_ms=%lld key_ms=%.2f resize_ms=%lld paint_ms=%lld",
        ed.line_count(), text.size(), load_ms, layout_ms, key_ms, resize_ms, paint_ms);
}

void TestSourceEditor::restyle_each_line_once() {
  std::string text = "Intro\n\n";
  for (int i = 1; i <= 300; ++i) text += std::to_string(i) + ". item *" + std::to_string(i) + "*\n";
  text += "\nEnd with `code`\n";
  SourceEditor ed;
  ed.resize(800, 600);
  ed.set_text(text);
  (void)ed.visual_line_count();
  std::vector<std::string> log_lines;
  lumos::set_log_level(lumos::LogLevel::debug);
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view l) { log_lines.emplace_back(l); });
  const auto last_change = [&]() -> std::pair<std::size_t, std::size_t> {
    const auto field = [](const std::string& l, const std::string& key) {
      const std::size_t at = l.find(" " + key + "=");
      return at == std::string::npos ? SIZE_MAX : std::stoul(l.substr(at + key.size() + 2));
    };
    for (auto it = log_lines.rbegin(); it != log_lines.rend(); ++it) {
      if (it->find("comp=qt event=change ") != std::string::npos) return {field(*it, "restyled"), field(*it, "lines")};
    }
    return {SIZE_MAX, 0};
  };
  const auto at = [&](const char* s) { return ed.text().find(s); };

  ed.set_cursor_position(at("3. item *3*\n") + 11);
  key(&ed, Qt::Key_Return);
  auto [restyled, lines] = last_change();
  const bool renumbered = at("301. item *300*") != std::string::npos;
  ed.set_cursor_position(0);
  type_key(&ed, "`");
  type_key(&ed, "`");
  type_key(&ed, "`");
  key(&ed, Qt::Key_Return);
  auto [fence_restyled, fence_lines] = last_change();
  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);

  QVERIFY(renumbered);
  QVERIFY2(restyled > 290 && restyled <= lines, qPrintable(QString("restyled=%1 lines=%2").arg(restyled).arg(lines)));
  QVERIFY2(fence_restyled > 290 && fence_restyled <= fence_lines,
           qPrintable(QString("restyled=%1 lines=%2").arg(fence_restyled).arg(fence_lines)));
  check_editor(ed);
  ed.undo();
  check_editor(ed);
}

void TestSourceEditor::commands_on_long_list() {
  std::string text;
  for (int i = 1; i <= 1000; ++i) {
    const std::string mark = std::to_string(i) + ". ";
    text += mark + "item " + std::to_string(i) + "\n";
    if (i % 10 == 0) {
      const std::string pad(mark.size(), ' ');
      text += pad + "a continuation line\n" + pad + "- nested a\n" + pad + "- nested b\n";
    }
  }
  SourceEditor ed;
  ed.resize(800, 600);
  ed.set_text(text);
  (void)ed.visual_line_count();
  const qint64 limit_ms = 1000;
  QElapsedTimer t;
  QStringList report;
  const auto timed = [&](const char* name, const auto& action) {
    t.restart();
    action();
    (void)ed.visual_line_count();  
    const qint64 ms = t.elapsed();
    report << QStringLiteral("%1_ms=%2").arg(QLatin1String(name)).arg(ms);
    QVERIFY2(ms < limit_ms, name);
  };
  const auto at = [&](const char* s) { return ed.text().find(s); };
  timed("enter", [&] {
    ed.set_cursor_position(at("3. item 3\n") + 9);
    key(&ed, Qt::Key_Return);
  });
  QCOMPARE(at("1001. item 1000") != std::string::npos, true);  
  ed.undo();
  timed("cut", [&] {
    ed.select(at("500. item 500"), at("501. item 501"));
    ed.cut();
  });
  ed.undo();
  timed("tab", [&] {
    ed.set_cursor_position(at("500. item 500"));
    key(&ed, Qt::Key_Tab);
  });
  timed("shift_tab", [&] { key(&ed, Qt::Key_Backtab, Qt::ShiftModifier); });
  QCOMPARE(ed.text(), text);
  const auto all = [&](lumos::BlockKind kind) {
    ed.select(0, ed.text().size());
    ed.apply_command(lumos::toggle_block(ed.document(), ed.selection(), kind));
  };
  timed("quote", [&] { all(lumos::BlockKind::block_quote); });
  ed.undo();
  timed("ordered_off", [&] { all(lumos::BlockKind::ordered_list); });
  ed.undo();
  timed("heading", [&] {
    ed.select(0, ed.text().size());
    ed.apply_command(lumos::set_heading(ed.document(), ed.selection(), 2));
  });
  ed.undo();
  QCOMPARE(ed.text(), text);

  {
    std::string table = "| a | b |\n| --- | --- |\n";
    for (int i = 0; i < 200; ++i) table += "| c | d |\n";
    SourceEditor ed2;
    ed2.resize(800, 600);
    ed2.set_text(table);
    (void)ed2.visual_line_count();
    ed2.set_cursor_position(table.find("| c | d |"));
    QElapsedTimer t2;
    t2.restart();
    key(&ed2, Qt::Key_Tab);
    (void)ed2.visual_line_count();
    report << QStringLiteral("table_tab_ms=%1").arg(t2.elapsed());
    t2.restart();
    key(&ed2, Qt::Key_Return);
    (void)ed2.visual_line_count();
    report << QStringLiteral("table_enter_ms=%1").arg(t2.elapsed());
  }

  qInfo("perf: %s", qPrintable(report.join(QLatin1Char(' '))));
}

void TestSourceEditor::render_png() {
  const QString dir = qEnvironmentVariable("TMPDIR");
  if (dir.isEmpty()) QSKIP("TMPDIR is not set");
  SourceEditor ed;
  ed.resize(700, 900);
  ed.set_text(read_file(qs(sample_path())));
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  QCOMPARE(ed.visual_line_count(), static_cast<int>(ed.line_count()));
  ed.select(30, 60);
  const QImage img = ed.grab().toImage();
  QVERIFY(!img.isNull());
  bool varied = false;
  const QRgb first = img.pixel(10, 10);
  for (int y = 0; y < img.height() && !varied; y += 3)
    for (int x = 0; x < img.width() && !varied; x += 3) varied = img.pixel(x, y) != first;
  QVERIFY(varied);
  const QString path = QDir(dir).filePath("lumos-source-editor.png");
  QVERIFY(img.save(path));
  qInfo("png: %s", qPrintable(path));
}

void TestSourceEditor::slots_callable_by_name() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("abc");
  QVERIFY(QMetaObject::invokeMethod(&ed, "selectAll"));
  QCOMPARE(ed.selected_text(), std::string("abc"));
  QVERIFY(QMetaObject::invokeMethod(&ed, "copy"));
  QCOMPARE(QApplication::clipboard()->text(), QString("abc"));
  QVERIFY(QMetaObject::invokeMethod(&ed, "cut"));
  QCOMPARE(ed.text(), std::string(""));
  QVERIFY(QMetaObject::invokeMethod(&ed, "paste"));
  QVERIFY(QMetaObject::invokeMethod(&ed, "paste"));
  QCOMPARE(ed.text(), std::string("abcabc"));
  QVERIFY(QMetaObject::invokeMethod(&ed, "undo"));
  QCOMPARE(ed.text(), std::string("abc"));
  QVERIFY(QMetaObject::invokeMethod(&ed, "redo"));
  QCOMPARE(ed.text(), std::string("abcabc"));
}

void TestSourceEditor::undo_redo_state() {
  SourceEditor ed;
  ed.resize(600, 400);
  QSignalSpy undo_spy(&ed, &SourceEditor::canUndoChanged);
  QSignalSpy redo_spy(&ed, &SourceEditor::canRedoChanged);
  QVERIFY(!ed.canUndo());
  QVERIFY(!ed.canRedo());
  QTest::keyClicks(&ed, "ab");
  QVERIFY(ed.canUndo());
  QCOMPARE(undo_spy.count(), 1);
  QCOMPARE(undo_spy.at(0).at(0).toBool(), true);
  ed.undo();
  QVERIFY(!ed.canUndo());
  QVERIFY(ed.canRedo());
  QCOMPARE(undo_spy.count(), 2);
  QCOMPARE(redo_spy.count(), 1);
  ed.redo();
  QVERIFY(ed.canUndo());
  QVERIFY(!ed.canRedo());
  QCOMPARE(redo_spy.count(), 2);
  ed.undo();
  QVERIFY(ed.canRedo());
  type_key(&ed, "x");
  QVERIFY(!ed.canRedo());
  QVERIFY(ed.canUndo());
  ed.set_text("new");
  QVERIFY(!ed.canUndo());
  QVERIFY(!ed.canRedo());
  QCOMPARE(undo_spy.last().at(0).toBool(), false);
}

void TestSourceEditor::set_text_contract() {
  SourceEditor ed;
  ed.resize(300, 200);
  std::string text;
  for (int i = 0; i < 200; ++i) text += "line " + std::to_string(i) + "\n";
  ed.set_text("x");
  type_key(&ed, "y");
  QVERIFY(ed.is_modified());
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  QSignalSpy modified(&ed, &SourceEditor::modificationChanged);
  ed.set_text(text);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(modified.count(), 1);
  QCOMPARE(modified.at(0).at(0).toBool(), false);
  QVERIFY(!ed.canUndo());
  QCOMPARE(ed.text(), text);
  QCOMPARE(ed.length(), text.size());
  QCOMPARE(ed.cursor_position(), std::size_t{0});

  QScrollBar* bar = ed.verticalScrollBar();
  const std::size_t at = text.find("line 120");
  ed.select(at, at + 4);
  const int scroll = bar->value();
  QVERIFY(scroll > 0);
  std::string edited = text;
  edited.insert(text.find("line 3\n"), "new ");  
  ed.set_text(edited, true);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(ed.anchor_position(), at);
  QCOMPARE(ed.cursor_position(), at + 4);
  QCOMPARE(bar->value(), scroll);
  ed.set_text("short", true);
  QCOMPARE(ed.cursor_position(), std::size_t{5});
  QCOMPARE(ed.anchor_position(), std::size_t{5});
  QCOMPARE(bar->value(), 0);
  ed.set_text(text, true);
  ed.set_cursor_position(at);
  ed.set_text(text);
  QCOMPARE(ed.cursor_position(), std::size_t{0});
  QCOMPARE(bar->value(), 0);
  QCOMPARE(changed.count(), 0);
  check_editor(ed);
}

void TestSourceEditor::replace_text_one_step() {
  SourceEditor ed;
  ed.resize(600, 400);
  const std::string before = "# Title\r\n\r\nSome *text* with é and more.\r\n";
  ed.set_text(before);
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  ed.set_cursor_position(before.find("more"));
  std::string after = before;
  after.replace(after.find("é"), 2, "ê");
  after.replace(0, 7, "## Other");
  ed.replace_text(after);
  QCOMPARE(ed.text(), after);
  QCOMPARE(ed.length(), after.size());
  QCOMPARE(changed.count(), 1);
  QCOMPARE(ed.cursor_position(), after.find("more"));
  QVERIFY(ed.is_modified());
  check_editor(ed);
  ed.undo();
  QCOMPARE(ed.text(), before);
  QVERIFY(!ed.is_modified());
  QVERIFY(!ed.canUndo());
  ed.redo();
  QCOMPARE(ed.text(), after);
  changed.clear();
  ed.replace_text(after);
  QCOMPARE(changed.count(), 0);
  ed.undo();
  QCOMPARE(ed.text(), before);
  ed.replace_text("");
  QCOMPARE(ed.text(), std::string());
  ed.replace_text(before);
  QCOMPARE(ed.text(), before);
  ed.undo();
  ed.undo();
  QCOMPARE(ed.text(), before);
  check_editor(ed);
}

void TestSourceEditor::restyle_emits_no_text_change() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("# Title\n\n**bold**\n");
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  QSignalSpy modified(&ed, &SourceEditor::modificationChanged);
  lumos::qt::StyleTable t = ed.style_table();
  t[lumos::NodeKind::strong].setForeground(Qt::red);
  ed.set_style_table(t);
  QFont f = ed.font();
  f.setPointSizeF(f.pointSizeF() + 3);
  ed.setFont(f);
  QPalette p = ed.palette();
  p.setColor(QPalette::Text, Qt::darkGreen);
  ed.setPalette(p);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(modified.count(), 0);
  QVERIFY(!ed.is_modified());
}

void TestSourceEditor::undo_to_saved_state() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("abc");
  ed.set_cursor_position(3);
  type_key(&ed, "d");
  ed.mark_saved();
  QVERIFY(!ed.is_modified());
  QVERIFY(ed.canUndo());  
  QSignalSpy modified(&ed, &SourceEditor::modificationChanged);
  key(&ed, Qt::Key_Left);  
  type_key(&ed, "e");
  QCOMPARE(modified.count(), 1);
  QCOMPARE(modified.at(0).at(0).toBool(), true);
  ed.undo();
  QCOMPARE(ed.text(), std::string("abcd"));
  QCOMPARE(modified.count(), 2);
  QCOMPARE(modified.at(1).at(0).toBool(), false);
  ed.undo();
  QCOMPARE(ed.text(), std::string("abc"));
  QCOMPARE(modified.count(), 3);
  QCOMPARE(modified.at(2).at(0).toBool(), true);
  ed.redo();
  QCOMPARE(modified.count(), 4);
  QVERIFY(!ed.is_modified());
}

void TestSourceEditor::read_only() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("one two\nthree");
  ed.set_cursor_position(3);
  QTest::keyClicks(&ed, "X");  
  QSignalSpy undo_spy(&ed, &SourceEditor::canUndoChanged);
  ed.set_read_only(true);
  QVERIFY(ed.is_read_only());
  QVERIFY(!ed.canUndo());
  QCOMPARE(undo_spy.count(), 1);
  QVERIFY(!ed.testAttribute(Qt::WA_InputMethodEnabled));
  const std::string text = ed.text();
  QTest::keyClicks(&ed, "abc");
  key(&ed, Qt::Key_Backspace);
  key(&ed, Qt::Key_Delete);
  key(&ed, Qt::Key_Return);
  key(&ed, Qt::Key_Tab);
  key(&ed, Qt::Key_Z, Qt::ControlModifier);
  ed.undo();
  QApplication::clipboard()->setText("pasted");
  ed.paste();
  {
    QInputMethodEvent e;
    e.setCommitString("ime");
    QApplication::sendEvent(&ed, &e);
  }
  QCOMPARE(ed.text(), text);
  key(&ed, Qt::Key_Home);
  key(&ed, Qt::Key_End, Qt::ShiftModifier);
  QCOMPARE(ed.selected_text(), std::string("oneX two"));
  ed.cut();
  QCOMPARE(ed.text(), text);
  key(&ed, Qt::Key_C, Qt::ControlModifier);
  QCOMPARE(QApplication::clipboard()->text(), QString("oneX two"));
  ed.selectAll();
  QCOMPARE(ed.selected_text(), text);
  ed.set_cursor_position(0);
  ed.insert_text("A");
  QCOMPARE(ed.text(), "A" + text);
  ed.set_read_only(false);
  QVERIFY(ed.canUndo());
  QVERIFY(ed.testAttribute(Qt::WA_InputMethodEnabled));
  ed.undo();
  ed.undo();
  QCOMPARE(ed.text(), std::string("one two\nthree"));
  check_editor(ed);
}

namespace {

bool override_taken(QWidget* w, Qt::Key k, Qt::KeyboardModifiers mods, const QString& text = {}) {
  QKeyEvent e(QEvent::ShortcutOverride, k, mods, text);
  e.ignore();
  QApplication::sendEvent(w, &e);
  return e.isAccepted();
}

}  

void TestSourceEditor::shortcut_override() {
  SourceEditor ed;
  const auto none = Qt::NoModifier;
  const auto ctrl = Qt::ControlModifier;
  for (Qt::Key k : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down, Qt::Key_Home, Qt::Key_End}) {
    QVERIFY(override_taken(&ed, k, none));
    QVERIFY(override_taken(&ed, k, ctrl));
    QVERIFY(override_taken(&ed, k, ctrl | Qt::ShiftModifier));
  }
  for (Qt::Key k : {Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Tab, Qt::Key_Backspace, Qt::Key_Return}) {
    QVERIFY(override_taken(&ed, k, none));
    QVERIFY(!override_taken(&ed, k, ctrl));
  }
  QVERIFY(override_taken(&ed, Qt::Key_A, none, "a"));
  QVERIFY(override_taken(&ed, Qt::Key_A, Qt::ShiftModifier, "A"));
  QVERIFY(override_taken(&ed, Qt::Key_E, ctrl | Qt::AltModifier, QString::fromUtf8("€")));  
  QVERIFY(!override_taken(&ed, Qt::Key_F, Qt::AltModifier, "f"));  
  for (Qt::Key k : {Qt::Key_Z, Qt::Key_Y, Qt::Key_C, Qt::Key_X, Qt::Key_V, Qt::Key_A, Qt::Key_S, Qt::Key_B})
    QVERIFY(!override_taken(&ed, k, ctrl, QString(QChar(k - Qt::Key_A + 1))));
  ed.set_read_only(true);
  QVERIFY(override_taken(&ed, Qt::Key_PageDown, none));
  QVERIFY(override_taken(&ed, Qt::Key_Left, ctrl));
  QVERIFY(!override_taken(&ed, Qt::Key_A, none, "a"));
  QVERIFY(!override_taken(&ed, Qt::Key_Tab, none));
  QVERIFY(!override_taken(&ed, Qt::Key_Backspace, none));
}

void TestSourceEditor::window_shortcuts_reach_window() {
  QMainWindow window;
  auto* ed = new SourceEditor(&window);
  window.setCentralWidget(ed);
  window.resize(600, 400);
  int next_tab = 0;
  int host_undo = 0;
  QAction* tab_action = window.addAction("next");
  tab_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_PageDown));
  QObject::connect(tab_action, &QAction::triggered, [&] { ++next_tab; });
  QAction* undo_action = window.addAction("undo");
  undo_action->setShortcut(QKeySequence::Undo);
  QObject::connect(undo_action, &QAction::triggered, [&] {
    ++host_undo;
    ed->undo();
  });
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.activateWindow();
  ed->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(&window));
  ed->set_text("");
  QTest::keyClicks(ed, "ab");
  QTest::keyClick(ed, Qt::Key_PageDown, Qt::ControlModifier);
  QCOMPARE(next_tab, 1);
  QTest::keyClick(ed, Qt::Key_Z, Qt::ControlModifier);
  QCOMPARE(host_undo, 1);
  QCOMPARE(ed->text(), std::string());
  QTest::keyClick(ed, Qt::Key_PageDown);
  QCOMPARE(next_tab, 1);
}

void TestSourceEditor::custom_context_menu() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("abc");
  QSignalSpy spy(&ed, &SourceEditor::customContextMenuRequested);
  const QPoint at(20, 10);
  {
    QContextMenuEvent e(QContextMenuEvent::Mouse, at, ed.viewport()->mapToGlobal(at));
    QApplication::sendEvent(ed.viewport(), &e);
  }
  QCOMPARE(spy.count(), 0);  
  ed.setContextMenuPolicy(Qt::CustomContextMenu);
  {
    QContextMenuEvent e(QContextMenuEvent::Mouse, at, ed.viewport()->mapToGlobal(at));
    QApplication::sendEvent(ed.viewport(), &e);
  }
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toPoint(), at);
  QVERIFY(ed.findChildren<QMenu*>().isEmpty());
}

void TestSourceEditor::theme_from_palette() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("# Title\n\n```\ncode\n```\n");
  QVERIFY(ed.styleSheet().isEmpty());
  const QColor light_marker = ed.style_table()[lumos::NodeKind::heading_marker].foreground().color();
  QPalette dark = ed.palette();
  dark.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
  dark.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
  dark.setColor(QPalette::Link, QColor(0x80, 0xb0, 0xff));
  ed.setPalette(dark);
  const lumos::qt::StyleTable& t = ed.style_table();
  const QColor marker = t[lumos::NodeKind::heading_marker].foreground().color();
  QVERIFY(marker != light_marker);
  QVERIFY(marker.lightness() < 0xe0 && marker.lightness() > 0x1e);
  QVERIFY(t.code_line_background.lightness() < 0x60);
  QCOMPARE(t[lumos::NodeKind::link].foreground().color(), QColor(0x80, 0xb0, 0xff));
  bool found = false;
  for (const auto& r : ed.line_formats(0)) found = found || r.format.foreground().color() == marker;
  QVERIFY(found);
  lumos::qt::StyleTable own = t;
  own[lumos::NodeKind::heading_marker].setForeground(Qt::red);
  ed.set_style_table(own);
  ed.setPalette(QPalette());
  QCOMPARE(ed.style_table()[lumos::NodeKind::heading_marker].foreground().color(), QColor(Qt::red));
}

void TestSourceEditor::two_widgets_share_no_state() {
  SourceEditor a;
  SourceEditor b;
  a.resize(600, 400);
  b.resize(600, 400);
  a.set_text("aaa");
  b.set_text("bbb");
  a.set_cursor_position(3);
  b.set_cursor_position(1);
  QSignalSpy b_changed(&b, &SourceEditor::textChanged);
  QTest::keyClicks(&a, "xy");
  QCOMPARE(a.text(), std::string("aaaxy"));
  QCOMPARE(b.text(), std::string("bbb"));
  QCOMPARE(b_changed.count(), 0);
  QVERIFY(!b.canUndo());
  QVERIFY(!b.is_modified());
  QTest::keyClicks(&b, "z");
  b.undo();
  QCOMPARE(a.text(), std::string("aaaxy"));
  a.set_read_only(true);
  QVERIFY(!b.is_read_only());
  lumos::qt::StyleTable t = a.style_table();
  t[lumos::NodeKind::strong].setForeground(Qt::red);
  a.set_style_table(t);
  QVERIFY(b.style_table()[lumos::NodeKind::strong].foreground().color() != QColor(Qt::red));
  QCOMPARE(b.cursor_position(), std::size_t{1});
}

void TestSourceEditor::works_with_signals_blocked() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("abc");
  QSignalSpy modified(&ed, &SourceEditor::modificationChanged);
  QSignalSpy undo_spy(&ed, &SourceEditor::canUndoChanged);
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  {
    const QSignalBlocker block(&ed);
    ed.set_cursor_position(3);
    QTest::keyClicks(&ed, "de");
    ed.replace_text("abcdef");
    ed.undo();
    ed.set_read_only(true);
    ed.set_read_only(false);
  }
  QCOMPARE(modified.count(), 0);
  QCOMPARE(undo_spy.count(), 0);
  QCOMPARE(changed.count(), 0);
  QCOMPARE(ed.text(), std::string("abcde"));
  QVERIFY(ed.is_modified());
  QVERIFY(ed.canUndo());
  QVERIFY(ed.canRedo());
  ed.mark_saved();
  QCOMPARE(modified.count(), 1);
  QCOMPARE(modified.at(0).at(0).toBool(), false);
  ed.undo();
  QCOMPARE(ed.text(), std::string("abc"));
  QCOMPARE(modified.count(), 2);
  QCOMPARE(changed.count(), 1);
  ed.undo();
  QVERIFY(!ed.canUndo());
  QCOMPARE(undo_spy.count(), 1);
  check_editor(ed);
}

void TestSourceEditor::renumber_after_edits() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("1. a\n2. b\n3. c\n4. d");
  ed.select(5, 10);
  ed.cut();
  QCOMPARE(ed.text(), std::string("1. a\n2. c\n3. d"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("1. a\n2. b\n3. c\n4. d"));
  ed.set_cursor_position(9);
  key(&ed, Qt::Key_Tab);
  QCOMPARE(ed.text(), std::string("1. a\n   1. b\n2. c\n3. d"));
  QCOMPARE(ed.cursor_position(), std::size_t{12});
  ed.set_text("1. a\n2. b\n3. c");
  ed.set_cursor_position(9);
  QTest::keyClicks(&ed, "x");
  QCOMPARE(ed.text(), std::string("1. a\n2. bx\n3. c"));
  ed.select(5, 6);
  QTest::keyClicks(&ed, "7");
  QCOMPARE(ed.text(), std::string("1. a\n2. bx\n3. c"));
  check_editor(ed);
}

void TestSourceEditor::marks_move_with_edits() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("ab cd\nef gh");
  QTextCharFormat f;
  f.setFontUnderline(true);
  ed.set_marks(1, {ByteRange{3, 5}, ByteRange{9, 11}, ByteRange{20, 30}}, f);
  QCOMPARE(ed.marks(1), (std::vector<ByteRange>{{3, 5}, {9, 11}}));
  const auto underlined = [&](std::size_t line) {
    int n = 0;
    for (const auto& r : ed.line_formats(line)) {
      if (r.format.fontUnderline()) n += r.length;
    }
    return n;
  };
  QCOMPARE(underlined(0), 2);
  QCOMPARE(underlined(1), 2);
  ed.set_cursor_position(0);
  QTest::keyClicks(&ed, "x");
  QCOMPARE(ed.marks(1), (std::vector<ByteRange>{{4, 6}, {10, 12}}));
  ed.set_cursor_position(6);
  QTest::keyClicks(&ed, "y");
  QCOMPARE(ed.marks(1), (std::vector<ByteRange>{{4, 6}, {11, 13}}));
  ed.set_cursor_position(5);
  QTest::keyClicks(&ed, "z");
  QCOMPARE(ed.marks(1), (std::vector<ByteRange>{{12, 14}}));
  ed.set_marks(2, {ByteRange{0, 3}, ByteRange{2, 5}}, f);
  QCOMPARE(ed.marks(2), (std::vector<ByteRange>{{0, 5}}));
  ed.set_marks(2, {}, f);
  QVERIFY(ed.marks(2).empty());
  ed.set_text("new");
  QVERIFY(ed.marks(1).empty());
  ed.set_text("ab\ncd");
  const int lh = QFontMetrics(ed.font()).lineSpacing();
  QCOMPARE(ed.offset_at(QPoint(1, ed.cursor_rect().y() + lh + lh / 2)), std::size_t{3});
  check_editor(ed);
}

void TestSourceEditor::markdown_keys() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("- a");
  ed.set_cursor_position(3);
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.text(), std::string("- a\n- "));
  QCOMPARE(ed.cursor_position(), std::size_t{6});
  QTest::keyClicks(&ed, "b");
  key(&ed, Qt::Key_Tab);
  QCOMPARE(ed.text(), std::string("- a\n  - b"));
  QCOMPARE(ed.cursor_position(), std::size_t{9});
  QVERIFY(override_taken(&ed, Qt::Key_Backtab, Qt::ShiftModifier));
  key(&ed, Qt::Key_Backtab, Qt::ShiftModifier);
  QCOMPARE(ed.text(), std::string("- a\n- b"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("- a\n  - b"));
  ed.undo();
  QCOMPARE(ed.text(), std::string("- a\n- b"));

  ed.set_cursor_position(6);
  key(&ed, Qt::Key_Backspace);
  QCOMPARE(ed.text(), std::string("- a\n  b"));
  QCOMPARE(ed.cursor_position(), std::size_t{6});

  ed.set_text("- a");
  ed.set_cursor_position(3);
  key(&ed, Qt::Key_Return, Qt::ShiftModifier);
  QCOMPARE(ed.text(), std::string("- a\\\n  "));
  QCOMPARE(ed.cursor_position(), std::size_t{7});
  ed.undo();
  QCOMPARE(ed.text(), std::string("- a"));
  ed.set_text("# a");
  ed.set_cursor_position(3);
  key(&ed, Qt::Key_Enter, Qt::ShiftModifier);
  QCOMPARE(ed.text(), std::string("# a\n"));

  ed.set_text("- a\n- ");
  ed.set_cursor_position(6);
  key(&ed, Qt::Key_Enter);
  QCOMPARE(ed.text(), std::string("- a\n"));
  key(&ed, Qt::Key_Tab);
  QCOMPARE(ed.text(), std::string("- a\n\t"));

  ed.set_text("> a\r\nb");
  ed.set_cursor_position(3);
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.text(), std::string("> a\r\n> \r\nb"));

  ed.set_text("- a");
  ed.set_cursor_position(3);
  ed.set_read_only(true);
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.text(), std::string("- a"));
  check_editor(ed);
}

void TestSourceEditor::table_keys() {
  SourceEditor ed;
  ed.resize(600, 400);
  const std::string base = "| aaa | bbb |\n| --- | --- |\n| ccc | ddd |";
  ed.set_text(base);

  ed.set_cursor_position(base.find("aaa"));
  key(&ed, Qt::Key_Tab);
  QCOMPARE(ed.anchor_position(), base.find("bbb"));
  QCOMPARE(ed.cursor_position(), base.find("bbb") + 3);
  QCOMPARE(ed.text(), base);

  key(&ed, Qt::Key_Backtab, Qt::ShiftModifier);
  QCOMPARE(ed.anchor_position(), base.find("aaa"));
  QCOMPARE(ed.cursor_position(), base.find("aaa") + 3);

  ed.set_cursor_position(base.find("ddd"));
  key(&ed, Qt::Key_Tab);
  const std::string with_row = base + "\n|     |     |\n";
  QCOMPARE(ed.text(), with_row);
  QCOMPARE(ed.cursor_position(), with_row.find("|     |     |") + 2);
  ed.undo();
  QCOMPARE(ed.text(), base);

  ed.set_cursor_position(base.find("aaa"));
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.anchor_position(), base.find("ccc"));
  QCOMPARE(ed.cursor_position(), base.find("ccc") + 3);
  key(&ed, Qt::Key_Return);
  QCOMPARE(ed.text(), base + "\n\n");
  QCOMPARE(ed.cursor_position(), ed.text().size());
  ed.undo();
  ed.undo();
  QCOMPARE(ed.text(), base);

  ed.set_cursor_position(base.find("ddd"));
  key(&ed, Qt::Key_Enter, Qt::ShiftModifier);
  QCOMPARE(ed.anchor_position(), base.find("bbb"));
  QCOMPARE(ed.cursor_position(), base.find("bbb") + 3);
  QCOMPARE(ed.text(), base);
  check_editor(ed);
}

void TestSourceEditor::apply_command_contract() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("a b");
  ed.select(0, 1);
  QSignalSpy changed(&ed, &SourceEditor::textChanged);
  QVERIFY(ed.apply_command(toggle_inline(ed.document(), ed.selection(), InlineKind::strong)));
  QCOMPARE(ed.text(), std::string("**a** b"));
  QCOMPARE(ed.anchor_position(), std::size_t{2});
  QCOMPARE(ed.cursor_position(), std::size_t{3});
  QVERIFY(changed.count() > 0);
  QVERIFY(format_at(ed.document(), ed.selection()).strong);
  ed.undo();
  QCOMPARE(ed.text(), std::string("a b"));
  QVERIFY(!ed.apply_command(std::nullopt));
  ed.set_read_only(true);
  QVERIFY(!ed.apply_command(toggle_inline(ed.document(), ed.selection(), InlineKind::strong)));
  QCOMPARE(ed.text(), std::string("a b"));
  check_editor(ed);
}

void TestSourceEditor::external_drop() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("first line\nsecond line\nthird");
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  const int lh = QFontMetrics(ed.font()).height();
  const int top = ed.cursor_rect().y();
  const QPoint at(1, top + lh + lh / 2);  

  const auto drop_at = [&](QMimeData& mime) {
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(ed.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(ed.viewport(), &drop);
    QVERIFY(drop.isAccepted());
  };

  {
    QMimeData mime;
    mime.setText("X");
    drop_at(mime);
    QCOMPARE(ed.text(), std::string("first line\nXsecond line\nthird"));
    ed.undo();
    QCOMPARE(ed.text(), std::string("first line\nsecond line\nthird"));
  }
  {
    QMimeData mime;
    mime.setUrls({QUrl("https://example.com/x")});
    drop_at(mime);
    QCOMPARE(ed.text(), std::string("first line\nhttps://example.com/xsecond line\nthird"));
    ed.undo();
  }
  {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile("/some/path/note.md")});
    drop_at(mime);
    QCOMPARE(ed.text(), std::string("first line\n/some/path/note.mdsecond line\nthird"));
    ed.undo();
  }
  {
    QMimeData mime;
    mime.setText("a\r\nb");
    drop_at(mime);
    QCOMPARE(ed.text(), std::string("first line\na\nbsecond line\nthird"));
    ed.undo();
  }
  check_editor(ed);
}

void TestSourceEditor::drop_ignored_read_only_or_disabled() {
  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("first line\nsecond line\nthird");
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  const int lh = QFontMetrics(ed.font()).height();
  const int top = ed.cursor_rect().y();
  const QPoint at(1, top + lh + lh / 2);
  QMimeData mime;
  mime.setText("X");
  const std::string before = ed.text();

  ed.set_read_only(true);
  QDragEnterEvent enter1(at, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &enter1);
  QVERIFY(!enter1.isAccepted());
  QDropEvent drop1(QPointF(at), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &drop1);
  QCOMPARE(ed.text(), before);
  ed.set_read_only(false);

  ed.setAcceptDrops(false);
  QDragEnterEvent enter2(at, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &enter2);
  QVERIFY(!enter2.isAccepted());
  QDropEvent drop2(QPointF(at), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &drop2);
  QCOMPARE(ed.text(), before);
}

void TestSourceEditor::auto_scroll_on_drag_select() {
  SourceEditor ed;
  ed.resize(300, 200);
  std::string text;
  for (int i = 0; i < 200; ++i) text += "line " + std::to_string(i) + "\n";
  ed.set_text(text);
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));
  QScrollBar* bar = ed.verticalScrollBar();
  QCOMPARE(bar->value(), 0);
  const int top = ed.cursor_rect().y();

  QTest::mousePress(ed.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(1, top + 2));
  const std::size_t pressed_at = ed.cursor_position();
  const QPointF below(1, ed.viewport()->height() + 20);
  QMouseEvent move(QEvent::MouseMove, below, ed.viewport()->mapToGlobal(below.toPoint()), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &move);
  QTest::qWait(150);
  QVERIFY(bar->value() > 0);
  QVERIFY(ed.has_selection());
  QVERIFY(ed.cursor_position() > pressed_at);
  const int value_after_wait = bar->value();

  QTest::mouseRelease(ed.viewport(), Qt::LeftButton, Qt::NoModifier, below.toPoint());
  QTest::qWait(150);
  QCOMPARE(bar->value(), value_after_wait);
}

void TestSourceEditor::logs_carry_no_text() {
  const std::string marker = "QZXMARK";
  std::vector<std::string> log_lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view l) { log_lines.emplace_back(l); });
  lumos::set_log_level(lumos::LogLevel::debug);

  SourceEditor ed;
  ed.resize(600, 400);
  ed.set_text("# QZXMARK\n\n- QZXMARK item\n1. QZXMARK one\n\n| QZXMARK | b |\n|---|---|\n| c | d |\n\nQZXMARK end\n");
  ed.show();
  QVERIFY(QTest::qWaitForWindowExposed(&ed));

  for (const std::size_t pos : {std::size_t{4}, std::size_t{25}, std::size_t{40}, std::size_t{50}}) {
    ed.set_cursor_position(pos);
    for (const char c : marker) type_key(&ed, QString(QChar(c)));
    key(&ed, Qt::Key_Return);
    key(&ed, Qt::Key_Tab);
    key(&ed, Qt::Key_Backspace);
  }
  ed.select(0, ed.text().size());
  ed.copy();
  ed.cut();
  ed.paste();
  ed.undo();
  ed.redo();

  QMimeData mime;
  mime.setText("QZXMARK dropped");
  const QPoint at(1, ed.cursor_rect().y());
  QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &enter);
  QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(ed.viewport(), &drop);
  QVERIFY(drop.isAccepted());

  ed.replace_text("QZXMARK replaced\n");
  ed.set_text("QZXMARK reloaded\n", true);
  ed.wipe();

  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);
  QVERIFY(log_lines.size() > 10);  
  for (const auto& line : log_lines) QVERIFY2(line.find(marker) == std::string::npos, line.c_str());
}

QTEST_MAIN(TestSourceEditor)
#include "test_source_editor.moc"
