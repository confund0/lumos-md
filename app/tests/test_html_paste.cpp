#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QString>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "html_paste.hpp"
#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/md/html_renderer.hpp"
#include "lumos/text_source.hpp"

namespace {

QString read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return QString::fromUtf8(ss.str().c_str());
}

std::string data_path(const char* name) {
  return std::string(LUMOS_PASTE_TEST_DATA) + "/" + name;
}

std::string paste_into_empty(const QString& html) {
  lumos::Document doc;
  auto r = paste_html(doc, lumos::Selection{0, 0}, html, "\n");
  if (!r) return "<REFUSED>";
  lumos::Document scratch(doc.text());
  for (auto& e : r->edits) scratch.apply(e);
  return scratch.text();
}

QString clean(const QString& in) {
  QString out;
  for (const QChar c : in) {
    if (c == QChar(0x00AD) || c == QChar(0x200B) || c == QChar(0xFEFF)) continue;
    out.append(c == QChar(0x00A0) ? QLatin1Char(' ') : c);
  }
  return out;
}

int flags_of(const QTextCharFormat& cf) {
  int f = 0;
  if (cf.fontWeight() >= QFont::DemiBold) f |= 1;
  if (cf.fontItalic()) f |= 2;
  if (cf.fontStrikeOut()) f |= 4;
  if (cf.fontFamilies().toString().contains(QLatin1String("monospace"), Qt::CaseInsensitive)) f |= 8;
  if (cf.isAnchor()) f |= 16;
  return f;
}

struct BlockSig {
  enum Kind { table, code, hr, heading, list, quote, plain } kind = plain;
  int heading_level = 0;
  int list_depth = 0;
  bool list_ordered = false;
  bool in_heading_flags_dropped = false;  
  QString text;
  std::vector<int> flags;  
  int rows = 0, cols = 0;
  std::vector<QString> cells;  
  std::vector<QString> code_lines;
};

void append_block_text(const QTextBlock& b, bool heading, QString& text, std::vector<int>& flags) {
  for (auto it = b.begin(); !it.atEnd(); ++it) {
    const QTextFragment frag = it.fragment();
    if (!frag.isValid()) continue;
    const QTextCharFormat cf = frag.charFormat();
    if (cf.isImageFormat()) {
      text.append(QChar(0xFFFC));
      flags.push_back(0);
      continue;
    }
    int f = flags_of(cf);
    if (heading) f &= ~1;  
    const QString t = clean(frag.text());
    for (const QChar c : t) {
      text.append(c);
      flags.push_back(f);
    }
  }
  while (!text.isEmpty() && (text.back() == QLatin1Char(' ') || text.back() == QLatin1Char('\t'))) {
    text.chop(1);
    flags.pop_back();
  }
}

std::vector<BlockSig> extract(QTextDocument& qdoc) {
  std::vector<BlockSig> out;
  QTextBlock b = qdoc.begin();
  while (b.isValid()) {
    if (QTextTable* table = qobject_cast<QTextTable*>(qdoc.frameAt(b.position()))) {
      BlockSig sig;
      sig.kind = BlockSig::table;
      sig.rows = table->rows();
      sig.cols = table->columns();
      for (int r = 0; r < sig.rows; ++r) {
        for (int c = 0; c < sig.cols; ++c) {
          QTextTableCell cell = table->cellAt(r, c);
          QString t;
          for (QTextBlock cb = qdoc.findBlock(cell.firstCursorPosition().position());
               cb.isValid() && cb.position() < cell.lastCursorPosition().position(); cb = cb.next()) {
            std::vector<int> ignore;
            QString bt;
            append_block_text(cb, false, bt, ignore);
            t += bt;
          }
          sig.cells.push_back(t);
        }
      }
      out.push_back(std::move(sig));
      b = qdoc.findBlock(table->lastPosition()).next();
      continue;
    }
    if (b.blockFormat().nonBreakableLines()) {
      BlockSig sig;
      sig.kind = BlockSig::code;
      while (b.isValid() && b.blockFormat().nonBreakableLines()) {
        sig.code_lines.push_back(clean(b.text()));
        b = b.next();
      }
      out.push_back(std::move(sig));
      continue;
    }
    if (b.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
      BlockSig sig;
      sig.kind = BlockSig::hr;
      out.push_back(std::move(sig));
      b = b.next();
      continue;
    }
    if (const int level = b.blockFormat().headingLevel(); level > 0) {
      BlockSig sig;
      sig.kind = BlockSig::heading;
      sig.heading_level = level;
      append_block_text(b, true, sig.text, sig.flags);
      out.push_back(std::move(sig));
      b = b.next();
      continue;
    }
    if (QTextList* qlist = b.textList()) {
      BlockSig sig;
      sig.kind = BlockSig::list;
      const QTextListFormat lf = qlist->format();
      sig.list_depth = lf.indent();
      sig.list_ordered = lf.style() <= QTextListFormat::ListDecimal;
      append_block_text(b, false, sig.text, sig.flags);
      out.push_back(std::move(sig));
      b = b.next();
      continue;
    }
    if (b.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0) {
      BlockSig sig;
      sig.kind = BlockSig::quote;
      append_block_text(b, false, sig.text, sig.flags);
      out.push_back(std::move(sig));
      b = b.next();
      continue;
    }
    BlockSig sig;
    append_block_text(b, false, sig.text, sig.flags);
    bool any = false;
    for (const QChar ch : sig.text)
      if (ch != QChar::LineSeparator) any = true;
    if (any) out.push_back(std::move(sig));
    b = b.next();
  }
  return out;
}

std::string render_markdown(const std::string& markdown) {
  lumos::Document scratch(markdown);
  const std::string text = scratch.text();
  const lumos::StringSource src(text);
  return lumos::md::render_html(scratch.tree(), src);
}

}  

TEST_CASE("sample 1: firefox recipe") {
  const std::string out = paste_into_empty(read_file(data_path("1-firefox-recipe.html")));
  CHECK(out ==
        "## Simple Tomato Soup\n"
        "\n"
        "A quick soup for cold days. It takes about **30 minutes** and uses only a "
        "few things you likely have at home, so it is a good choice for a weekday "
        "evening when you are "
        "tired.![Tomato soup in a bowl](https://example.com/img/soup.jpg)\n"
        "\n"
        "### Ingredients\n"
        "\n"
        "- 2 tbsp olive oil\n"
        "- 1 onion, *chopped*\n"
        "- 800 g canned tomatoes\n"
        "- ½ tsp salt\n"
        "\n"
        "### Steps\n"
        "\n"
        "1. Heat the oil in a pot.\n"
        "2. Add the onion and cook for 5 minutes.\\\n"
        "   Stir often.\n"
        "3. Add the tomatoes. See [our tips](https://example.com/tips).");
}

TEST_CASE("sample 2: chrome recipe") {
  const std::string out = paste_into_empty(read_file(data_path("2-chrome-recipe.html")));
  CHECK(out ==
        "## Simple Tomato Soup\n"
        "\n"
        "**2 tbsp** olive oil\n"
        "\n"
        "**1** onion, chopped\n"
        "\n"
        "- Heat the oil.\n"
        "- Add the onion.\n"
        "\n"
        "**Note:** salt to taste, *not* more.");
}

TEST_CASE("sample 3: signal") {
  const std::string out = paste_into_empty(read_file(data_path("3-signal.html")));
  CHECK(out ==
        "Hi! Here is the list for tomorrow:\n"
        "\n"
        "- bread\n"
        "\n"
        "- milk (2x)\n"
        "\n"
        "* 3 eggs\n"
        "\n"
        "Call me at 5. **Important** - bring the key \xF0\x9F\x94\x91 and see "
        "[https://example.com/map?a=1&b=2](https://example.com/map?a=1&b=2)\n"
        "\n"
        "`code 42` and ~~old~~");
}

TEST_CASE("sample 4: table") {
  const std::string out = paste_into_empty(read_file(data_path("4-table.html")));
  CHECK(out ==
        "#### Nutrition (per serving)\n"
        "\n"
        "| **Nutrient**   | **Amount** |\n"
        "| -------------- | ---------- |\n"
        "| Calories       | 120 kcal   |\n"
        "| Fat            | **4 g**    |\n"
        "| Salt \\| sodium | 0.8 g      |");
}

TEST_CASE("sample 5: code and quote") {
  const std::string out = paste_into_empty(read_file(data_path("5-code.html")));
  CHECK(out ==
        "Run `make install` as root:\n"
        "\n"
        "```\n"
        "cd build\n"
        "make -j4\n"
        "  sudo make install   # indented\n"
        "```\n"
        "\n"
        "> Quoted text, with a [link](https://example.com).\n"
        "\n"
        "Second quote paragraph.");
}

TEST_CASE("sample 6: escapes") {
  const std::string out = paste_into_empty(read_file(data_path("6-escapes.html")));
  CHECK(out ==
        "1. Not a list, just a sentence that starts with a number.\n"
        "\n"
        "# not a heading, and *stars* and _under_scores_ and [brackets](x) and <tag> "
        "and a\\backslash and `tick`.\n"
        "\n"
        "- not a list item either\n"
        "\n"
        "- Outer one\n"
        "\n"
        "  - Inner a\n"
        "  - Inner b with a long text that goes on and on past the eighty column "
        "limit to see if it wraps there\n"
        "\n"
        "- Outer two\n"
        "\n"
        "3. Third\n"
        "4. Fourth\n"
        "\n"
        "Line one\\\n"
        "Line two\\\n"
        "After two breaks\n"
        "\n"
        "Plain div text\n"
        "\n"
        "Second div\n"
        "\n"
        "done task\n"
        "\n"
        "# Big\n"
        "\n"
        "##### Small\n"
        "\n"
        "---\n"
        "\n"
        "After rule");
}

TEST_CASE("sample 7: firefox real") {
  const std::string out = paste_into_empty(read_file(data_path("7-firefox-real.html")));
  CHECK(out ==
        "**My daily checklist**\n"
        "\n"
        "- Check the mail\n"
        "- Water the plants\n"
        "- Tidy the desk\n"
        "- Lock the door");
}

TEST_CASE("sample 8: chromium real") {
  const std::string out = paste_into_empty(read_file(data_path("8-chromium-real.html")));
  CHECK(out ==
        "- **Short notes**: use when the idea fits in one line\n"
        "- **Long notes**: use when the idea needs more than one paragraph\n"
        "- **Shared notes**: use when more than one person edits the same page\n"
        "- **Pinned notes**: use when the note should stay at the top of the list\n"
        "- **Archived notes**: use when the note is done but kept for later (list "
        "within a list).");
}

QString strip_marker(const QString& s) {
  int i = 0;
  if (i < s.size() && (s[i] == QLatin1Char('-') || s[i] == QLatin1Char('*'))) {
    ++i;
  } else {
    const int digits = i;
    while (i < s.size() && s[i].isDigit()) ++i;
    if (i == digits || i >= s.size() || (s[i] != QLatin1Char('.') && s[i] != QLatin1Char(')'))) return s;
    ++i;
  }
  if (i < s.size() && s[i] == QLatin1Char(' ')) ++i;
  return s.mid(i);
}

void check_round_trip(const std::vector<BlockSig>& before, const std::vector<BlockSig>& after, bool tolerate_marker,
                      bool mask_strike) {
  REQUIRE(before.size() == after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    const BlockSig& a = before[i];
    const BlockSig& c = after[i];
    INFO("block " << i);
    if (tolerate_marker && a.kind == BlockSig::plain && c.kind == BlockSig::list) {
      CHECK(strip_marker(a.text).toStdString() == c.text.toStdString());
      continue;
    }
    CHECK(a.kind == c.kind);
    CHECK(a.heading_level == c.heading_level);
    CHECK(a.list_depth == c.list_depth);
    CHECK(a.list_ordered == c.list_ordered);
    CHECK(a.text.toStdString() == c.text.toStdString());
    std::vector<int> aflags = a.flags, cflags = c.flags;
    if (mask_strike) {
      for (int& f : aflags) f &= ~4;
      for (int& f : cflags) f &= ~4;
    }
    CHECK(aflags == cflags);
    CHECK(a.rows == c.rows);
    CHECK(a.cols == c.cols);
    if (a.cells.size() == c.cells.size()) {
      for (std::size_t k = 0; k < a.cells.size(); ++k) CHECK(a.cells[k].toStdString() == c.cells[k].toStdString());
    } else {
      CHECK(a.cells.size() == c.cells.size());
    }
    if (a.code_lines.size() == c.code_lines.size()) {
      for (std::size_t k = 0; k < a.code_lines.size(); ++k)
        CHECK(a.code_lines[k].toStdString() == c.code_lines[k].toStdString());
    } else {
      CHECK(a.code_lines.size() == c.code_lines.size());
    }
  }
}

TEST_CASE("round trip of all samples") {
  struct Sample {
    const char* name;
    bool tolerate_marker;
    bool mask_strike;
  };
  static const Sample samples[] = {
      {"1-firefox-recipe.html", false, false}, {"2-chrome-recipe.html", false, false},
      {"3-signal.html", true, true},           {"4-table.html", false, false},
      {"5-code.html", false, false},           {"7-firefox-real.html", false, false},
      {"8-chromium-real.html", false, false},
  };
  for (const Sample& sample : samples) {
    SECTION(sample.name) {
      const QString html = read_file(data_path(sample.name));
      QTextDocument qdoc1;
      qdoc1.setHtml(html);
      const std::vector<BlockSig> before = extract(qdoc1);

      const std::string markdown = paste_into_empty(html);
      REQUIRE(markdown != "<REFUSED>");
      const std::string rendered = render_markdown(markdown);
      QTextDocument qdoc2;
      qdoc2.setHtml(QString::fromUtf8(rendered.c_str()));
      const std::vector<BlockSig> after = extract(qdoc2);

      check_round_trip(before, after, sample.tolerate_marker, sample.mask_strike);
    }
  }
}

std::optional<std::string> paste_at(const std::string& doc_text, std::size_t offset, const QString& html) {
  lumos::Document doc(doc_text);
  auto r = paste_html(doc, lumos::Selection{offset, offset}, html, "\n");
  if (!r) return std::nullopt;
  lumos::Document scratch(doc.text());
  for (auto& e : r->edits) scratch.apply(e);
  return scratch.text();
}

void check_untouched_lines(const std::string& before_text, const std::string& after_text,
                            std::size_t changed_start, std::size_t changed_end) {
  const lumos::Document before(before_text);
  const lumos::Document after(after_text);
  std::size_t line_start = 0;
  while (line_start < before_text.size()) {
    const std::size_t nl = before_text.find('\n', line_start);
    const std::size_t line_end = nl == std::string::npos ? before_text.size() : nl;
    if (line_end < changed_start || line_start > changed_end) {
      const std::size_t new_line_start =
          line_start >= changed_end ? line_start + (after_text.size() - before_text.size()) : line_start;
      INFO("line at old offset " << line_start);
      const lumos::NodeId old_id = before.tree().node_at(line_start);
      const lumos::NodeId new_id = after.tree().node_at(new_line_start);
      std::vector<lumos::NodeKind> old_chain, new_chain;
      for (lumos::NodeId id = old_id; id != lumos::no_node; id = before.tree().node(id).parent)
        old_chain.push_back(before.tree().node(id).kind);
      for (lumos::NodeId id = new_id; id != lumos::no_node; id = after.tree().node(id).parent)
        new_chain.push_back(after.tree().node(id).kind);
      CHECK(old_chain == new_chain);
    }
    line_start = line_end + 1;
  }
}

TEST_CASE("paste C1: list into a list item, text before cursor") {
  const std::string before = "- item";
  const auto out = paste_at(before, before.size(), QStringLiteral("<ul><li>a</li><li>b</li></ul>"));
  REQUIRE(out.has_value());
  CHECK(*out == "- item\n- a\n- b");
  check_untouched_lines(before, *out, 0, before.size());
}

TEST_CASE("paste C2: list into a quote, text before cursor") {
  const std::string before = "> quote";
  const auto out = paste_at(before, before.size(), QStringLiteral("<ul><li>a</li><li>b</li></ul>"));
  REQUIRE(out.has_value());
  CHECK(*out == "> quote\n> - a\n> - b");
  check_untouched_lines(before, *out, 0, before.size());
}

TEST_CASE("paste C3: two paragraphs on a blank line between two paragraphs") {
  const std::string before = "p1\n\n\n\np2";
  const auto out = paste_at(before, 4, QStringLiteral("<p>x</p><p>y</p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "p1\n\nx\n\ny\n\np2");
  check_untouched_lines(before, *out, 4, 4);
}

TEST_CASE("paste C4: one paragraph with a bold run, mid-line") {
  const std::string before = "foo bar";
  const auto out = paste_at(before, 4, QStringLiteral("<p>x <b>y</b></p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "foo x **y**bar");
  check_untouched_lines(before, *out, 4, 4);
}

TEST_CASE("paste C5: a heading and a paragraph, mid-line") {
  const std::string before = "foo bar";
  const auto out = paste_at(before, 4, QStringLiteral("<h2>H</h2><p>z</p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "foo\n\n## H\n\nzbar");
  check_untouched_lines(before, *out, 4, 4);
}

TEST_CASE("paste C6: two paragraphs into a list item, text before cursor") {
  const std::string before = "- item";
  const auto out = paste_at(before, before.size(), QStringLiteral("<p>x</p><p>y</p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "- itemx\n\n  y");
  check_untouched_lines(before, *out, 0, before.size());
}

TEST_CASE("paste C7: two paragraphs into a quote, text before cursor") {
  const std::string before = "> quote";
  const auto out = paste_at(before, before.size(), QStringLiteral("<p>x</p><p>y</p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "> quotex\n>\n> y");
  check_untouched_lines(before, *out, 0, before.size());
}

TEST_CASE("paste R1: toggle_inline refuses on a quoted bold run") {
  const auto out = paste_at("", 0, QStringLiteral("<p>a<b>\"q\"</b>b</p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "a\"q\"b");
}

TEST_CASE("paste R2: a table with a pipe in a header cell") {
  const auto out =
      paste_at("", 0, QStringLiteral("<table><tr><th>a|b</th><th>c</th></tr><tr><td>1</td><td>2</td></tr></table>"));
  REQUIRE(out.has_value());
  lumos::Document doc(*out);
  const lumos::Format fmt = lumos::format_at(doc, lumos::Selection{0, 0});
  CHECK(fmt.table);
  CHECK(out->find("a\\|b") != std::string::npos);
  CHECK(out->find("| 2") != std::string::npos);
}

TEST_CASE("paste R3: a dangerous link keeps only its text") {
  const auto out = paste_at("", 0, QStringLiteral("<p><a href=\"javascript:alert(1)\">t</a></p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "t");
}

TEST_CASE("paste R4: a data: image keeps only its alt text") {
  const auto out =
      paste_at("", 0, QStringLiteral("<p><img src=\"data:image/png;base64,AAAA\" alt=\"pic\"></p>"));
  REQUIRE(out.has_value());
  CHECK(*out == "pic");
}

TEST_CASE("paste R5: cursor in a table cell refuses") {
  const std::string before = "| a | b |\n| - | - |\n| c | d |";
  const std::size_t offset = before.find('c');
  lumos::Document doc(before);
  auto r = paste_html(doc, lumos::Selection{offset, offset}, QStringLiteral("<p>x</p>"), "\n");
  CHECK(!r.has_value());
}

TEST_CASE("paste R5: cursor in a fenced code block refuses") {
  const std::string before = "```\ncode\n```";
  const std::size_t offset = before.find("code") + 4;
  lumos::Document doc(before);
  auto r = paste_html(doc, lumos::Selection{offset, offset}, QStringLiteral("<p>x</p>"), "\n");
  CHECK(!r.has_value());
}

TEST_CASE("paste timing on a long list") {
  std::string text;
  for (int i = 1; i <= 1000; ++i) {
    const std::string mark = std::to_string(i) + ". ";
    text += mark + "item " + std::to_string(i) + "\n";
    if (i % 10 == 0) {
      const std::string pad(mark.size(), ' ');
      text += pad + "a continuation line\n" + pad + "- nested a\n" + pad + "- nested b\n";
    }
  }
  text += "\n";
  lumos::Document doc(text);
  const QString html = read_file(data_path("1-firefox-recipe.html"));
  QElapsedTimer timer;
  timer.start();
  auto r = paste_html(doc, lumos::Selection{doc.size(), doc.size()}, html, "\n");
  std::printf("paste_timing lines=%zu ms=%lld\n", doc.buffer().line_count(), static_cast<long long>(timer.elapsed()));
  CHECK(r.has_value());
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
