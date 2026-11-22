
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include <string>
#include <utility>

#include "line_merge.hpp"
#include "merge_window.hpp"

namespace {

const MergeNames kNames{QStringLiteral("On disk")};

int filler_rows(QPlainTextEdit* pane) {
  int n = 0;
  for (const QString& line : pane->toPlainText().split(QLatin1Char('\n'))) {
    if (line.isEmpty()) ++n;
  }
  return n;
}

QColor row_background(QPlainTextEdit* pane, const QString& text) {
  for (const QTextEdit::ExtraSelection& s : pane->extraSelections()) {
    if (s.cursor.block().text() == text) return s.format.background().color();
  }
  return {};
}

LineMerge two_conflicts_merge() {
  return merge_lines("1\n2\n3\n4\n5\n", "1\nY2\n3\nY4\n5\n", "1\nO2\n3\nO4\n5\n");
}

LineMerge one_sided_merge() {
  return merge_lines("a\nb\nc\nd\n", "a\nYb\nc\nd\n", "a\nb\nc\nOd\n");
}

QRect block_rect(QPlainTextEdit* pane, int block_number) {
  return pane->cursorRect(QTextCursor(pane->document()->findBlockByNumber(block_number)));
}

bool filler_has_hatch(QPlainTextEdit* pane) {
  for (const QTextEdit::ExtraSelection& s : pane->extraSelections()) {
    if (s.cursor.block().text().isEmpty() && s.format.background().style() == Qt::BDiagPattern) return true;
  }
  return false;
}

}  

class TestMergeWindow : public QObject {
  Q_OBJECT

 private slots:
  void no_conflicts();
  void label_and_ok_as_picks_are_made();
  void click_picks_and_moves_to_next_conflict();
  void click_on_one_sided_chunk_toggles_result();
  void use_all_gives_one_side();
  void use_both_disabled_without_conflicts();
  void alignment_with_filler();
  void scroll_sync();
  void escape_rejects_ok_accepts();
  void picked_conflict_tints_panes();
  void one_sided_click_tints_left_out_side_yellow();
  void same_chunk_row_has_no_selection();
  void filler_rows_are_hatched();
  void button_cells_stay_aligned();
  void undo_redo_pick_and_click();
  void undo_redo_use_all();
  void new_pick_after_undo_clears_redo();
  void click_same_side_pushes_no_step();
  void undo_redo_shortcuts();
  void merge_png();
};

void TestMergeWindow::no_conflicts() {
  const LineMerge merge = one_sided_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  QCOMPARE(window.conflicts(), 0);
  QCOMPARE(window.current_conflict(), -1);
  QVERIFY(window.use_button()->isEnabled());
  QCOMPARE(window.conflict_label()->text(), QStringLiteral("No conflicts: all changes went in by themselves."));
  QCOMPARE(window.result(), merge_result(merge, default_picks(merge)));
}

void TestMergeWindow::label_and_ok_as_picks_are_made() {
  const LineMerge merge = two_conflicts_merge();
  QCOMPARE(merge.conflicts(), 2);
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  QVERIFY(!window.use_button()->isEnabled());
  QCOMPARE(window.conflict_label()->text(), QStringLiteral("2 conflicts left to choose"));
  QCOMPARE(window.current_conflict(), 0);

  window.pick(Pick::yours);
  QCOMPARE(window.current_conflict(), 1);
  QCOMPARE(window.conflict_label()->text(), QStringLiteral("1 conflict left to choose"));
  QVERIFY(!window.use_button()->isEnabled());

  window.pick(Pick::both);
  QCOMPARE(window.conflict_label()->text(), QStringLiteral("All chosen."));
  QVERIFY(window.use_button()->isEnabled());
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::both, Pick::open}));
}

void TestMergeWindow::click_picks_and_moves_to_next_conflict() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  QCOMPARE(window.current_conflict(), 0);

  QPlainTextEdit* yours_pane = window.pane(MergeWindow::Pane::yours);
  QTest::mouseClick(yours_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(yours_pane, 1).center());
  QCOMPARE(window.current_conflict(), 1);
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::open, Pick::open}));

  QPlainTextEdit* other_pane = window.pane(MergeWindow::Pane::other);
  QTest::mouseClick(other_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(other_pane, 1).center());
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::other, Pick::open, Pick::open, Pick::open}));
}

void TestMergeWindow::click_on_one_sided_chunk_toggles_result() {
  const LineMerge merge = one_sided_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  QVERIFY(window.result().find("Od") != std::string::npos);

  QPlainTextEdit* yours_pane = window.pane(MergeWindow::Pane::yours);
  QTest::mouseClick(yours_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(yours_pane, 3).center());
  QVERIFY(window.result().find("Od") == std::string::npos);

  QPlainTextEdit* other_pane = window.pane(MergeWindow::Pane::other);
  QTest::mouseClick(other_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(other_pane, 3).center());
  QVERIFY(window.result().find("Od") != std::string::npos);
}

void TestMergeWindow::use_all_gives_one_side() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  window.use_all(Pick::other);
  QCOMPARE(window.result(), std::string("1\nO2\n3\nO4\n5\n"));
  QVERIFY(window.use_button()->isEnabled());

  window.use_all(Pick::yours);
  QCOMPARE(window.result(), std::string("1\nY2\n3\nY4\n5\n"));
  QVERIFY(window.use_button()->isEnabled());
}

void TestMergeWindow::use_both_disabled_without_conflicts() {
  const LineMerge merge = one_sided_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  QVERIFY(!window.use_both_button()->isEnabled());
}

void TestMergeWindow::alignment_with_filler() {
  const std::string base = "a\nb\nc\nd\ne\nf\ng\nh\n";
  const std::string yours = "a\nY1\nY2\nY3\nb\nC1\nd\ne\nf\ng\nh\n";
  const std::string other = "a\nb\nO1\nO2\nd\ne\nh\n";
  const LineMerge merge = merge_lines(base, yours, other);
  QCOMPARE(merge.conflicts(), 1);
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  const int blocks = window.pane(MergeWindow::Pane::yours)->blockCount();
  QCOMPARE(window.pane(MergeWindow::Pane::other)->blockCount(), blocks);
  QCOMPARE(window.pane(MergeWindow::Pane::result)->blockCount(), blocks);

  QCOMPARE(filler_rows(window.pane(MergeWindow::Pane::yours)), 1);
  QCOMPARE(filler_rows(window.pane(MergeWindow::Pane::other)), 5);
}

void TestMergeWindow::scroll_sync() {
  std::string base, yours;
  for (int i = 0; i < 200; ++i) {
    const std::string line = "line" + std::to_string(i) + "\n";
    base += line;
    yours += (i == 100) ? ("Y100\n") : line;
  }
  const LineMerge merge = merge_lines(base, yours, base);
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  QScrollBar* result_bar = window.pane(MergeWindow::Pane::result)->verticalScrollBar();
  QVERIFY(result_bar->maximum() > 0);
  result_bar->setValue(result_bar->maximum());
  QCOMPARE(window.pane(MergeWindow::Pane::yours)->verticalScrollBar()->value(), result_bar->value());
  QCOMPARE(window.pane(MergeWindow::Pane::other)->verticalScrollBar()->value(), result_bar->value());
}

void TestMergeWindow::escape_rejects_ok_accepts() {
  {
    const LineMerge merge = two_conflicts_merge();
    MergeWindow window(merge, QStringLiteral("a.md"), kNames);
    QTimer::singleShot(0, &window, [&] { QTest::keyClick(&window, Qt::Key_Escape); });
    QVERIFY(window.exec() == QDialog::Rejected);
  }
  {
    const LineMerge merge = merge_lines("a\n", "a\n", "a\n");
    MergeWindow window(merge, QStringLiteral("a.md"), kNames);
    QTimer::singleShot(0, &window, [&] { QTest::mouseClick(window.use_button(), Qt::LeftButton); });
    QVERIFY(window.exec() == QDialog::Accepted);
  }
}

void TestMergeWindow::picked_conflict_tints_panes() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  window.pick(Pick::yours);

  const QColor yours = row_background(window.pane(MergeWindow::Pane::yours), QStringLiteral("Y2"));
  QVERIFY(yours.isValid());
  QVERIFY(yours.green() > yours.red() && yours.green() > yours.blue());

  const QColor other = row_background(window.pane(MergeWindow::Pane::other), QStringLiteral("O2"));
  QVERIFY(other.isValid());
  QVERIFY(other.red() > other.blue() && other.green() > other.blue());

  const QColor result = row_background(window.pane(MergeWindow::Pane::result), QStringLiteral("Y2"));
  QVERIFY(result.isValid());
  QVERIFY(result.green() > result.red() && result.green() > result.blue());

  window.pick(Pick::other);
  const QColor other_result = row_background(window.pane(MergeWindow::Pane::result), QStringLiteral("O4"));
  QVERIFY(other_result.isValid());
  QVERIFY(other_result.blue() > other_result.red() && other_result.blue() > other_result.green());
}

void TestMergeWindow::one_sided_click_tints_left_out_side_yellow() {
  const LineMerge merge = one_sided_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  QPlainTextEdit* yours_pane = window.pane(MergeWindow::Pane::yours);
  QTest::mouseClick(yours_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(yours_pane, 3).center());

  const QColor yours = row_background(yours_pane, QStringLiteral("d"));
  QVERIFY(yours.isValid());
  QVERIFY(yours.green() > yours.red() && yours.green() > yours.blue());

  const QColor other = row_background(window.pane(MergeWindow::Pane::other), QStringLiteral("Od"));
  QVERIFY(other.isValid());
  QVERIFY(other.red() > other.blue() && other.green() > other.blue());
}

void TestMergeWindow::same_chunk_row_has_no_selection() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  QVERIFY(!row_background(window.pane(MergeWindow::Pane::yours), QStringLiteral("1")).isValid());
}

void TestMergeWindow::filler_rows_are_hatched() {
  const std::string base = "a\nb\nc\nd\ne\nf\ng\nh\n";
  const std::string yours = "a\nY1\nY2\nY3\nb\nC1\nd\ne\nf\ng\nh\n";
  const std::string other = "a\nb\nO1\nO2\nd\ne\nh\n";
  const LineMerge merge = merge_lines(base, yours, other);
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  QVERIFY(filler_has_hatch(window.pane(MergeWindow::Pane::other)));
}

void TestMergeWindow::button_cells_stay_aligned() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  window.resize(1000, 700);
  QTest::qWait(10);

  const auto span_in_dialog = [&](QWidget* w) {
    const int left = window.mapFromGlobal(w->mapToGlobal(QPoint(0, 0))).x();
    const int right = window.mapFromGlobal(w->mapToGlobal(QPoint(w->width(), 0))).x();
    return std::pair{left, right};
  };
  const auto centre_x_in_dialog = [&](QWidget* w) {
    return window.mapFromGlobal(w->mapToGlobal(QPoint(w->width() / 2, 0))).x();
  };

  const auto [yours_left, yours_right] = span_in_dialog(window.pane(MergeWindow::Pane::yours));
  const auto [other_left, other_right] = span_in_dialog(window.pane(MergeWindow::Pane::other));
  const int yours_button_x = centre_x_in_dialog(window.use_this_button(MergeWindow::Pane::yours));
  const int other_button_x = centre_x_in_dialog(window.use_this_button(MergeWindow::Pane::other));

  QVERIFY(yours_button_x >= yours_left && yours_button_x <= yours_right);
  QVERIFY(other_button_x >= other_left && other_button_x <= other_right);
}

void TestMergeWindow::undo_redo_pick_and_click() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  QVERIFY(!window.can_undo());
  QVERIFY(!window.can_redo());
  QVERIFY(!window.undo_button()->isEnabled());
  QVERIFY(!window.redo_button()->isEnabled());

  window.pick(Pick::yours);
  QPlainTextEdit* other_pane = window.pane(MergeWindow::Pane::other);
  QTest::mouseClick(other_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(other_pane, 1).center());
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::other, Pick::open, Pick::open, Pick::open}));
  QVERIFY(window.can_undo());
  QVERIFY(!window.can_redo());
  QVERIFY(window.undo_button()->isEnabled());
  QVERIFY(!window.redo_button()->isEnabled());

  window.undo();
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::open, Pick::open}));
  QVERIFY(window.can_undo());
  QVERIFY(window.can_redo());

  window.undo();
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::open, Pick::open, Pick::open, Pick::open}));
  QVERIFY(!window.can_undo());
  QVERIFY(!window.undo_button()->isEnabled());
  QVERIFY(window.can_redo());
  QVERIFY(window.redo_button()->isEnabled());

  window.redo();
  window.redo();
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::other, Pick::open, Pick::open, Pick::open}));
  QVERIFY(window.can_undo());
  QVERIFY(!window.can_redo());
}

void TestMergeWindow::undo_redo_use_all() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  window.pick(Pick::yours);
  window.pick(Pick::both);
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::both, Pick::open}));

  window.use_all(Pick::other);
  QCOMPARE(window.result(), std::string("1\nO2\n3\nO4\n5\n"));

  window.undo();
  QCOMPARE(window.result(), merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::both, Pick::open}));
}

void TestMergeWindow::new_pick_after_undo_clears_redo() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  window.pick(Pick::yours);
  window.undo();
  QVERIFY(window.can_redo());

  window.pick(Pick::other);
  QVERIFY(!window.can_redo());
}

void TestMergeWindow::click_same_side_pushes_no_step() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);

  window.pick(Pick::yours);
  QVERIFY(window.can_undo());

  QPlainTextEdit* yours_pane = window.pane(MergeWindow::Pane::yours);
  QTest::mouseClick(yours_pane->viewport(), Qt::LeftButton, Qt::NoModifier, block_rect(yours_pane, 1).center());

  window.undo();
  QVERIFY(!window.can_undo());  
}

void TestMergeWindow::undo_redo_shortcuts() {
  const LineMerge merge = two_conflicts_merge();
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  window.show();
  QVERIFY(QTest::qWaitForWindowActive(&window));

  window.pick(Pick::yours);
  QVERIFY(window.can_undo());

  QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
  QVERIFY(!window.can_undo());
  QVERIFY(window.can_redo());

  QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
  QVERIFY(window.can_undo());
  QVERIFY(!window.can_redo());
}

void TestMergeWindow::merge_png() {
  if (!qEnvironmentVariableIsSet("LUMOS_MERGE_PNG")) return;
  const std::string base = "1\n2\n3\n4\n5\n6\n7\n8\n9\n";
  const std::string yours = "1\nY2\n3\nY4\n5\nY6a\nY6b\n7\n8\n9\n";
  const std::string other = "1\nO2\n3\nO4\n5\n6\n7\nO8\n9\n";
  const LineMerge merge = merge_lines(base, yours, other);
  MergeWindow window(merge, QStringLiteral("a.md"), kNames);
  window.pick(Pick::yours);

  window.resize(1200, 800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(window.grab().save(qEnvironmentVariable("LUMOS_MERGE_PNG")));
}

QTEST_MAIN(TestMergeWindow)
#include "test_merge_window.moc"
