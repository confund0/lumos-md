
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <random>
#include <string>

#include "lumos/document.hpp"
#include "lumos/tools.hpp"

using lumos::DiagnosticCode;
using lumos::Document;
using lumos::Edit;
using lumos::Properties;
using lumos::PropertiesOptions;
using lumos::Severity;
using lumos::UrlForm;
using lumos::properties;
using lumos::url_form;

namespace {

std::string repeat(std::string_view s, std::size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (std::size_t i = 0; i < n; ++i) out += s;
  return out;
}

}  

TEST_CASE("tools: properties of an empty document", "[tools]") {
  const Document doc;
  const Properties p = properties(doc);
  CHECK(p.bytes == 0);
  CHECK(p.characters == 0);
  CHECK(p.words == 0);
  CHECK(p.lines == 1);
}

TEST_CASE("tools: word count joins emphasis and backslash escapes", "[tools]") {
  const Document doc(std::string("wo**r**d a\\_b &amp; x"));
  CHECK(properties(doc).words == 4);
}

TEST_CASE("tools: word count over a GFM table", "[tools]") {
  const Document doc(std::string("a|b\n-|-\nc|d\n"));
  const Properties p = properties(doc);
  CHECK(p.words == 4);
  CHECK(p.tables == 1);
}

TEST_CASE("tools: word count of CJK text", "[tools]") {
  const Document doc(std::string("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e text"));
  CHECK(properties(doc).words == 4);
}

TEST_CASE("tools: character count of a multi-byte code point", "[tools]") {
  const Document doc(std::string("\xc3\xa9"));  
  const Properties p = properties(doc);
  CHECK(p.characters == 1);
  CHECK(p.bytes == 2);
}

TEST_CASE("tools: character count of an invalid byte", "[tools]") {
  const Document doc(std::string("\xff"));
  CHECK(properties(doc).characters == 1);
}

TEST_CASE("tools: link, image and autolink targets in order", "[tools]") {
  const std::string text = "[a](http://x \"t\") ![i](p.png) <http://y>\n";
  const Document doc(text);
  const Properties p = properties(doc);
  CHECK(p.links == 2);
  CHECK(p.images == 1);
  REQUIRE(p.targets.size() == 3);

  const std::size_t link_start = text.find("[a]");
  const std::size_t link_end = text.find(')') + 1;
  CHECK(p.targets[0].kind == lumos::md::UrlKind::link);
  CHECK(p.targets[0].url == "http://x");
  CHECK(p.targets[0].form == UrlForm::scheme_url);
  CHECK(p.targets[0].start == link_start);
  CHECK(p.targets[0].end == link_end);

  const std::size_t image_start = text.find('!');
  const std::size_t image_end = text.find("p.png)") + std::string("p.png)").size();
  CHECK(p.targets[1].kind == lumos::md::UrlKind::image);
  CHECK(p.targets[1].url == "p.png");
  CHECK(p.targets[1].form == UrlForm::relative_path);
  CHECK(p.targets[1].start == image_start);
  CHECK(p.targets[1].end == image_end);

  const std::size_t auto_start = text.find('<');
  const std::size_t auto_end = text.find('>') + 1;
  CHECK(p.targets[2].kind == lumos::md::UrlKind::link);
  CHECK(p.targets[2].url == "http://y");
  CHECK(p.targets[2].form == UrlForm::scheme_url);
  CHECK(p.targets[2].start == auto_start);
  CHECK(p.targets[2].end == auto_end);
}

TEST_CASE("tools: reference link expansion and unused definitions", "[tools]") {
  const Document doc(std::string("[a][r] [r]\n\n[r]: /abs \"title\"\n[s]: /other\n"));
  const Properties p = properties(doc);
  REQUIRE(p.targets.size() == 2);
  for (const auto& target : p.targets) {
    CHECK(target.url == "/abs");
    CHECK(target.form == UrlForm::absolute_path);
  }
  CHECK(p.link_expansion == 2 * (4 + 7));
}

TEST_CASE("tools: url_form classifies forms", "[tools]") {
  CHECK(url_form("") == UrlForm::other);
  CHECK(url_form("#x") == UrlForm::other);
  CHECK(url_form("?q") == UrlForm::other);
  CHECK(url_form("/a") == UrlForm::absolute_path);
  CHECK(url_form("\\a") == UrlForm::absolute_path);
  CHECK(url_form("C:\\a") == UrlForm::absolute_path);
  CHECK(url_form("c:/a") == UrlForm::absolute_path);
  CHECK(url_form("C:") == UrlForm::absolute_path);
  CHECK(url_form("a:b") == UrlForm::absolute_path);  
  CHECK(url_form("//h/x") == UrlForm::scheme_url);
  CHECK(url_form("FILE:///a") == UrlForm::file_url);
  CHECK(url_form("mailto:a@b") == UrlForm::scheme_url);
  CHECK(url_form("http://x") == UrlForm::scheme_url);
  CHECK(url_form("a/b.png") == UrlForm::relative_path);
  CHECK(url_form("./a") == UrlForm::relative_path);
  CHECK(url_form("x-y") == UrlForm::relative_path);
}

TEST_CASE("tools: link expansion warning over the limit", "[tools]") {
  PropertiesOptions options;
  options.link_expansion_limit = 10;

  const Document over(std::string("[a][r]\n\n[r]: /abcdefghij\n"));  
  const Properties p_over = properties(over, options);
  CHECK(p_over.link_expansion == 11);
  REQUIRE(p_over.diagnostics.size() == 1);
  CHECK(p_over.diagnostics[0].severity == Severity::warning);
  CHECK(p_over.diagnostics[0].code == DiagnosticCode::link_expansion_large);
  CHECK(p_over.diagnostics[0].value == 11);

  const Document under(std::string("[a][r]\n\n[r]: /a\n"));  
  const Properties p_under = properties(under, options);
  CHECK(p_under.link_expansion == 2);
  CHECK(p_under.diagnostics.empty());
}

TEST_CASE("tools: task, heading, paragraph and code block counts", "[tools]") {
  const Document doc(std::string("# h\n\n- [x] a\n- [ ] b\n- [ ] c\n\n```\ncode\n```\n"));
  const Properties p = properties(doc);
  CHECK(p.tasks_done == 1);
  CHECK(p.tasks_open == 2);
  CHECK(p.headings == 1);
  CHECK(p.paragraphs == 3);  
  CHECK(p.code_blocks == 1);
}

TEST_CASE("tools: properties over 100,000 nested quotes", "[pathological]") {
  const Document doc(repeat("> ", 100000) + "a");
  const auto t0 = std::chrono::steady_clock::now();
  const Properties p = properties(doc);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK(p.words == 1);
  CHECK(seconds < 10.0);
}

namespace {

std::string tools_sample() {
  std::string out;
  out += "# Heading *em* \xe6\x97\xa5\n\n";
  out += "- [x] done [a][r]\n- [ ] open ![i](p.png)\n\n";
  out += "a|b\n-|-\n1|2\n\n";
  out += "[a][r] and [r] and [b](http://x \"t\")\n\n";
  out += "[r]: /abs \"title\"\n";
  return out;
}

std::size_t snap_down(const std::string& text, std::size_t offset) {
  while (offset > 0 && offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) --offset;
  return offset;
}

std::size_t snap_up(const std::string& text, std::size_t offset) {
  while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) ++offset;
  return offset;
}

Edit random_edit(std::mt19937& rng, const std::string& text) {
  static constexpr std::string_view pieces[] = {"a", " b", "**c**", "\n- [ ] d\n", "[e][r]", "\xe6\x97\xa5", "|",
                                                 "1", "\n\n"};
  std::uniform_int_distribution<std::size_t> offset_dist(0, text.size());
  const std::size_t offset = snap_down(text, offset_dist(rng));
  std::uniform_int_distribution<int> kind_dist(0, 1);
  if (kind_dist(rng) == 0 || text.empty()) {
    std::uniform_int_distribution<std::size_t> piece_dist(0, std::size(pieces) - 1);
    return Edit{offset, 0, std::string(pieces[piece_dist(rng)])};
  }
  std::uniform_int_distribution<std::size_t> remove_dist(0, std::min<std::size_t>(5, text.size() - offset));
  const std::size_t end = snap_up(text, offset + remove_dist(rng));
  return Edit{offset, end - offset, std::string()};
}

void check_same(const Properties& a, const Properties& b) {
  CHECK(a.bytes == b.bytes);
  CHECK(a.characters == b.characters);
  CHECK(a.words == b.words);
  CHECK(a.lines == b.lines);
  CHECK(a.headings == b.headings);
  CHECK(a.paragraphs == b.paragraphs);
  CHECK(a.links == b.links);
  CHECK(a.images == b.images);
  CHECK(a.code_blocks == b.code_blocks);
  CHECK(a.tables == b.tables);
  CHECK(a.tasks_done == b.tasks_done);
  CHECK(a.tasks_open == b.tasks_open);
  CHECK(a.link_expansion == b.link_expansion);
  REQUIRE(a.targets.size() == b.targets.size());
  for (std::size_t i = 0; i < a.targets.size(); ++i) {
    CHECK(a.targets[i].kind == b.targets[i].kind);
    CHECK(a.targets[i].form == b.targets[i].form);
    CHECK(a.targets[i].url == b.targets[i].url);
    CHECK(a.targets[i].start == b.targets[i].start);
    CHECK(a.targets[i].end == b.targets[i].end);
  }
}

}  

TEST_CASE("tools: property check over random edits", "[tools]") {
  std::string text = tools_sample();
  Document doc(text);
  std::mt19937 rng(7);
  for (int step = 0; step < 200; ++step) {
    const Edit e = random_edit(rng, text);
    if (e.removed == 0 && e.inserted.empty()) continue;
    text.replace(e.offset, e.removed, e.inserted);
    doc.apply(e);
    INFO("step " << step);
    check_same(properties(doc), properties(Document(text)));
  }
}
