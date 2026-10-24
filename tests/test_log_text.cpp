#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/log.hpp"
#include "lumos/tools.hpp"

namespace {

constexpr std::string_view kMarker = "QZXMARK";

const std::string kText =
    "# QZXMARK heading\n"
    "\n"
    "Setext QZXMARK\n"
    "===\n"
    "\n"
    "A **QZXMARK** _para_ with `QZXMARK` and [QZXMARK](https://QZXMARK.example/x) and\n"
    "a lazy QZXMARK line <https://QZXMARK.example> ~~QZXMARK~~ &amp; [ref][QZXMARK].\n"
    "\n"
    "[QZXMARK]: https://QZXMARK.example/ref \"QZXMARK title\"\n"
    "\n"
    "- QZXMARK one\n"
    "- [ ] QZXMARK task\n"
    "  - QZXMARK nested\n"
    "\n"
    "1. QZXMARK first\n"
    "2. QZXMARK second\n"
    "10. QZXMARK tenth\n"
    "\n"
    "> QZXMARK quote\n"
    "QZXMARK lazy\n"
    "\n"
    "| QZXMARK | b |\n"
    "|---|:-:|\n"
    "| QZXMARK cell | QZXMARK |\n"
    "\n"
    "```QZXMARK\n"
    "QZXMARK code\n"
    "```\n"
    "\n"
    "    QZXMARK indented\n"
    "\n"
    "<div>QZXMARK html</div>\n"
    "\n"
    "---\n"
    "QZXMARK end\n";

struct Capture {
  std::vector<std::string> lines;

  Capture() {
    lumos::set_log_sink([this](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });
    lumos::set_log_level(lumos::LogLevel::debug);
  }
  ~Capture() {
    lumos::set_log_sink(nullptr);
    lumos::set_log_level(lumos::LogLevel::info);
  }
  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;
};

void apply_and_undo(lumos::Document& doc, const std::optional<lumos::CommandResult>& result) {
  if (!result || result->edits.empty()) return;
  doc.begin();
  for (const auto& e : result->edits) doc.apply(e);
  doc.commit();
  doc.undo();
  doc.redo();
  doc.undo();
}

}  

TEST_CASE("logs at debug level carry no document text") {
  Capture capture;
  lumos::Document doc(kText);

  using Command = std::function<std::optional<lumos::CommandResult>(const lumos::Document&, lumos::Selection)>;
  const std::vector<Command> commands = {
      [](const auto& d, auto s) { return lumos::insert_newline(d, s, "\n"); },
      [](const auto& d, auto s) { return lumos::insert_line_break(d, s, "\n"); },
      [](const auto& d, auto s) { return lumos::indent_list(d, s); },
      [](const auto& d, auto s) { return lumos::outdent_list(d, s); },
      [](const auto& d, auto s) { return lumos::delete_markup_backward(d, s); },
      [](const auto& d, auto s) { return lumos::next_cell(d, s, "\n"); },
      [](const auto& d, auto s) { return lumos::previous_cell(d, s); },
      [](const auto& d, auto s) { return lumos::next_row(d, s, "\n"); },
      [](const auto& d, auto s) { return lumos::previous_row(d, s); },
      [](const auto& d, auto s) { return lumos::format_table(d, s); },
      [](const auto& d, auto s) { return lumos::insert_table(d, s, 2, 2, "\n"); },
      [](const auto& d, auto s) { return lumos::toggle_inline(d, s, lumos::InlineKind::strong); },
      [](const auto& d, auto s) { return lumos::toggle_inline(d, s, lumos::InlineKind::code); },
      [](const auto& d, auto s) { return lumos::insert_link(d, s, "QZXMARK", "https://QZXMARK.example"); },
      [](const auto& d, auto s) { return lumos::insert_image(d, s, "QZXMARK", "QZXMARK.png"); },
      [](const auto& d, auto s) { return lumos::remove_link(d, s); },
      [](const auto& d, auto s) { return lumos::set_heading(d, s, 2); },
      [](const auto& d, auto s) { return lumos::set_heading(d, s, 0); },
      [](const auto& d, auto s) { return lumos::toggle_block(d, s, lumos::BlockKind::ordered_list); },
      [](const auto& d, auto s) { return lumos::toggle_block(d, s, lumos::BlockKind::block_quote); },
      [](const auto& d, auto s) { return lumos::toggle_block(d, s, lumos::BlockKind::code_block); },
      [](const auto& d, auto s) { return lumos::insert_thematic_break(d, s); },
      [](const auto& d, auto s) { return lumos::renumber(d, s); },
      [](const auto& d, auto s) { return lumos::marker_to_text(d, s); },
      [](const auto& d, auto s) { return lumos::drop_text(d, {0, s.cursor}, d.size(), true); },
  };

  const std::size_t size = doc.size();
  for (std::size_t at = 0; at < size; at += 5) {
    const std::size_t end = std::min(size, at + 9);
    for (const auto& command : commands) {
      apply_and_undo(doc, command(doc, {at, at}));
      apply_and_undo(doc, command(doc, {at, end}));
    }
    (void)lumos::format_at(doc, {at, end});
  }
  REQUIRE(doc.text() == kText);

  for (std::size_t at : {std::size_t{0}, size / 3, size / 2, size - 1}) {
    for (std::size_t i = 0; i < kMarker.size(); ++i) {
      doc.apply({at + i, 0, std::string(1, kMarker[i])});
    }
    doc.apply({at, kMarker.size(), ""});
  }

  for (const bool regex : {false, true}) {
    const lumos::FindOptions options{.case_sensitive = false, .whole_word = false, .regex = regex};
    (void)lumos::find_all(doc, "QZXMARK", options);
    (void)lumos::find_all(doc, "QZXMARK(", options);
    apply_and_undo(doc, lumos::replace_all(doc, "QZXMARK", "QZXMARK QZXMARK", options));
    if (const auto hits = lumos::find_all(doc, "QZXMARK", options); hits && !hits->empty()) {
      apply_and_undo(doc, lumos::replace_one(doc, hits->front(), "QZXMARK", "x QZXMARK", options));
    }
  }

  (void)lumos::out_of_sequence(doc);
  (void)lumos::properties(doc);
  doc.load(kText + "QZXMARK loaded\n");
  doc.mark_saved();
  doc.wipe();

  for (const std::string_view comp : {"commands", "parser", "document", "tools"}) {
    const std::string key = " comp=" + std::string(comp) + " ";
    INFO(comp);
    CHECK(std::any_of(capture.lines.begin(), capture.lines.end(),
                      [&](const std::string& line) { return line.find(key) != std::string::npos; }));
  }
  for (const auto& line : capture.lines) {
    INFO(line);
    CHECK(line.find(kMarker) == std::string::npos);
  }
}
