
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <string_view>

#include "lumos/md/html_renderer.hpp"
#include "lumos/parser.hpp"

namespace md = lumos::md;

namespace {

constexpr double limit_s = 10.0;

std::string repeat(std::string_view s, std::size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (std::size_t i = 0; i < n; ++i) out += s;
  return out;
}

std::size_t count(std::string_view s, std::string_view what) {
  std::size_t n = 0;
  for (std::size_t at = s.find(what); at != std::string_view::npos; at = s.find(what, at + what.size())) ++n;
  return n;
}

struct Run {
  std::string html;
  double seconds = 0;
};

Run run(const std::string& text) {
  const auto t0 = std::chrono::steady_clock::now();
  auto p = md::make_markdown_parser(lumos::Dialect::commonmark());
  const lumos::StringSource src(text);
  p->parse(src);
  md::HtmlOptions opt;
  opt.unsafe = true;
  Run r;
  r.html = md::render_html(p->tree(), src, opt);
  r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK_FALSE(p->tree().validate(text.size()).has_value());
  CHECK_FALSE(p->tree().validate_coverage(text).has_value());
  return r;
}

std::size_t skip_open_tags(std::string_view s, bool newline_required) {
  std::size_t p = 0;
  while (p < s.size() && s[p] == '<' && p + 1 < s.size() && s[p + 1] != '/') {
    const std::size_t close = s.find('>', p);
    if (close == std::string_view::npos) break;
    const bool newline = close + 1 < s.size() && s[close + 1] == '\n';
    if (newline_required && !newline) break;
    p = close + 1 + (newline ? 1 : 0);
  }
  return p;
}

bool only_close_tags(std::string_view s, std::size_t p) {
  if (p >= s.size()) return false;
  while (p < s.size()) {
    if (s.compare(p, 2, "</") != 0) return false;
    const std::size_t close = s.find(">\n", p);
    if (close == std::string_view::npos) return false;
    p = close + 2;
  }
  return true;
}

}  

TEST_CASE("md pathological: nested block quotes", "[pathological]") {
  const Run r = run(repeat("> ", 25000) + "a");
  CHECK(count(r.html, "<blockquote>\n") == 25000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: deeply nested lists", "[pathological]") {
  std::string text;
  for (std::size_t x = 0; x < 500; ++x) text += repeat("  ", x) + "* a\n";
  const Run r = run(text);
  const std::string expected =
      "<ul>\n" + repeat("<li>a\n<ul>\n", 499) + "<li>a</li>\n</ul>\n" + repeat("</li>\n</ul>\n", 499);
  CHECK(r.html == expected);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: U+0000 in input", "[pathological]") {
  const Run r = run(std::string("abc\0de\0", 7));
  CHECK(r.html == "<p>abc\xEF\xBF\xBD" "de\xEF\xBF\xBD</p>\n");
}

TEST_CASE("md pathological: empty lines in deeply nested lists", "[pathological]") {
  const Run r = run(repeat("- ", 30000) + "x" + repeat("\n", 30000));
  const std::size_t p = skip_open_tags(r.html, false);
  REQUIRE(p < r.html.size());
  CHECK(r.html[p] == 'x');
  CHECK(only_close_tags(r.html, p + 1));
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: empty lines in deeply nested lists in a block quote", "[pathological]") {
  const Run r = run("> " + repeat("- ", 30000) + "x\n" + repeat(">\n", 30000));
  const std::size_t p = skip_open_tags(r.html, false);
  REQUIRE(p < r.html.size());
  CHECK(r.html[p] == 'x');
  CHECK(only_close_tags(r.html, p + 1));
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: emphasis in a deep block quote (block part)", "[pathological]") {
  const Run r = run(repeat(">", 100000) + repeat("a*", 100000));
  const std::size_t p = skip_open_tags(r.html, true);
  REQUIRE(r.html.compare(p, 3, "<p>") == 0);
  const std::size_t end = r.html.find("</p>\n", p);
  REQUIRE(end != std::string::npos);
  CHECK(only_close_tags(r.html, end + 5));
  CHECK(count(r.html, "<blockquote>\n") == 100000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: block cases without a cmark counterpart", "[pathological]") {
  Run r = run(repeat("* ", 30000) + "x\n");
  CHECK(count(r.html, "<li>") == 30000);
  CHECK(r.seconds < limit_s);
  r = run(repeat("- ", 2000) + "x\n" + repeat(repeat(" ", 4000) + "\n", 2000) + "y\n");
  CHECK(r.html.find("<p>y</p>") != std::string::npos);
  CHECK(r.seconds < limit_s);
  r = run(repeat("> ", 2000) + "a\n" + repeat("b\n", 50000));
  CHECK(count(r.html, "<blockquote>\n") == 2000);
  CHECK(r.seconds < limit_s);
  r = run(repeat("> Intro with quoted text and *em*\n", 50000));
  CHECK(count(r.html, "\n") == 50000 + 2);
  CHECK(r.seconds < limit_s);
  r = run(repeat("- item\n", 50000));
  CHECK(count(r.html, "<li>") == 50000);
  CHECK(r.seconds < limit_s);
  r = run("```\n" + repeat("a\n", 100000));
  CHECK(count(r.html, "\n") == 100001);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: nested strong emph", "[pathological]") {
  const Run r = run(repeat("*a **a ", 32500) + "b" + repeat(" a** a*", 32500));
  CHECK(count(r.html, "<em>a <strong>a ") == 32500);
  CHECK(count(r.html, " a</strong> a</em>") == 32500);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: many emph closers with no openers", "[pathological]") {
  const Run r = run(repeat("a_ ", 32500));
  CHECK(count(r.html, "a_ ") == 32499);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: many emph openers with no closers", "[pathological]") {
  const Run r = run(repeat("_a ", 32500));
  CHECK(count(r.html, "_a ") == 32499);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: mismatched openers and closers", "[pathological]") {
  const Run r = run(repeat("*a_ ", 25000));
  CHECK(count(r.html, "*a_ ") == 24999);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: issue #389", "[pathological]") {
  const Run r = run(repeat("*a ", 20000) + repeat("_a*_ ", 20000));
  CHECK(count(r.html, "<em>a ") == 20000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: openers and closers multiple of 3", "[pathological]") {
  const Run r = run("a**b" + repeat("c* ", 25000));
  CHECK(r.html.compare(0, 7, "<p>a**b") == 0);
  CHECK(count(r.html, "c* ") == 24999);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: backticks", "[pathological]") {
  std::string text;
  for (std::size_t x = 1; x < 2500; ++x) text += "e" + repeat("`", x);
  const Run r = run(text);
  CHECK(r.html.compare(0, 3, "<p>") == 0);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: unclosed <!--", "[pathological]") {
  const Run r = run("</" + repeat("<!--", 300000));
  CHECK(count(r.html, "&lt;!--") == 300000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: emphasis in a deep block quote (inline part)", "[pathological]") {
  const Run r = run(repeat(">", 100000) + repeat("a*", 100000));
  const std::size_t p = skip_open_tags(r.html, true);
  REQUIRE(r.html.compare(p, 3, "<p>") == 0);
  const std::size_t end = r.html.find("</p>\n", p);
  REQUIRE(end != std::string::npos);
  CHECK(end > p + 3);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: nested inlines (cmark renderer case)", "[pathological]") {
  const Run r = run(repeat("*", 20000) + "a" + repeat("*", 20000));
  CHECK(r.html.compare(0, 3, "<p>") == 0);
  CHECK(r.html.find('a') != std::string::npos);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: many link closers with no openers", "[pathological]") {
  const Run r = run(repeat("a]", 32500));
  CHECK(count(r.html, "a]") == 32500);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: many link openers with no closers", "[pathological]") {
  const Run r = run(repeat("[a", 32500));
  CHECK(count(r.html, "[a") == 32500);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: link openers and emph closers", "[pathological]") {
  const Run r = run(repeat("[ a_", 25000));
  CHECK(count(r.html, "[ a_") == 25000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: pattern [ (]( repeated", "[pathological]") {
  const Run r = run(repeat("[ (](", 40000));
  CHECK(count(r.html, "[ (](") == 40000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: pattern ![[]() repeated", "[pathological]") {
  const Run r = run(repeat("![[]()", 160000));
  CHECK(count(r.html, "![<a href=\"\"></a>") == 160000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: hard link/emph case", "[pathological]") {
  const Run r = run("**x [a*b**c*](d)");
  CHECK(r.html == "<p>**x <a href=\"d\">a<em>b**c</em></a></p>\n");
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: nested brackets", "[pathological]") {
  const Run r = run(repeat("[", 25000) + "a" + repeat("]", 25000));
  CHECK(count(r.html, "[") == 25000);
  CHECK(count(r.html, "]") == 25000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: unclosed links A", "[pathological]") {
  const Run r = run(repeat("[a](<b", 30000));
  CHECK(count(r.html, "[a](&lt;b") == 30000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: unclosed links B", "[pathological]") {
  const Run r = run(repeat("[a](b", 30000));
  CHECK(count(r.html, "[a](b") == 30000);
  CHECK(r.seconds < limit_s);
}

TEST_CASE("md pathological: reference collisions", "[pathological]") {
  std::string text;
  constexpr int count_defs = 25000;
  for (int i = 0; i < count_defs; ++i) {
    text += "[x" + std::to_string(i) + "]: /url\n\n[x" + std::to_string(i) + "]\n\n";
  }
  const Run r = run(text);
  CHECK(count(r.html, "<a href=\"/url\">x") == count_defs);
  CHECK(r.seconds < limit_s);
}
