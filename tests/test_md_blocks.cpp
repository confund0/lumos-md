
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "lumos/log.hpp"
#include "lumos/md/html_renderer.hpp"
#include "lumos/parser.hpp"
#include "md/block_state.hpp"
#include "md/line_reader.hpp"
#include "md/md_parser.hpp"
#include "md/ref_index.hpp"
#include "md/scanners.hpp"

namespace md = lumos::md;
using lumos::NodeKind;

namespace {

class ChunkedSource final : public lumos::TextSource {
 public:
  ChunkedSource(std::string_view text, std::size_t n) : text_(text), lines_(text), n_(n) {}
  std::size_t size() const override { return text_.size(); }
  std::string_view chunk_at(std::size_t offset) const override {
    if (offset > text_.size()) throw std::out_of_range("chunk_at");
    const std::size_t end = std::min(text_.size(), (offset / n_ + 1) * n_);
    return text_.substr(offset, end - offset);
  }
  std::string slice(std::size_t offset, std::size_t length) const override { return lines_.slice(offset, length); }
  std::size_t line_of(std::size_t offset) const override { return lines_.line_of(offset); }
  std::size_t line_start(std::size_t line) const override { return lines_.line_start(line); }
  std::size_t line_count() const override { return lines_.line_count(); }

 private:
  std::string_view text_;
  lumos::StringSource lines_;
  std::size_t n_;
};

struct Parsed {
  std::string tree;
  std::string html;
};

Parsed parse(std::string_view text, const lumos::TextSource* src = nullptr) {
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  const lumos::StringSource s(text);
  const lumos::TextSource& use = src ? *src : s;
  p->parse(use);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  md::HtmlOptions opt;
  opt.unsafe = true;
  return {t.to_debug_string(), md::render_html(t, use, opt)};
}

std::string html(std::string_view text) { return parse(text).html; }

struct FrontMatterCase {
  std::optional<std::pair<std::size_t, std::size_t>> range;
  std::string html;
};
FrontMatterCase parse_front_matter(std::string_view text) {
  auto p = md::make_markdown_parser(lumos::Dialect{});
  const lumos::StringSource s(text);
  p->parse(s);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  FrontMatterCase c;
  if (t.size() > 1 && t.node(1).kind == NodeKind::front_matter) c.range = {t.node(1).start, t.node(1).end};
  md::HtmlOptions opt;
  opt.unsafe = true;
  c.html = md::render_html(t, s, opt);
  return c;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  

TEST_CASE("md blocks: front matter") {
  {
    const auto c = parse_front_matter("---\na: b\n---\nx\n");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 13});
    CHECK(c.html == "<p>x</p>\n");
  }
  {
    const auto c = parse_front_matter("+++\na = 1\n+++\n# h\n");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 14});
    CHECK(c.html == "<h1>h</h1>\n");
  }
  {
    const auto c = parse_front_matter("---\n---\n");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 8});
    CHECK(c.html.empty());
  }
  {
    const auto c = parse_front_matter("---  \nx\n---\t\n");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 13});
    CHECK(c.html.empty());
  }
  {
    const auto c = parse_front_matter("---\nx\n---");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 9});
    CHECK(c.html.empty());
  }
  {
    const auto c = parse_front_matter("\xEF\xBB\xBF---\nx\n---\ny");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{3, 13});
    CHECK(c.html == "<p>y</p>\n");
  }
  {
    const auto c = parse_front_matter("---\r\na: b\r\n---\r\nx\r\n");
    REQUIRE(c.range);
    CHECK(*c.range == std::pair<std::size_t, std::size_t>{0, 16});
    CHECK(c.html == "<p>x</p>\n");
  }

  for (std::string_view text :
       {"---\na\n", " ---\nx\n---\n", "---\nx\n+++\n", "----\nx\n----\n", "---\nx\n--- a\n", "x\n---\na\n---\n"}) {
    INFO(text);
    const auto c = parse_front_matter(text);
    CHECK_FALSE(c.range);
    lumos::Dialect d{};
    d.front_matter = false;
    auto p = md::make_markdown_parser(d);
    const lumos::StringSource s(text);
    p->parse(s);
    md::HtmlOptions opt;
    opt.unsafe = true;
    CHECK(c.html == md::render_html(p->tree(), s, opt));
  }
}

TEST_CASE("md lines: line endings and chunk borders") {
  const std::string text = "ab\r\ncd\ref\n\ngh";
  for (std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{100}}) {
    ChunkedSource src(text, n);
    md::LineReader r(src);
    std::vector<std::string> lines;
    std::vector<int> endings;
    for (std::size_t off = 0; off < text.size();) {
      md::Line l = r.read(off);
      lines.emplace_back(l.text);
      endings.push_back(l.ending);
      off = l.end();
    }
    CHECK(lines == std::vector<std::string>{"ab", "cd", "ef", "", "gh"});
    CHECK(endings == std::vector<int>{2, 1, 1, 1, 0});
    CHECK(r.read(text.size()).text.empty());
    CHECK(r.read(text.size()).ending == 0);
  }
}

TEST_CASE("md lines: cursor columns and partial tabs") {
  md::LineCursor c(" \tab");
  CHECK(c.indent() == 4);
  CHECK(c.first_nonspace() == 2);
  c.advance(2, true);  
  CHECK(c.pos() == 1);
  CHECK(c.partial());
  CHECK(c.column() == 2);
  CHECK(c.partial_rest() == 2);
  CHECK(c.content_pos() == 2);
  CHECK(c.indent() == 2);
  c.advance(2, true);  
  CHECK(c.pos() == 2);
  CHECK_FALSE(c.partial());
  CHECK(c.column() == 4);
  md::LineCursor b("\tx");
  b.advance(1, false);  
  CHECK(b.column() == 4);
  CHECK(b.peek() == 'x');
  md::LineCursor e("   ");
  CHECK(e.blank());
  e.skip_to_first_nonspace();
  CHECK(e.at_end());
}

TEST_CASE("md scanners: block starts") {
  std::size_t at = 0;
  CHECK(md::scan_atx_heading_start("## a", &at) == 2);
  CHECK(at == 3);
  CHECK(md::scan_atx_heading_start("#", &at) == 1);
  CHECK(md::scan_atx_heading_start("#a", &at) == 0);
  CHECK(md::scan_atx_heading_start("####### a", &at) == 0);
  const md::AtxContent ac = md::scan_atx_content("## foo ##  ", 3);
  CHECK(ac.content_end == 6);
  CHECK(ac.closing_start == 7);
  CHECK(ac.closing_end == 9);
  CHECK(md::scan_atx_content("# foo#", 2).closing_start == 6);
  CHECK(md::scan_atx_content("### ###", 4).content_end == 4);

  CHECK(md::scan_fence_open("```js") == 3);
  CHECK(md::scan_fence_open("``` a`b") == 0);
  CHECK(md::scan_fence_open("~~~~ a`b") == 4);
  CHECK(md::scan_fence_open("``") == 0);
  CHECK(md::scan_fence_close("````  ") == 4);
  CHECK(md::scan_fence_close("``` x") == 0);

  std::size_t run = 0;
  CHECK(md::scan_setext_underline("=== ", &run) == 1);
  CHECK(run == 3);
  CHECK(md::scan_setext_underline("--", &run) == 2);
  CHECK(md::scan_setext_underline("- -", &run) == 0);

  std::size_t fail = 0;
  CHECK(md::scan_thematic_break("* * *", &fail));
  CHECK_FALSE(md::scan_thematic_break("- - x", &fail));
  CHECK(fail == 4);
  CHECK_FALSE(md::scan_thematic_break("+++", &fail));
  CHECK(fail == 0);

  auto m = md::scan_list_marker("- a", false);
  REQUIRE(m);
  CHECK(m->ch == '-');
  CHECK(m->width == 1);
  m = md::scan_list_marker("123456789) a", false);
  REQUIRE(m);
  CHECK(m->ordered);
  CHECK(m->number == 123456789);
  CHECK(m->ch == ')');
  CHECK(m->width == 10);
  CHECK_FALSE(md::scan_list_marker("1234567890. a", false));
  CHECK(md::scan_list_marker("-\va", false));  
  CHECK_FALSE(md::scan_list_marker("-a", false));
  CHECK(md::scan_list_marker("-", false));
  CHECK_FALSE(md::scan_list_marker("-", true));  
  CHECK_FALSE(md::scan_list_marker("2. a", true));
  CHECK(md::scan_list_marker("1. a", true));
}

TEST_CASE("md scanners: HTML blocks") {
  CHECK(md::scan_html_block_start("<script>") == 1);
  CHECK(md::scan_html_block_start("<PRE") == 1);
  CHECK(md::scan_html_block_start("<pre/>") == 0);
  CHECK(md::scan_html_block_start("<!-- x") == 2);
  CHECK(md::scan_html_block_start("<?php") == 3);
  CHECK(md::scan_html_block_start("<!DOCTYPE") == 4);
  CHECK(md::scan_html_block_start("<!doctype") == 4);
  CHECK(md::scan_html_block_start("<![CDATA[") == 5);
  CHECK(md::scan_html_block_start("<div>") == 6);
  CHECK(md::scan_html_block_start("</DIV") == 6);
  CHECK(md::scan_html_block_start("<search>") == 6);
  CHECK(md::scan_html_block_start("<div/>") == 6);
  CHECK(md::scan_html_block_start("<divx>") == 0);
  CHECK(md::scan_html_block_start_7("<a href=\"x\">  ") == 7);
  CHECK(md::scan_html_block_start_7("<pre/>") == 7);
  CHECK(md::scan_html_block_start_7("</a >") == 7);
  CHECK(md::scan_html_block_start_7("<a\vb=c>") == 7);
  CHECK(md::scan_html_block_start_7("<a> b") == 0);
  CHECK(md::scan_html_block_start_7("<a b=>") == 0);
  CHECK(md::html_block_ends_on_line(1, "x </STYLE> y"));
  CHECK(md::html_block_ends_on_line(2, "-->"));
  CHECK(md::html_block_ends_on_line(4, "a>"));
  CHECK_FALSE(md::html_block_ends_on_line(5, "]] >"));
  CHECK_FALSE(md::html_block_ends_on_line(6, "</div>"));
}

TEST_CASE("md state: arena and line table") {
  md::StateArena a;
  md::OpenBlock q;
  q.kind = md::OpenKind::block_quote;
  const md::StateId s1 = a.push(md::document_state, q);
  const md::StateId s2 = a.push(md::document_state, q);
  CHECK(s1 == s2);
  CHECK(a.get(s1).depth == 1);
  md::OpenBlock item;
  item.kind = md::OpenKind::list_item;
  item.width = 2;
  item.small = md::item_empty;
  const md::StateId i1 = a.push(s1, item);
  const md::StateId i2 = a.with_flags(i1, 0);
  CHECK(i1 != i2);
  CHECK(a.get(i2).parent == s1);
  const std::size_t before = a.size();
  const std::vector<md::StateId> live{i2};
  const auto map = a.compact(live);
  CHECK(a.size() == before - 1);
  CHECK(map[i1] == md::no_state);
  CHECK(a.get(map[i2]).kind == md::OpenKind::list_item);

  md::LineTable t;
  t.push(0, 0);
  t.push(4, 1);
  t.push(9, 2);
  CHECK(t.line_of(5) == 1);
  CHECK(t.line_of(100) == 2);
  const std::vector<std::size_t> starts{4, 6};
  const std::vector<md::StateId> states{7, 8};
  t.replace(1, 2, starts, states, 2);
  CHECK(t.size() == 4);
  CHECK(t.start(3) == 11);
  CHECK(t.state(2) == 8);
}

TEST_CASE("md blocks: paragraphs with soft and hard breaks") {
  const Parsed p = parse("a  \nb\\\nc \n  d\n");
  CHECK(p.tree ==
        "(document@0..14 (paragraph@0..14 (text@0..1) (hard_break@1..4) (text@4..5) (hard_break@5..7) "
        "(text@7..8) (soft_break@8..10) (block_continuation@10..12) (text@12..13)))");
  CHECK(p.html == "<p>a<br />\nb<br />\nc\nd</p>\n");
  CHECK(html("a\\\n\v") == "<p>a\\</p>\n");
  CHECK(html("a\n\vb") == "<p>a\n\vb</p>\n");
}

TEST_CASE("md inlines: code span, autolink and emphasis markers, tree shape") {
  CHECK(parse("`a` <ab:y> **b**").tree ==
        "(document@0..16 (paragraph@0..16 (code_span@0..3 (code_span_delimiter@0..1) (text@1..2) "
        "(code_span_delimiter@2..3)) (text@3..4) (autolink@4..10 (link_delimiter@4..5) (text@5..9) "
        "(link_delimiter@9..10)) (text@10..11) (strong@11..16 (emphasis_delimiter@11..13) (text@13..14) "
        "(emphasis_delimiter@14..16))))");
  const Parsed q = parse("> `a\n> b`\n");
  CHECK(q.tree ==
        "(document@0..10 (block_quote@0..10 (block_quote_marker@0..1) (paragraph@2..10 (code_span@2..9 "
        "(code_span_delimiter@2..3) (text@3..8 (block_continuation@5..7)) (code_span_delimiter@8..9)))))");
  CHECK(q.html == "<blockquote>\n<p><code>a b</code></p>\n</blockquote>\n");
}

TEST_CASE("md inlines: link and image markers, tree shape") {
  CHECK(parse("[a](<b> \"t\")").tree ==
        "(document@0..12 (paragraph@0..12 (link@0..12 (link_delimiter@0..1) (link_text@1..2 (text@1..2)) "
        "(link_delimiter@2..3) (link_delimiter@3..4) (link_delimiter@4..5) (link_destination@5..6) "
        "(link_delimiter@6..7) (link_title@8..11) (link_delimiter@11..12))))");
  CHECK(parse("![a][b] [c][] [d]()\n\n[b]: /u").tree ==
        "(document@0..28 (paragraph@0..20 (image@0..7 variant=1 (link_delimiter@0..2) (link_text@2..3 "
        "(text@2..3)) (link_delimiter@3..4) (link_label@4..7)) (text@7..14) (link@14..19 (link_delimiter@14..15) "
        "(link_text@15..16 (text@15..16)) (link_delimiter@16..17) (link_delimiter@17..18) "
        "(link_delimiter@18..19))) (link_reference_definition@21..28 (link_label@21..24) "
        "(link_delimiter@24..25) (link_destination@26..28)))");
}

TEST_CASE("md blocks: block quote with prefixes and a lazy line") {
  const Parsed p = parse("> a\n> b\nc\n>\n");
  CHECK(p.tree ==
        "(document@0..12 (block_quote@0..12 (block_quote_marker@0..1) (paragraph@2..10 (text@2..3) "
        "(soft_break@3..4) (block_continuation@4..6) (text@6..7) (soft_break@7..8) (text@8..9)) "
        "(block_continuation@10..11)))");
  CHECK(p.html == "<blockquote>\n<p>a\nb\nc</p>\n</blockquote>\n");
}

TEST_CASE("md blocks: lists, markers and tightness") {
  const Parsed p = parse("- a\n- b\n\n3) c\n");
  CHECK(p.tree ==
        "(document@0..14 (list@0..8 tight variant=45 (list_item@0..4 (list_marker@0..1) (paragraph@2..4 "
        "(text@2..3))) (list_item@4..8 (list_marker@4..5) (paragraph@6..8 (text@6..7)))) "
        "(list@9..14 ordered tight variant=41 (list_item@9..14 (list_marker@9..11) (paragraph@12..14 "
        "(text@12..13)))))");
  CHECK(p.html == "<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n<ol start=\"3\">\n<li>c</li>\n</ol>\n");
  CHECK(html("- a\n\n- b\n") == "<ul>\n<li>\n<p>a</p>\n</li>\n<li>\n<p>b</p>\n</li>\n</ul>\n");
  CHECK(html("- <!--\n  x\n\n- b\n") == "<ul>\n<li>\n<!--\nx\n\n</li>\n<li>\n<p>b</p>\n</li>\n</ul>\n");
  CHECK(html("- ```\n  a\n\n  ```\n- b\n") == "<ul>\n<li>\n<pre><code>a\n\n</code></pre>\n</li>\n<li>b</li>\n</ul>\n");
  CHECK(html("> a\n2. b\n") == "<blockquote>\n<p>a</p>\n</blockquote>\n<ol start=\"2\">\n<li>b</li>\n</ol>\n");
  CHECK(html("-\n  \n\n  a\n") == "<ul>\n<li>a</li>\n</ul>\n");
  CHECK(html("-\n\n  a\n") == "<ul>\n<li></li>\n</ul>\n<p>a</p>\n");
}

TEST_CASE("md blocks: fenced code with prefixes and partial tabs") {
  const Parsed p = parse("> ```js x\n>\t\tfoo\n> ```\n");
  CHECK(p.tree ==
        "(document@0..23 (block_quote@0..23 (block_quote_marker@0..1) (code_block@2..23 fenced "
        "(code_fence_delimiter@2..5) (info_string@5..9) (code_content@10..17 (block_continuation@10..12 vs=2)) "
        "(block_continuation@17..19) (code_fence_delimiter@19..22))))");
  CHECK(p.html == "<blockquote>\n<pre><code class=\"language-js\">  \tfoo\n</code></pre>\n</blockquote>\n");
  const std::string open(300, '`');
  const std::string close(260, '`');
  CHECK(html(open + "\na\n" + close + "\nb\n") == "<pre><code>a\n</code></pre>\n<p>b</p>\n");
  CHECK(html("~~~\na") == "<pre><code>a\n</code></pre>\n");
  CHECK(html("~~~ &amp;x\\!y z\n~~~\n") == "<pre><code class=\"language-&amp;x!y\"></code></pre>\n");
}

TEST_CASE("md blocks: indented code, HTML blocks, headings, breaks") {
  const Parsed p = parse(">\t\tfoo\n");
  CHECK(p.tree ==
        "(document@0..7 (block_quote@0..7 (block_quote_marker@0..1) (code_block@3..7 (code_content@3..7 vs=2))))");
  CHECK(p.html == "<blockquote>\n<pre><code>  foo\n</code></pre>\n</blockquote>\n");
  CHECK(html("    a\n      \n    b\n  \n") == "<pre><code>a\n  \nb\n</code></pre>\n");
  CHECK(html("  <div>\n  *x*\n\nb") == "  <div>\n  *x*\n<p>b</p>\n");
  CHECK(html("# a #\n## \nb\n===\nc\n---\n***\n") ==
        "<h1>a</h1>\n<h2></h2>\n<h1>b</h1>\n<h2>c</h2>\n<hr />\n");
  CHECK(parse("Foo\n  ===  \n").tree ==
        "(document@0..12 (heading@0..12 level=1 setext (text@0..3) (block_continuation@4..6) "
        "(heading_marker@6..9)))");
}

TEST_CASE("md blocks: CRLF, BOM and NUL") {
  CHECK(html("a\r\nb\r\n\r\n```\r\nx\r\n```\r\n") == "<p>a\nb</p>\n<pre><code>x\n</code></pre>\n");
  CHECK(html("a\rb") == "<p>a\nb</p>\n");
  const Parsed b = parse("\xEF\xBB\xBF# a\n");
  CHECK(b.tree == "(document@0..7 (heading@3..7 level=1 (heading_marker@3..4) (text@5..6)))");
  CHECK(html(std::string("a\0b\n", 4)) == "<p>a\xEF\xBF\xBD" "b</p>\n");
  CHECK(html("") == "");
  CHECK(html("\n\n") == "");
}

TEST_CASE("md blocks: chunked text gives the same tree") {
  const std::string spec = read_file(std::string(LUMOS_SPEC_DIR) + "/commonmark-0.31.2.txt");
  const Parsed whole = parse(spec);
  for (std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{4096}}) {
    const ChunkedSource src(spec, n);
    const Parsed chunked = parse(spec, &src);
    CHECK(chunked.tree == whole.tree);
    CHECK(chunked.html == whole.html);
  }
}

TEST_CASE("md blocks: deep nesting without recursion") {
  const std::size_t depth = 10000;
  std::string quotes;
  for (std::size_t i = 0; i < depth; ++i) quotes += "> ";
  quotes += "a\n";
  const Parsed q = parse(quotes);
  std::size_t opened = 0;
  for (std::size_t at = q.html.find("<blockquote>"); at != std::string::npos; at = q.html.find("<blockquote>", at + 1))
    ++opened;
  CHECK(opened == depth);

  std::string lists;
  for (std::size_t i = 0; i < depth; ++i) lists += "- ";
  lists += "a\n\n\n";
  for (std::size_t i = 0; i < depth; ++i) lists += "  ";
  lists += "b\n";
  const Parsed l = parse(lists);
  CHECK(l.html.find("<p>b</p>") != std::string::npos);
  std::size_t items = 0;
  for (std::size_t at = l.html.find("<li>"); at != std::string::npos; at = l.html.find("<li>", at + 1)) ++items;
  CHECK(items == depth);
}

TEST_CASE("md parser: make_markdown_parser, apply_edit and the line table") {
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  p->parse("> a\n> b\n");
  CHECK(p->changes().size() == 1);
  CHECK(p->changes()[0].new_end == 8);
  p->apply_edit(lumos::Edit{4, 0, "x\n"}, "> a\nx\n> b\n");
  auto q = md::make_markdown_parser(lumos::Dialect::commonmark());
  q->parse("> a\nx\n> b\n");
  CHECK(p->tree().to_debug_string() == q->tree().to_debug_string());
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(p.get());
  REQUIRE(mp != nullptr);
  REQUIRE(mp->lines().size() == 3);
  const md::StateId s = mp->lines().state(1);
  CHECK(mp->arena().get(s).kind == md::OpenKind::paragraph);
  CHECK(mp->arena().get(mp->arena().get(s).parent).kind == md::OpenKind::block_quote);
  CHECK(mp->lines().state(0) == md::document_state);
}

TEST_CASE("md parser: debug log of decisions") {
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view l) { lines.emplace_back(l); });
  lumos::set_log_level(lumos::LogLevel::debug);
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  p->parse("> a\nb\n\n- x\n\n- y\n");
  lumos::set_log_level(lumos::LogLevel::info);
  lumos::set_log_sink(nullptr);
  auto has = [&](std::string_view what) {
    for (const std::string& l : lines)
      if (l.find(what) != std::string::npos && l.find("comp=parser") != std::string::npos) return true;
    return false;
  };
  CHECK(has("event=parse_start"));
  CHECK(has("event=lazy_line line=2"));
  CHECK(has("event=list_loose"));
  CHECK(has("event=parse_end"));
}

TEST_CASE("md blocks: link reference definitions, tree shape") {
  CHECK(html("- # h\n  [a]: /u\n\n  c\n") == "<ul>\n<li>\n<h1>h</h1>\nc</li>\n</ul>\n");
  CHECK(html("- # h\n  [a]: /u\n\n- c\n") == "<ul>\n<li>\n<h1>h</h1>\n</li>\n<li>\n<p>c</p>\n</li>\n</ul>\n");
  CHECK(html("- [a]: /u\n\n- c\n") == "<ul>\n<li></li>\n<li>\n<p>c</p>\n</li>\n</ul>\n");
  CHECK(html("- [a]: /u\n  ***\n- c") == "<ul>\n<li>\n<hr />\n</li>\n<li>c</li>\n</ul>\n");
  CHECK(html("1.\tfoo\n\n\t[d]:\n\tu\n> x\n") ==
        "<ol>\n<li>\n<p>foo</p>\n</li>\n</ol>\n<blockquote>\n<p>x</p>\n</blockquote>\n");
  CHECK(html("> [a]: /u\n> b\n> ===\n") == "<blockquote>\n<h1>b</h1>\n</blockquote>\n");
  CHECK(html("[a]: /u\n===") == "<p>===</p>\n");
  CHECK(html("[a]: /u\n---") == "<hr />\n");
  CHECK(html("[ ]: /u\nx") == "<p>[ ]: /u\nx</p>\n");
  CHECK(html("[a]:\n/u\n\"t\" x") == "<p>&quot;t&quot; x</p>\n");
  CHECK(html("- a\n- [b]: /u\n\n  c\n- d") == "<ul>\n<li>a</li>\n<li>c</li>\n<li>d</li>\n</ul>\n");
  CHECK(html("[a]: /u") == "");

  CHECK(html("[a]: /u\r\nx\r\n") == "<p>x</p>\n");
  CHECK(html("[a]: /u\rx\r") == "<p>x</p>\n");
  CHECK(html("[a]: <my url>\nx\n") == "<p>x</p>\n");
  CHECK(html("[a]: /u\n\"title\"\nx\n") == "<p>x</p>\n");
  CHECK(html("> [a]:\n/u\nx\n") == "<blockquote>\n<p>x</p>\n</blockquote>\n");
  CHECK(html("> [a]:\n> /u\n> \"t\"\n> x\n") == "<blockquote>\n<p>x</p>\n</blockquote>\n");

  const Parsed p = parse("[a]: /u \"t\"\n");
  CHECK(p.html.empty());
  bool saw_definition = false;
  for (NodeKind k : {NodeKind::link_reference_definition, NodeKind::link_label, NodeKind::link_destination,
                     NodeKind::link_title})
    if (p.tree.find(lumos::kind_name(k)) != std::string::npos) saw_definition = true;
  CHECK(saw_definition);
}

TEST_CASE("md blocks: link reference definitions, debug log") {
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view l) { lines.emplace_back(l); });
  lumos::set_log_level(lumos::LogLevel::debug);
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  p->parse("[a]: /u\n---\n");
  lumos::set_log_level(lumos::LogLevel::info);
  lumos::set_log_sink(nullptr);
  auto has = [&](std::string_view what) {
    for (const std::string& l : lines)
      if (l.find(what) != std::string::npos && l.find("comp=parser") != std::string::npos) return true;
    return false;
  };
  CHECK(has("event=definitions line=2 count=1 rest=0"));
  CHECK(has("event=setext_rejected line=2 reason=only_definitions"));
}

TEST_CASE("md parser: link reference definitions fill the RefIndex") {
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  p->parse("[Foo]: /u\n\n[bar]: /v \"t\"\n");
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(p.get());
  REQUIRE(mp != nullptr);
  const md::RefIndex& refs = mp->refs();
  md::LabelId id = 0;
  REQUIRE(refs.find("foo", id));
  CHECK(refs.is_defined(id));
  REQUIRE(refs.find("bar", id));
  CHECK(refs.is_defined(id));
  CHECK_FALSE(refs.find("baz", id));
}

namespace {

std::string html_gfm(std::string_view text) {
  auto p = md::make_markdown_parser(lumos::Dialect{});
  const lumos::StringSource s(text);
  p->parse(s);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  md::HtmlOptions opt;
  opt.unsafe = true;
  return md::render_html(t, s, opt);
}

std::string html_gfm_6b(std::string_view text) {
  lumos::Dialect d{};
  d.front_matter = false;
  auto p = md::make_markdown_parser(d);
  const lumos::StringSource s(text);
  p->parse(s);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  md::HtmlOptions opt;
  opt.unsafe = true;
  opt.tagfilter = true;
  return md::render_html(t, s, opt);
}

std::string escape_oneline(std::string_view s) {
  std::string out = "\"";
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\x%02x", c);
      out += buf;
    } else {
      out += ch;
    }
  }
  out += "\"";
  return out;
}

Parsed parse_gfm_6c(std::string_view text) {
  lumos::Dialect d{};
  d.front_matter = false;
  auto p = md::make_markdown_parser(d);
  const lumos::StringSource s(text);
  p->parse(s);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  md::HtmlOptions opt;
  opt.unsafe = true;
  return {t.to_debug_string(), md::render_html(t, s, opt)};
}

std::string html_gfm_6c(std::string_view text) {
  lumos::Dialect d{};
  d.front_matter = false;
  auto p = md::make_markdown_parser(d);
  const lumos::StringSource s(text);
  p->parse(s);
  const lumos::SyntaxTree& t = p->tree();
  INFO(t.to_debug_string());
  CHECK_FALSE(t.validate(text.size()).has_value());
  CHECK_FALSE(t.validate_coverage(text).has_value());
  md::HtmlOptions opt;
  opt.unsafe = true;
  return md::render_html(t, s, opt);
}

}  

TEST_CASE("md blocks: GFM tables") {
  static const std::pair<std::string_view, std::string_view> cases[] = {
#include "gfm_table_cases.inc"
  };
  for (const auto& [input, expected] : cases) {
    INFO(input);
    CHECK(html_gfm(input) == expected);
  }
}

TEST_CASE("md blocks: GFM task lists and tag filter") {
  static const std::pair<std::string_view, std::string_view> cases[] = {
#include "gfm_6b_cases.inc"
  };
  for (const auto& [input, expected] : cases) {
    INFO(input);
    CHECK(html_gfm_6b(input) == expected);
  }
}

TEST_CASE("md blocks: GFM extended autolinks") {
  static const std::pair<std::string_view, std::string_view> cases[] = {
#include "gfm_6c_cases.inc"
  };
  for (const auto& [input, expected] : cases) {
    const std::string got = html_gfm_6c(input);
    if (got != expected) FAIL_CHECK(escape_oneline(input) + " got " + escape_oneline(got));
  }
}

TEST_CASE("md inlines: GFM extended autolinks, tree shape") {
  CHECK(parse_gfm_6c("x www.a.b y\n").tree ==
        "(document@0..12 (paragraph@0..12 (text@0..2) (autolink@2..9 variant=2 (text@2..9)) (text@9..11)))");
  CHECK(parse_gfm_6c("ahttp://b.c\n").tree == "(document@0..12 (paragraph@0..12 (text@0..11)))");
  CHECK(parse_gfm_6c("a\\_b@c.d\n").tree ==
        "(document@0..9 (paragraph@0..9 (autolink@0..8 variant=4 (text@0..1) (escape@1..3) (text@3..8))))");
  CHECK(parse_gfm_6c("*a@b.c*\n").tree ==
        "(document@0..8 (paragraph@0..8 (emphasis@0..7 (emphasis_delimiter@0..1) (autolink@1..6 variant=4 "
        "(text@1..6)) (emphasis_delimiter@6..7))))");
}
