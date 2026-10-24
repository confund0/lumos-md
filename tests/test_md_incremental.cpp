
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"
#include "md/md_parser.hpp"
#include "md_test_util.hpp"

namespace md = lumos::md;
using lumos::Edit;
using lumos::SyntaxTree;
using lumos::TextRegion;

namespace {

void check_tree(const SyntaxTree& t, std::string_view src, std::size_t near_offset = 0) {
  if (const auto err = t.validate(src.size())) {
    INFO(*err);
    INFO(lumos_test::source_window(src, near_offset));
    FAIL_CHECK("validate failed");
  }
  if (const auto err = t.validate_coverage(src)) {
    INFO(*err);
    INFO(lumos_test::source_window(src, near_offset));
    FAIL_CHECK("validate_coverage failed");
  }
}

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

std::vector<std::string> defined_label_texts(const md::RefIndex& idx) {
  std::vector<std::string> out;
  for (const auto& d : idx.definitions()) out.emplace_back(idx.label(d.label));
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<std::string> expected_flips(const md::RefIndex& before, const md::RefIndex& after) {
  const auto b = defined_label_texts(before);
  const auto a = defined_label_texts(after);
  std::vector<std::string> out;
  std::set_symmetric_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(out));
  return out;
}

bool has_link_ancestor(const SyntaxTree& t, std::size_t offset) {
  for (lumos::NodeId id = t.node_at(offset); id != lumos::no_node; id = t.node(id).parent)
    if (t.node(id).kind == lumos::NodeKind::link) return true;
  return false;
}

Edit piece_edit(std::mt19937& rng, const std::string& text, std::size_t max_size,
                const std::vector<std::string>& pieces) {
  auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
  std::size_t start = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
  if (pick(3) == 0) {
    while (start > 0 && text[start - 1] != '\n') --start;
  }
  while (start < text.size() && is_continuation(text[start])) ++start;
  std::size_t end = start;
  if (pick(3) == 0 || text.size() > max_size) {
    end = std::min(text.size(), start + pick(text.size() > max_size ? 60 : 20));
    while (end < text.size() && is_continuation(text[end])) ++end;
  }
  std::string inserted;
  const std::size_t count = pick(4);
  for (std::size_t i = 0; i < count; ++i) inserted += pieces[pick(pieces.size())];
  return Edit{start, end - start, inserted};
}

Edit structure_edit(std::mt19937& rng, const std::string& text, std::size_t max_size) {
  static const std::vector<std::string> pieces = {
      "a", " ", "\n", "\n\n", "\n\n\n", "- ", "  - ", "* ", "+ ", "1. ", "2) ", "> ", ">> ", "> > ",
      "```\n", "```js\n", "~~~\n", "~~~~\n", "    ", "\t", "\t\t", "<div>\n", "</div>\n", "<!--\n", "-->\n",
      "===\n", "---\n", "***\n", "___\n", "# ", "## ", "###### ", "[x]: /u\n", "\r\n", "x\r", "  \n",
      "[y]: /v\n", "[x]:", " /u", " \"t\"\n", "[", "]", "[X]: /w\n",
      "|", " | ", "|-|\n", "-|-\n", ":-:|", "\\|", "`|`", "| a | b |\n",
      "[ ] ", "[x] ",
      "www.a.b ", "a@b.c ",
  };
  return piece_edit(rng, text, max_size, pieces);
}

Edit link_edit(std::mt19937& rng, const std::string& text, std::size_t max_size) {
  static const std::vector<std::string> pieces = {
      "[a]: /u\n", "[b]: /v\n", "[a*]: /w\n", "[", "]", "[a]", "[b][a]", "*", "\n\n", "- ", "> ",
      "a", "b", "c", " ",
  };
  return piece_edit(rng, text, max_size, pieces);
}

std::string link_heavy_doc() {
  std::string out;
  out += "[a]: /u\n\n";
  out += "[a] and [a][] and [b][a] and *[a*]* and ![a] and [a](/u)\n\n";
  out += "# Heading with [a] and [b][a]\n\n";
  out += "- [a] in an item\n- ![a] and [b][a] in another\n  - [a*]: /w\n- [a*] after a nested definition\n\n";
  out += "> [a] in a quote\n> [b][a]\n\n";
  out += "[b]: /v\n";
  return out;
}

void random_md_edits(std::string text, unsigned seed, int steps,
                      Edit (*pick_edit)(std::mt19937&, const std::string&, std::size_t) = structure_edit) {
  auto inc = md::make_markdown_parser();
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
  REQUIRE(mp != nullptr);
  inc->parse(text);
  check_tree(inc->tree(), text);
  lumos_test::dump_start(text);
  auto fresh = md::make_markdown_parser();
  const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
  REQUIRE(fresh_mp != nullptr);
  std::mt19937 rng(seed);
  const std::size_t max_size = text.size() * 5 / 4 + 200;
  for (int step = 0; step < steps; ++step) {
    Edit e;
    while (e.removed == 0 && e.inserted.empty()) e = pick_edit(rng, text, max_size);
    lumos_test::dump_edit(e);
    const SyntaxTree old_tree = inc->tree();
    text.replace(e.offset, e.removed, e.inserted);
    if (std::getenv("LUMOS_COLLECT_FAILS") != nullptr) {
      const std::size_t fallbacks = mp->fallback_count();
      std::string sig;
      try {
        inc->apply_edit(e, text);
      } catch (const std::exception& ex) {
        sig = std::string("throw: ") + ex.what();
      }
      if (sig.empty() && mp->fallback_count() != fallbacks) sig = "fallback: " + mp->last_fallback_reason();
      if (sig.empty()) {
        fresh->parse(text);
        sig = lumos_test::tree_signature(inc->tree(), fresh->tree());
      }
      if (!sig.empty()) {
        std::fprintf(stderr, "SIG %s\n", sig.c_str());
        inc->parse(text);
      }
      continue;
    }
    const md::RefIndex old_refs = mp->refs();
    const std::size_t fallbacks_before = mp->fallback_count();
    inc->apply_edit(e, text);
    INFO("seed " << seed << " step " << step << " edit " << e.offset << " -" << e.removed << " +\"" << e.inserted
                 << "\"");
    INFO(lumos_test::source_window(text, e.offset));
    INFO("fallback_count=" << mp->fallback_count() << " reason: " << mp->last_fallback_reason());
    REQUIRE(mp->fallback_count() == fallbacks_before);
    fresh->parse(text);
    const std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
    INFO(diff);
    REQUIRE(diff.empty());
    const std::string refs_diff = lumos_test::diff_refs(mp->refs(), fresh_mp->refs());
    INFO(refs_diff);
    REQUIRE(refs_diff.empty());
    std::vector<std::string> flipped = mp->last_flipped();
    std::sort(flipped.begin(), flipped.end());
    const std::vector<std::string> expected = expected_flips(old_refs, mp->refs());
    INFO("flipped: got " << flipped.size() << " expected " << expected.size());
    REQUIRE(flipped == expected);
    check_tree(inc->tree(), text, e.offset);
    const auto regions = inc->changes();
    REQUIRE(!regions.empty());
    if (regions.size() == 1) {
      const TextRegion r = regions.front();
      REQUIRE(r.start <= e.offset);
      REQUIRE(r.new_end >= e.offset + e.inserted.size());
    }
    const std::string locality = lumos_test::check_locality(old_tree, inc->tree(), regions);
    INFO(locality);
    REQUIRE(locality.empty());
  }
}

std::string read_sample() {
  const auto path = std::filesystem::path(LUMOS_TEST_DATA_DIR) / "sample.md";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  const auto size = std::filesystem::file_size(path);
  std::string out(static_cast<std::size_t>(size), '\0');
  in.read(out.data(), static_cast<std::streamsize>(size));
  return out;
}

std::string block_heavy_doc() {
  std::string out;
  out += "# Title\n\nIntro para\nwith a lazy continuation\n\n";
  out += "- a\n  - b\n    - c\n      text\n- d\n\n  para in d\n\n";
  out += "1. one\n2. two\n\n   nested para\n\n3. three\n";
  out += "> quote\n> more\n>> deep\n>\n> after blank\n";
  out += "> - item in quote\n> - item2\n>\n>   loose child\n";
  out += "```cpp title\ncode line\n\nmore code\n```\n\n";
  out += "~~~\nunfenced body\n~~~~\n\n";
  out += "    indented\n    code\n\n  \n    more code\n\n";
  out += "<div>\n<p>raw</p>\n</div>\n\n";
  out += "<!-- comment\nacross lines -->\n\n";
  out += "Setext One\n==========\n\nSetext Two\n----------\n\n";
  out += "\t- tab item\n\t\tindented continuation\n\n";
  out += "- empty item kept open\n\n  \n\n  still in item\n\n- next\n";
  out += "- a\n\n- b\n\n- c\n";
  return out;
}

std::string table_heavy_doc() {
  std::string out;
  out += "| a | b |\n| --- | :-: |\n| c | d |\n| e\\|f | `g\\|h` |\n\n";
  out += "para right above\na|b\n-|-\nx|y\n\n";
  out += "- item with a table\n\n  h1|h2\n  -|-\n  r1|r2\n- next item\n\n";
  out += "> h1|h2\n> -|-\n> r1|r2\n> still quoted\n\n";
  out += "h1|h2|h3\n-|-|-\n1|2\n1|2|3|4\n\n";
  out += "t1|t2\n-|-\nrow\n- a list right after\n- more\n";
  return out;
}

std::string task_front_doc() {
  std::string out;
  out += "---\ntitle: x\n---\n\n";
  out += "- [ ] a\n- [x] b\n  - [ ] nested\n  - [X] nested2\n\n";
  out += "1. [ ] one\n2. [x] two\n\n";
  out += "> - [ ] in a quote\n> - [x] also\n\n";
  out += "- [ ] \n  para on the next line\n\n";
  out += "- [ ] loose a\n\n- [x] loose b\n\n";
  out += "- [x] [y]: /u\n\n[y]\n";
  return out;
}

Edit task_front_edit(std::mt19937& rng, const std::string& text, std::size_t max_size) {
  static const std::vector<std::string> pieces = {
      "a", " ", "\n", "\n\n", "\n\n\n", "- ", "  - ", "* ", "+ ", "1. ", "2) ", "> ", ">> ", "> > ",
      "```\n", "```js\n", "~~~\n", "~~~~\n", "    ", "\t", "\t\t", "<div>\n", "</div>\n", "<!--\n", "-->\n",
      "===\n", "---\n", "***\n", "___\n", "# ", "## ", "###### ", "[x]: /u\n", "\r\n", "x\r", "  \n",
      "[y]: /v\n", "[x]:", " /u", " \"t\"\n", "[", "]", "[X]: /w\n",
      "|", " | ", "|-|\n", "-|-\n", ":-:|", "\\|", "`|`", "| a | b |\n",
      "[ ] ", "[x] ", "[X] ", "+++\n",
  };
  auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
  if (pick(8) == 0) {
    if (!text.empty() && pick(3) == 0) {
      const std::size_t nl = text.find('\n');
      const std::size_t removed = nl == std::string::npos ? text.size() : nl + 1;
      return Edit{0, removed, ""};
    }
    return Edit{0, 0, pick(2) == 0 ? "---\n" : "+++\n"};
  }
  return piece_edit(rng, text, max_size, pieces);
}

std::string autolink_doc() {
  std::string out;
  out += "See www.a.b and http://x.y and a@b.c in a paragraph.\n\n";
  out += "- www.c.d in an item\n- http://e.f/g and a.b+c@d-e.f\n";
  out += "  - *www.h.i* and [www.j.k]\n\n";
  out += "> http://m.n in a quote\n> a@m.c and www.o.p\n\n";
  out += "| a | b |\n| --- | --- |\n| www.q.r\\|s | a\\_b@c.d |\n\n";
  out += "## www.t.u and http://v.w heading\n\n";
  out += "a\\_b@c.d and &#97;@b.c over an escape and an entity.\n";
  return out;
}

Edit autolink_edit(std::mt19937& rng, const std::string& text, std::size_t max_size) {
  static const std::vector<std::string> pieces = {
      "a", " ", "\n", "\n\n", "\n\n\n", "- ", "  - ", "* ", "+ ", "1. ", "2) ", "> ", ">> ", "> > ",
      "```\n", "```js\n", "~~~\n", "~~~~\n", "    ", "\t", "\t\t", "<div>\n", "</div>\n", "<!--\n", "-->\n",
      "===\n", "---\n", "***\n", "___\n", "# ", "## ", "###### ", "[x]: /u\n", "\r\n", "x\r", "  \n",
      "[y]: /v\n", "[x]:", " /u", " \"t\"\n", "[", "]", "[X]: /w\n",
      "|", " | ", "|-|\n", "-|-\n", ":-:|", "\\|", "`|`", "| a | b |\n",
      "[ ] ", "[x] ",
      "www.", "http://", "@", ".", "_", "(", ")", ";", "&amp;", "<", "[", "]", "mailto:", "\\",
  };
  return piece_edit(rng, text, max_size, pieces);
}

std::string crlf(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '\n') out += '\r';
    out += c;
  }
  return out;
}

}  

TEST_CASE("md incremental: apply_edit matches a full parse over the sample", "[random]") {
  for (unsigned seed = 1; seed <= 5; ++seed) random_md_edits(read_sample(), seed, 400);
}

TEST_CASE("md incremental: apply_edit matches a full parse over a block-heavy document", "[random]") {
  for (unsigned seed = 11; seed <= 20; ++seed) random_md_edits(block_heavy_doc(), seed, 400);
}

TEST_CASE("md incremental: apply_edit matches a full parse over CRLF text", "[random]") {
  for (unsigned seed = 31; seed <= 35; ++seed) random_md_edits(crlf(block_heavy_doc()), seed, 300);
}

TEST_CASE("md incremental: apply_edit matches a full parse over a link-heavy document", "[random]") {
  for (unsigned seed = 41; seed <= 50; ++seed) random_md_edits(link_heavy_doc(), seed, 400, link_edit);
}

TEST_CASE("md incremental: apply_edit matches a full parse over a table-heavy document", "[random]") {
  for (unsigned seed = 61; seed <= 70; ++seed) random_md_edits(table_heavy_doc(), seed, 400);
}

TEST_CASE("md incremental: apply_edit matches a full parse over a task and front matter document", "[random]") {
  for (unsigned seed = 71; seed <= 80; ++seed) random_md_edits(task_front_doc(), seed, 400, task_front_edit);
}

TEST_CASE("md incremental: apply_edit matches a full parse over an autolink document", "[random]") {
  for (unsigned seed = 81; seed <= 90; ++seed) random_md_edits(autolink_doc(), seed, 400, autolink_edit);
}

TEST_CASE("md incremental: front matter restart") {
  auto check = [](const std::string& before, const Edit& edit, std::size_t expected_restart) {
    auto inc = md::make_markdown_parser();
    const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
    REQUIRE(mp != nullptr);
    inc->parse(before);
    std::string after = before;
    after.replace(edit.offset, edit.removed, edit.inserted);
    inc->apply_edit(edit, after);
    INFO("before: " << before << "\nafter: " << after);
    CHECK(mp->last_run_stats().incremental);
    CHECK(mp->last_run_stats().restart_line == expected_restart);
    auto fresh = md::make_markdown_parser();
    fresh->parse(after);
    const std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
    INFO(diff);
    CHECK(diff.empty());
  };
  const std::string doc = "---\na: b\n---\nx";
  check(doc, Edit{13, 1, "y"}, 3);
  check(doc, Edit{5, 1, "c"}, 0);
  check("---\na\n\nb\n", Edit{7, 0, "---\n"}, 0);
}

TEST_CASE("md incremental: locality of typing in a paragraph and a list item", "[random]") {
  std::string text = "Intro\n\n";
  for (int i = 0; i < 5000; ++i) text += "- item " + std::to_string(i) + "\n";
  text += "\n";
  for (int i = 0; i < 2000; ++i) text += "word" + std::to_string(i) + " ";
  text += "\n";

  auto p = md::make_markdown_parser();
  p->parse(text);
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(p.get());
  REQUIRE(mp != nullptr);

  const std::size_t at = text.find("- item 2500\n") + std::string("- item 2500").size();
  Edit e1{at, 0, "!"};
  text.replace(e1.offset, e1.removed, e1.inserted);
  p->apply_edit(e1, text);
  INFO("lines_parsed=" << mp->last_run_stats().lines_parsed);
  CHECK(mp->last_run_stats().incremental);
  CHECK(mp->last_run_stats().lines_parsed < 20);

  const std::size_t at2 = text.find("word1000");
  Edit e2{at2, 0, "x"};
  text.replace(e2.offset, e2.removed, e2.inserted);
  p->apply_edit(e2, text);
  INFO("lines_parsed=" << mp->last_run_stats().lines_parsed);
  CHECK(mp->last_run_stats().incremental);
  CHECK(mp->last_run_stats().lines_parsed <= 4);

  auto fresh = md::make_markdown_parser();
  fresh->parse(text);
  const std::string diff = lumos_test::diff_trees(p->tree(), fresh->tree());
  INFO(diff);
  REQUIRE(diff.empty());
}

namespace {

std::vector<std::string> read_spec_examples(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  std::istringstream text(ss.str());
  static const std::string fence(32, '`');
  std::vector<std::string> out;
  std::string line;
  int state = 0;
  std::string markdown;
  while (std::getline(text, line)) {
    if (state == 0) {
      if (line.rfind(fence + " example", 0) == 0) {
        state = 1;
        markdown.clear();
      }
    } else if (state == 1) {
      if (line == ".") {
        state = 2;
      } else {
        markdown += line + "\n";
      }
    } else {
      if (line == fence) {
        state = 0;
        for (std::size_t at = markdown.find("\xE2\x86\x92"); at != std::string::npos;
             at = markdown.find("\xE2\x86\x92", at + 1))
          markdown.replace(at, 3, "\t");
        out.push_back(markdown);
      }
    }
  }
  return out;
}

}  

TEST_CASE("md incremental: spec examples built and edited one by one", "[random]") {
  const std::string path = std::string(LUMOS_SPEC_DIR) + "/commonmark-0.31.2.txt";
  const std::vector<std::string> examples = read_spec_examples(path);
  REQUIRE(examples.size() > 600);

  auto inc = md::make_markdown_parser();
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
  REQUIRE(mp != nullptr);
  std::string text;
  inc->parse(text);
  lumos_test::dump_start(text);
  auto fresh = md::make_markdown_parser();
  const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
  REQUIRE(fresh_mp != nullptr);
  std::mt19937 rng(7);
  std::size_t checked = 0;
  for (std::size_t i = 0; i < examples.size(); i += 3) {  
    const Edit e{text.size(), 0, examples[i]};
    lumos_test::dump_edit(e);
    text.replace(e.offset, e.removed, e.inserted);
    const md::RefIndex old_refs = mp->refs();
    const std::size_t fallbacks_before = mp->fallback_count();
    inc->apply_edit(e, text);
    INFO("example index " << i);
    INFO(lumos_test::source_window(text, e.offset));
    INFO("fallback_count=" << mp->fallback_count() << " reason: " << mp->last_fallback_reason());
    REQUIRE(mp->fallback_count() == fallbacks_before);
    fresh->parse(text);
    {
      const std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
      INFO(diff);
      REQUIRE(diff.empty());
    }
    {
      const std::string refs_diff = lumos_test::diff_refs(mp->refs(), fresh_mp->refs());
      INFO(refs_diff);
      REQUIRE(refs_diff.empty());
      std::vector<std::string> flipped = mp->last_flipped();
      std::sort(flipped.begin(), flipped.end());
      REQUIRE(flipped == expected_flips(old_refs, mp->refs()));
    }
    check_tree(inc->tree(), text, e.offset);
    ++checked;
    if (checked % 7 == 0) {
      Edit re = structure_edit(rng, text, text.size() + 200);
      lumos_test::dump_edit(re);
      text.replace(re.offset, re.removed, re.inserted);
      const md::RefIndex old_refs2 = mp->refs();
      const std::size_t fallbacks_before2 = mp->fallback_count();
      inc->apply_edit(re, text);
      INFO(lumos_test::source_window(text, re.offset));
      INFO("fallback_count=" << mp->fallback_count() << " reason: " << mp->last_fallback_reason());
      REQUIRE(mp->fallback_count() == fallbacks_before2);
      fresh->parse(text);
      const std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
      INFO(diff);
      REQUIRE(diff.empty());
      const std::string refs_diff = lumos_test::diff_refs(mp->refs(), fresh_mp->refs());
      INFO(refs_diff);
      REQUIRE(refs_diff.empty());
      std::vector<std::string> flipped = mp->last_flipped();
      std::sort(flipped.begin(), flipped.end());
      REQUIRE(flipped == expected_flips(old_refs2, mp->refs()));
      check_tree(inc->tree(), text, re.offset);
    }
  }
}

TEST_CASE("md incremental: reference definition flips") {
  auto check = [](const std::string& before, const Edit& edit, std::vector<std::string> expected) {
    auto inc = md::make_markdown_parser();
    const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
    REQUIRE(mp != nullptr);
    inc->parse(before);
    const md::RefIndex old_refs = mp->refs();
    std::string after = before;
    after.replace(edit.offset, edit.removed, edit.inserted);
    inc->apply_edit(edit, after);
    INFO("before: " << before << "\nafter: " << after);
    INFO("fallback_count=" << mp->fallback_count() << " reason: " << mp->last_fallback_reason());
    REQUIRE(mp->fallback_count() == 0);

    auto fresh = md::make_markdown_parser();
    const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
    fresh->parse(after);
    CHECK(lumos_test::diff_trees(inc->tree(), fresh->tree()).empty());
    CHECK(lumos_test::diff_refs(mp->refs(), fresh_mp->refs()).empty());

    CHECK(expected_flips(old_refs, mp->refs()) == expected);
    std::vector<std::string> flipped = mp->last_flipped();
    std::sort(flipped.begin(), flipped.end());
    CHECK(flipped == expected);
  };

  SECTION("add a definition") { check("x\n\n", {3, 0, "[a]: /u\n"}, {"a"}); }
  SECTION("remove it") { check("x\n\n[a]: /u\n", {3, 8, ""}, {"a"}); }
  SECTION("rename the label") { check("x\n\n[a]: /u\n", {4, 1, "b"}, {"a", "b"}); }
  SECTION("change only the destination (no flip)") { check("x\n\n[a]: /u\n", {8, 2, "/v"}, {}); }
  SECTION("a second definition of a defined label (no flip)") {
    check("[a]: /u\n\nx\n\n", {12, 0, "[a]: /v\n"}, {});
  }
  SECTION("a definition inside a taken list item (the item before the edited one)") {
    check("- x\n- [a]: /u\n- b\n  more\n- c\n", {24, 0, "!"}, {});
  }
  SECTION("a leaf before the run") {
    check("[a]\n\nx\n", {5, 1, "[a]: /u"}, {"a"});
  }
  SECTION("a leaf after the run") { check("x\n\n[a]\n", {0, 1, "[a]: /u"}, {"a"}); }
  SECTION("*[a*] outside the run, flip by adding") {
    check("*[a*]\n\nmid\n\nx\n", {12, 1, "[a*]: /u"}, {"a*"});
  }
  SECTION("*[a*] outside the run, flip by removing") {
    check("*[a*]\n\nmid\n\n[a*]: /u\n", {12, 8, "x"}, {"a*"});
  }
  SECTION("[b][a] outside the run, flip by adding") {
    check("[b][a]\n\nmid\n\nx\n", {13, 1, "[a]: /u"}, {"a"});
  }
  SECTION("[b][a] outside the run, flip by removing") {
    check("[b][a]\n\nmid\n\n[a]: /u\n", {13, 7, "x"}, {"a"});
  }
  SECTION("a heading dependent") { check("# [a]\n\nx\n", {7, 1, "[a]: /u"}, {"a"}); }
  SECTION("a leaf that asks a different label set after the flip") {
    check("[a][c][b]\n\nx\n", {11, 1, "[c]: /u"}, {"c"});
  }
  SECTION("a taken dependent") {
    auto inc = md::make_markdown_parser();
    const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
    REQUIRE(mp != nullptr);
    const std::string before = "- x\n- [a]\n- b\n  more\n- c\n";
    inc->parse(before);
    const Edit edit{23, 1, "[a]: /u"};
    std::string after = before;
    after.replace(edit.offset, edit.removed, edit.inserted);
    inc->apply_edit(edit, after);
    INFO("before: " << before << "\nafter: " << after);
    REQUIRE(mp->fallback_count() == 0);
    INFO("takes=" << mp->last_run_stats().takes);
    REQUIRE(mp->last_run_stats().takes > 0);
    CHECK(has_link_ancestor(inc->tree(), after.find('a')));

    auto fresh = md::make_markdown_parser();
    const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
    fresh->parse(after);
    CHECK(lumos_test::diff_trees(inc->tree(), fresh->tree()).empty());
    CHECK(lumos_test::diff_refs(mp->refs(), fresh_mp->refs()).empty());
  }
}

TEST_CASE("md incremental: reference index compaction") {
  auto inc = md::make_markdown_parser();
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
  REQUIRE(mp != nullptr);
  std::string text = "x\n";
  inc->parse(text);
  constexpr int n = 200;
  for (int i = 1; i <= n; ++i) {
    const std::string piece = "[l" + std::to_string(i) + "] ";
    const Edit e{1, 0, piece};
    text.insert(e.offset, piece);
    inc->apply_edit(e, text);
  }
  {
    auto fresh = md::make_markdown_parser();
    fresh->parse(text);
    const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
    CHECK(lumos_test::diff_refs(mp->refs(), fresh_mp->refs()).empty());
  }

  bool compacted = false;
  for (int i = n; i >= 1; --i) {
    const std::string piece = "[l" + std::to_string(i) + "] ";
    const std::size_t before_labels = mp->refs().label_count();
    const std::size_t at = text.find(piece);
    REQUIRE(at != std::string::npos);
    const Edit e{at, piece.size(), ""};
    text.erase(e.offset, e.removed);
    inc->apply_edit(e, text);
    if (mp->refs().label_count() < before_labels) compacted = true;
  }
  INFO("final labels=" << mp->refs().label_count());
  CHECK(compacted);

  auto fresh = md::make_markdown_parser();
  fresh->parse(text);
  const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
  CHECK(lumos_test::diff_trees(inc->tree(), fresh->tree()).empty());
  CHECK(lumos_test::diff_refs(mp->refs(), fresh_mp->refs()).empty());
}

TEST_CASE("md incremental: regression cases found by random edits") {
  struct Case {
    std::string before;
    Edit edit;
    std::size_t expect_fallback = 0;
  };
  const std::vector<Case> cases = {
      {"> Quote with *em\n> across* lines.\n>\n> > Nested quote.\n\nafter\n", {5, 0, "-->\n"}},
      {"3) other\n4) list\n\nafter\n", {9, 0, "+ a\t"}},
      {"- a\n- b\n\n> quote\n", {4, 0, "!"}},
      {"~~~\nfence body\n~~~\nmore\n", {13, 0, "\t\t"}},
      {"e\n- two\n  - n", {10, 0, "a\n\n\n2) "}},
      {"- one\n- two\n  - nested *em*\n  - [ ] open task\n  - [x] done task\n- three\n\n1",
       {14, 0, "a\n\n\n2) "}},
      {"> more\r\n>> deep\r\n>\r\n>", {8, 0, "***\n"}},
      {"- nested *em*\n  - [ ] open task\n  - [x] done task\n- three\n\n1", {0, 0, "a\n\n\n2) "}},
      {"> after blank\n> - item in quote\n> - item2\n>\n>   l", {0, 17, ""}},
      {"```cpp title\r\ncode line\r\n\r\nmore code\r\n```\r\n\r\n~~~\r\nunfenced body\r\n~~~~\r\n\r\n"
       "    indented\r\n    code\r\n\r\n  \r\n    more code\r\n\r\n<div>\r\n<p>raw</p>\r\n</div>\r\n\r\n"
       "<!-- comment\r\nacross lines -->\r\n\r\nSetext One\r\n==========\r\n\r\nSetext Two\r\n----------\r\n\r\n"
       "\t- tab item\r\n\t\tindented",
       {257, 1, "> > \t\t-->\n"}},
      {"> # a\n\n> # b\n> # c\nafter\n", {6, 1, ""}},
      {"\n```\n\n", {0, 0, "a"}},
      {"#\n```\r}", {0, 0, "\t"}},
      {"~~~\n\n", {3, 0, "\n"}},
      {"~~~\rm", {3, 0, "\n"}},
      {"~~~\n~~~e\n~~~", {0, 0, "`"}},
      {"\n", {0, 0, "**\n\n"}},
      {"a\n\nb", {3, 0, "\n"}},
      {"```\n```\n ```\n\t\nx", {15, 0, "a"}},
      {"i\n a", {2, 2, "<!--"}},
      {"g\n-", {2, 0, "\n"}},
      {"~\n>", {2, 1, "="}},
      {">+\n>\n)", {5, 1, ""}},
      {"- a\n- b\n", {4, 0, "\n\n"}},
      {"b\n    - c\n  ", {12, 0, "~~~~\n\n"}},
      {"    a\n\n    b\nx\n", {7, 0, "z"}},
      {"\td\n\n    c", {4, 5, ""}},
      {">     a\n>\n>\n>     b\n", {11, 0, " "}},
      {"    a\n      \n    b\n", {7, 0, " "}},
      {"    a\n    b\n    c\n", {10, 1, ""}},
      {":\n<div\n\n", {1, 0, "\t"}},
      {"1.\n*\n\n", {2, 0, "\n"}},
      {"- # h\n\n  b\n", {9, 0, "x"}},
      {"- a\n\n- b\n", {4, 0, "x\n"}},
      {"```\n```\nt\n#", {10, 0, "\n"}},
      {"  - d\n\r", {6, 1, "\t---"}},
      {"~~~\n~~~\n- |\n  ", {14, 0, "***"}},
      {"- a\n- b\n- c\n- d\n- e\n", {11, 0, "!"}},
      {"- a\n\n- b\n\n- c\n\n- d\n\n- e\n", {13, 0, "!"}},
      {"- a\n  - x\n  - y\n- b\n- c\n", {9, 0, "!"}},
      {"> - a\n> - b\n> - c\n> - d\n", {11, 0, "!"}},
      {"- a\n- b\n- c\n- d\n- e\n", {8, 2, ""}},
      {"```\n```\n___\n\n", {12, 0, "-"}},
      {"x\n\n---\n[d]: /u\n\n\nh\n", {3, 0, "-"}},
      {"# x\nfoo\n\n\n", {4, 3, "[d]: /u"}},
      {"### h\n[x]: /u\n\n\nx\n", {14, 1, ""}},
  };
  for (const Case& c : cases) {
    auto inc = md::make_markdown_parser();
    const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
    REQUIRE(mp != nullptr);
    inc->parse(c.before);
    std::string after = c.before;
    after.replace(c.edit.offset, c.edit.removed, c.edit.inserted);
    inc->apply_edit(c.edit, after);
    auto fresh = md::make_markdown_parser();
    fresh->parse(after);
    INFO("before: " << c.before << "\nafter: " << after);
    INFO("fallback_count=" << mp->fallback_count() << " reason: " << mp->last_fallback_reason());
    CHECK(mp->fallback_count() == c.expect_fallback);
    const std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
    INFO(diff);
    CHECK(diff.empty());
    check_tree(inc->tree(), after, c.edit.offset);
  }
}
