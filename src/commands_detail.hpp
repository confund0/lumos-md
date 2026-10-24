#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/log.hpp"
#include "lumos/types.hpp"

namespace lumos::detail {

void log_decision(std::string_view event, std::string_view kind, std::size_t edits);

struct LineText {
  std::size_t start = 0;
  std::string text;
  std::size_t end() const { return start + text.size(); }
};

LineText line_text(const Buffer& b, std::size_t line);

struct Context {
  NodeId node = no_node;
  bool quote = false;
  std::size_t from = 0;  
  std::size_t mark_end = 0;  
  std::size_t to = 0;        
  std::string after;         
  NodeId list = no_node;
  bool ordered = false;
  std::string bullet;      
  std::uint64_t number = 0;
  std::string task;        

  std::string blank(std::optional<std::size_t> width, bool trailing = true) const {
    std::string r = quote ? ">" : "";
    if (width) {
      while (r.size() < *width) r += ' ';
      return r;
    }
    const std::size_t used = r.size() + after.size();
    if (to - from > used) r.append(to - from - used, ' ');
    return trailing ? r + after : r;
  }

  std::string marker() const {
    if (quote) return ">" + after;
    return (ordered ? std::to_string(number + 1) : std::string()) + bullet + task + (after.empty() ? " " : after);
  }
};

std::optional<Context> make_context(const Document& doc, NodeId id);

std::vector<Context> contexts_at(const Document& doc, NodeId id, NodeId* code);

std::vector<Context> contexts_at_line(const Document& doc, std::size_t line);

std::string quote_prefix(const std::vector<Context>& ctx);

std::string continuation_prefix(const std::vector<Context>& ctx);

std::string_view line_ending_of(const Buffer& buf, std::size_t line);

NodeId node_before(const Document& doc, std::size_t pos);

std::size_t last_line_of(const Document& doc, NodeId id);

std::size_t map_position(std::size_t pos, const std::vector<Edit>& ascending);

bool is_space(char c);
bool all_space(std::string_view s);  
std::size_t first_line_of(const Document& doc, NodeId id);
bool space_byte(char c);
void apply_to(std::string& text, const std::vector<Edit>& edits);

CommandResult from_ascending(std::vector<Edit> ascending, Selection sel);

std::size_t container_end_column(const std::string& text, const std::vector<Context>& ctx, const Document& doc,
                                 std::size_t line);

std::size_t content_column(const std::string& text, const std::vector<Context>& ctx, const Document& doc,
                           std::size_t line);

struct BlockLine {
  LineText lt;
  std::size_t col = 0;
  NodeId block = no_node;
  bool underline = false;  

  bool blank() const { return col >= lt.text.size(); }
};

BlockLine block_line(const Document& doc, std::size_t line);

std::optional<Edit> blank_before_at(const Document& doc, std::size_t line, const std::vector<Context>& ctx);

std::optional<Edit> blank_after_at(const Document& doc, std::size_t line, const std::vector<Context>& ctx);

std::pair<std::size_t, std::size_t> touched_lines(const Document& doc, const std::vector<Edit>& edits);

bool leaf_block(NodeKind k);
bool container_block(NodeKind k);

struct LineRole {
  NodeKind leaf = NodeKind::document;
  std::vector<NodeKind> containers;
  bool reaches = false;
};

std::optional<LineRole> line_role(const Document& doc, std::size_t line, std::size_t tf, std::size_t tl);

bool keeps_outside(const Document& doc, const CommandResult& r, std::size_t first, std::size_t last);

struct BlockDraft {
  std::vector<Edit> edits;
  Selection sel;
  std::optional<Edit> blank_before;
  std::optional<Edit> blank_after;
  std::optional<Selection> after;
  bool check_typed = false;
};

std::optional<CommandResult> with_typed(const Document& doc, const CommandResult& r, std::string_view typed);

std::optional<CommandResult> resolve_blanks(const Document& doc, BlockDraft draft, std::string_view event,
                                            std::string_view kind);

bool line_in_kind(const Document& doc, std::size_t line, NodeKind k);

std::vector<Context> empty_line_contexts(const Document& doc, std::size_t line_no, const LineText& lt);

std::string blank_line_pad(const Document& doc, std::size_t line);

std::optional<CommandResult> place_block(const Document& doc, const Edit& core, const std::vector<Context>& ctx,
                                         std::size_t node_start, NodeKind k, std::size_t target,
                                         std::string_view event, std::string_view kind);

}  
