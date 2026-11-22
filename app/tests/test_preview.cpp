
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "document_tab.hpp"
#include "lumos/document.hpp"
#include "lumos/qt/source_editor.hpp"
#include "lumos/tools.hpp"
#include "main_window.hpp"
#include "preview.hpp"

namespace {

class TestablePreviewView : public PreviewView {
 public:
  using PreviewView::loadResource;
};

QAction* find_action(MainWindow& window, const QString& text) {
  for (QAction* a : window.findChildren<QAction*>())
    if (QString(a->text()).remove(QChar('&')) == text) return a;
  return nullptr;
}

QString write_file(const QDir& dir, const QString& name, const QString& text) {
  const QString path = dir.filePath(name);
  QFile f(path);
  if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
  return path;
}

}  

class TestPreview : public QObject {
  Q_OBJECT

 private slots:
  void task_items();
  void code_block_has_no_empty_last_line();
  void safe_mode();
  void over_cap();
  void links();
  void images();
  void scroll();
  void context_menu();
};

class TestPreviewWindow : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();

  void hidden_by_default_no_render_on_edit();
  void show_preview_renders_once_and_tracks_edits();
  void splitter_buttons_toggle_and_restore();
  void off_from_preview_full_gives_editor_full_width();
  void settings_round_trip_split_and_shown();
  void tab_switch_renders_new_tab();
  void close_last_tab_shows_blank();
  void scroll_follows_editor();
  void broken_link_shows_notice();
  void zoom_changes_preview_font();
  void preview_png();

 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<MainWindow> window_;
  QString settings_file_;
};

void TestPreview::task_items() {
  const lumos::Document doc("- [ ] a\n- [x] b\n");
  PreviewView view;
  view.show_document(doc, QString());
  QVERIFY(view.toPlainText().contains(QStringLiteral("☐ a")));
  QVERIFY(view.toPlainText().contains(QStringLiteral("☑ b")));
  QCOMPARE(view.render_count(), 1);
}

void TestPreview::code_block_has_no_empty_last_line() {
  const lumos::Document doc("```\none\ntwo\n```\n\nafter\n");
  PreviewView view;
  view.show_document(doc, QString());
  QCOMPARE(view.toPlainText().trimmed(), QStringLiteral("one\ntwo\nafter"));
}

void TestPreview::safe_mode() {
  const lumos::Document doc("<script>alert(1)</script>\n\nok\n");
  PreviewView view;
  view.show_document(doc, QString());
  QVERIFY(!view.toPlainText().contains(QStringLiteral("alert")));
  QCOMPARE(view.toPlainText().trimmed(), QStringLiteral("ok"));
}

void TestPreview::over_cap() {
  std::string text = "[x]: /u \"" + std::string(100000, 'a') + "\"\n\n";
  for (int i = 0; i < 200; ++i) text += "[x] ";
  const lumos::Document doc(text);
  PreviewView view;
  view.show_document(doc, QString());
  QCOMPARE(view.toPlainText(), QStringLiteral("The preview is too large (over 16 MiB of HTML)."));
  QVERIFY(view.anchors().empty());
}

void TestPreview::links() {
  const lumos::Document doc("[l](http://x.org/a)\n\n[r]\n\n[r]: ../b.md\n");
  PreviewView view;
  view.show_document(doc, QString());

  std::vector<lumos::LinkTarget> seen;
  view.set_link_clicked([&](const lumos::LinkTarget& t) { seen.push_back(t); });

  view.anchorClicked(QUrl(QStringLiteral("lumos-link:0")));
  view.anchorClicked(QUrl(QStringLiteral("lumos-link:1")));
  QCOMPARE(seen.size(), std::size_t{2});
  QCOMPARE(seen[0].kind, lumos::md::UrlKind::link);
  QCOMPARE(QString::fromStdString(seen[0].url), QStringLiteral("http://x.org/a"));
  QCOMPARE(seen[0].form, lumos::UrlForm::scheme_url);
  QCOMPARE(QString::fromStdString(seen[1].url), QStringLiteral("../b.md"));
  QCOMPARE(seen[1].form, lumos::UrlForm::relative_path);
  QCOMPARE(seen[0].start, std::size_t{0});
  QCOMPARE(seen[0].end, std::size_t{0});

  view.anchorClicked(QUrl(QStringLiteral("lumos-link:9")));
  view.anchorClicked(QUrl(QStringLiteral("http://x.org")));
  QCOMPARE(seen.size(), std::size_t{2});
}

void TestPreview::images() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString png_path = dir.filePath(QStringLiteral("pic.png"));
  QImage source(1600, 100, QImage::Format_RGB32);
  source.fill(Qt::white);
  QVERIFY(source.save(png_path));
  const QString note_path = dir.filePath(QStringLiteral("note.md"));
  const QString expected_url = QUrl::fromLocalFile(png_path).toString(QUrl::FullyEncoded);

  const lumos::Document doc("![alt](pic.png)\n\n![far](http://x.org/i.png)\n");
  TestablePreviewView view;
  view.show_document(doc, note_path);

  const QString html = view.toHtml();
  QVERIFY(html.contains(expected_url));
  QVERIFY(!html.contains(QStringLiteral("x.org")));
  QVERIFY(view.toPlainText().contains(QStringLiteral("far")));

  const QVariant resource = view.loadResource(QTextDocument::ImageResource, QUrl(expected_url));
  const QImage image = resource.value<QImage>();
  QVERIFY(!image.isNull());
  QVERIFY(image.width() / image.devicePixelRatio() <= 800);

  const QVariant resource_again = view.loadResource(QTextDocument::ImageResource, QUrl(expected_url));
  QCOMPARE(resource_again.value<QImage>().cacheKey(), image.cacheKey());
}

void TestPreview::scroll() {
  std::string text;
  for (int i = 1; i <= 200; ++i) text += "para " + std::to_string(i) + "\n\n";
  text += "| h |\n|---|\n| r1 |\n| r2 |\n";
  const lumos::Document doc(text);

  PreviewView view;
  view.resize(400, 300);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  view.show_document(doc, QString());

  const auto& anchor_list = view.anchors();
  QVERIFY(anchor_list.size() >= 201);
  for (std::size_t i = 1; i < anchor_list.size(); ++i) QVERIFY(anchor_list[i].second > anchor_list[i - 1].second);
  QVERIFY(anchor_list.back().second > anchor_list[199].second);

  QScrollBar* const bar = view.verticalScrollBar();
  const int expected_k = std::clamp(anchor_list[100].second, bar->minimum(), bar->maximum());
  view.scroll_to_offset(anchor_list[100].first);
  QCOMPARE(bar->value(), expected_k);

  const std::size_t mid_offset = (anchor_list[100].first + anchor_list[101].first) / 2;
  view.scroll_to_offset(mid_offset);
  const int mid_value = bar->value();
  const int expected_101 = std::clamp(anchor_list[101].second, bar->minimum(), bar->maximum());
  QVERIFY(mid_value > expected_k);
  QVERIFY(mid_value < expected_101);

  view.scroll_to_offset(0);
  QCOMPARE(bar->value(), 0);

  int previous = -1;
  for (std::size_t offset = 0; offset <= doc.size(); offset += 37) {
    view.scroll_to_offset(offset);
    const int value = bar->value();
    QVERIFY(value >= previous);
    previous = value;
  }
}

void TestPreview::context_menu() {
  const lumos::Document doc("some text\n");
  PreviewView view;
  view.show_document(doc, QString());

  QMenu* const menu = view.make_context_menu();
  QCOMPARE(menu->actions().size(), 2);
  QVERIFY(!menu->actions()[0]->isEnabled());

  view.selectAll();
  QMenu* const menu_after_select = view.make_context_menu();
  QVERIFY(menu_after_select->actions()[0]->isEnabled());
}

namespace {

QToolButton* splitter_button(MainWindow& window, const char* name) {
  return window.preview_splitter_for_tests()->handle(1)->findChild<QToolButton*>(QString::fromLatin1(name));
}

QList<int> sizes(MainWindow& window) { return window.preview_splitter_for_tests()->sizes(); }

void type_text(MainWindow& window, const QString& text) {
  QTest::keyClicks(window.current_tab()->editor(), text);
}

}  

void TestPreviewWindow::init() {
  dir_ = std::make_unique<QTemporaryDir>();
  QVERIFY(dir_->isValid());
  settings_file_ = QDir(dir_->path()).filePath(QStringLiteral("settings.ini"));
  window_ = std::make_unique<MainWindow>(settings_file_);
  window_->resize(1000, 600);
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
}

void TestPreviewWindow::cleanup() {
  window_.reset();
  dir_.reset();
}

void TestPreviewWindow::hidden_by_default_no_render_on_edit() {
  PreviewView* preview = window_->preview_for_tests();
  QVERIFY(preview->isHidden());
  type_text(*window_, QStringLiteral("abc"));
  QTest::qWait(500);
  QCOMPARE(preview->render_count(), 0);
}

void TestPreviewWindow::show_preview_renders_once_and_tracks_edits() {
  PreviewView* preview = window_->preview_for_tests();
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  QVERIFY(!preview->isHidden());
  QCOMPARE(preview->render_count(), 1);
  QVERIFY(sizes(*window_)[0] > 0 && sizes(*window_)[1] > 0);
  QVERIFY(QSettings(settings_file_, QSettings::IniFormat).value(QStringLiteral("view/preview")).toBool());
  type_text(*window_, QStringLiteral("x"));
  QCOMPARE(preview->render_count(), 1);
  QVERIFY(QTest::qWaitFor([&] { return preview->render_count() == 2; }, 2000));
  QVERIFY(preview->toPlainText().contains(QStringLiteral("x")));
}

void TestPreviewWindow::splitter_buttons_toggle_and_restore() {
  PreviewView* preview = window_->preview_for_tests();
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  QToolButton* left = splitter_button(*window_, "preview_full_button");
  QToolButton* right = splitter_button(*window_, "editor_full_button");
  QVERIFY(left && right);
  const QList<int> split = sizes(*window_);

  left->click();
  QCOMPARE(sizes(*window_)[0], 0);
  QVERIFY(!left->isEnabled());
  QVERIFY(right->isEnabled());
  right->click();
  QCOMPARE(sizes(*window_), split);

  right->click();
  QCOMPARE(sizes(*window_)[1], 0);
  QVERIFY(!right->isEnabled());
  const int renders = preview->render_count();
  type_text(*window_, QStringLiteral("y"));
  QTest::qWait(500);
  QCOMPARE(preview->render_count(), renders);
  left->click();
  QCOMPARE(sizes(*window_), split);
  QCOMPARE(preview->render_count(), renders + 1);
  QVERIFY(preview->toPlainText().contains(QStringLiteral("y")));
}

void TestPreviewWindow::off_from_preview_full_gives_editor_full_width() {
  QAction* show = find_action(*window_, QStringLiteral("Show Preview"));
  show->trigger();
  splitter_button(*window_, "preview_full_button")->click();
  QCOMPARE(sizes(*window_)[0], 0);
  show->trigger();
  QVERIFY(window_->preview_for_tests()->isHidden());
  QVERIFY(QTest::qWaitFor(
      [&] { return window_->tabs_for_tests()->width() == window_->preview_splitter_for_tests()->width(); }, 1000));
  show->trigger();
  QVERIFY(sizes(*window_)[0] > 0 && sizes(*window_)[1] > 0);
}

void TestPreviewWindow::settings_round_trip_split_and_shown() {
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  window_->preview_splitter_for_tests()->setSizes({300, 682});
  emit window_->preview_splitter_for_tests()->splitterMoved(300, 1);
  const QList<int> split = sizes(*window_);
  const double share = static_cast<double>(split[1]) / (split[0] + split[1]);
  QVERIFY(window_->close());
  window_.reset();
  window_ = std::make_unique<MainWindow>(settings_file_);
  window_->resize(1000, 600);
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  QVERIFY(!window_->preview_for_tests()->isHidden());
  const QList<int> now = sizes(*window_);
  QVERIFY(std::abs(static_cast<double>(now[1]) / (now[0] + now[1]) - share) < 0.01);
}

void TestPreviewWindow::tab_switch_renders_new_tab() {
  QDir dir(dir_->path());
  QVERIFY(window_->open_file(write_file(dir, QStringLiteral("a.md"), QStringLiteral("alpha\n"))));
  QVERIFY(window_->open_file(write_file(dir, QStringLiteral("b.md"), QStringLiteral("beta\n"))));
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  PreviewView* preview = window_->preview_for_tests();
  QVERIFY(preview->toPlainText().contains(QStringLiteral("beta")));
  window_->tabs_for_tests()->setCurrentIndex(0);
  QVERIFY(preview->toPlainText().contains(QStringLiteral("alpha")));
}

void TestPreviewWindow::close_last_tab_shows_blank() {
  QDir dir(dir_->path());
  QVERIFY(window_->open_file(write_file(dir, QStringLiteral("a.md"), QStringLiteral("alpha\n"))));
  QCOMPARE(window_->tabs_for_tests()->count(), 1);
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  PreviewView* preview = window_->preview_for_tests();
  QVERIFY(preview->toPlainText().contains(QStringLiteral("alpha")));
  QVERIFY(window_->close_tab(0));
  QCOMPARE(window_->tabs_for_tests()->count(), 1);
  QVERIFY(!preview->toPlainText().contains(QStringLiteral("alpha")));
}

void TestPreviewWindow::scroll_follows_editor() {
  QString text;
  for (int i = 0; i < 300; ++i) text += QStringLiteral("para %1\n\n").arg(i);
  QVERIFY(window_->open_file(write_file(QDir(dir_->path()), QStringLiteral("long.md"), text)));
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  PreviewView* preview = window_->preview_for_tests();
  QScrollBar* editor_bar = window_->current_tab()->editor()->verticalScrollBar();
  editor_bar->setValue(editor_bar->maximum() / 2);
  const int middle = preview->verticalScrollBar()->value();
  QVERIFY(middle > 0);
  editor_bar->setValue(editor_bar->maximum() * 3 / 4);
  QVERIFY(preview->verticalScrollBar()->value() > middle);
}

void TestPreviewWindow::broken_link_shows_notice() {
  QVERIFY(window_->open_file(
      write_file(QDir(dir_->path()), QStringLiteral("links.md"), QStringLiteral("[m](missing.md)\n"))));
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  PreviewView* preview = window_->preview_for_tests();
  emit preview->anchorClicked(QUrl(QStringLiteral("lumos-link:0")));
  auto* notice = window_->findChild<QLabel*>(QStringLiteral("notice"));
  QVERIFY(notice);
  QVERIFY(notice->text().contains(QStringLiteral("Cannot open missing.md")));
}

void TestPreviewWindow::zoom_changes_preview_font() {
  PreviewView* preview = window_->preview_for_tests();
  const qreal before = preview->font().pointSizeF();
  find_action(*window_, QStringLiteral("Zoom in"))->trigger();
  QVERIFY(preview->font().pointSizeF() > before);
}

void TestPreviewWindow::preview_png() {
  if (!qEnvironmentVariableIsSet("LUMOS_PREVIEW_PNG")) return;
  QVERIFY(window_->open_file(QStringLiteral(LUMOS_SOURCE_DIR "/tests/manual/preview.md")));
  if (qEnvironmentVariableIsSet("LUMOS_PREVIEW_PAPER")) find_action(*window_, QStringLiteral("Paper look"))->trigger();
  find_action(*window_, QStringLiteral("Show Preview"))->trigger();
  window_->resize(1400, 800);
  QTest::qWait(100);
  QScrollBar* bar = window_->current_tab()->editor()->verticalScrollBar();
  bar->setValue(qEnvironmentVariableIntValue("LUMOS_PREVIEW_SCROLL"));
  QVERIFY(window_->grab().save(qEnvironmentVariable("LUMOS_PREVIEW_PNG")));
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  int failed = 0;
  {
    TestPreview widget_tests;
    failed += QTest::qExec(&widget_tests, argc, argv);
  }
  {
    TestPreviewWindow window_tests;
    failed += QTest::qExec(&window_tests, argc, argv);
  }
  return failed == 0 ? 0 : 1;
}

#include "test_preview.moc"
