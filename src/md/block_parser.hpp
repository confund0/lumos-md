#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"
#include "md/block_state.hpp"
#include "md/content.hpp"
#include "md/features.hpp"
#include "md/line_reader.hpp"
#include "md/scanners.hpp"

namespace lumos::md {

inline constexpr std::uint32_t no_index = UINT32_MAX;
inline constexpr std::uint32_t deferred_parent = no_node - 1;

struct BlockOutput {
  std::vector<Node> nodes;  
  std::vector<std::size_t> line_starts;
  std::vector<StateId> line_states;

  struct Definition {
    std::size_t start = 0;
    std::string label;
  };
  std::vector<Definition> definitions;

  struct Leaf {
    std::uint32_t node = 0;
    std::vector<Segment> segments;
  };
  std::vector<Leaf> leaves;

  void clear();
};

class BlockParser {
 public:
  BlockParser(const Features& features, StateArena& arena) noexcept : features_(features), arena_(&arena) {}

  void begin(std::size_t text_size, BlockOutput& out, const TextSource& text, std::size_t start_line = 0);
  void begin_run(std::size_t text_size, BlockOutput& out, std::size_t start_line, const TextSource& text);
  StateId feed(const Line& line);
  void finish();
  void finish_run();

  StateId state() const noexcept { return state_; }
  std::size_t open_depth() const noexcept { return open_.size() - 1; }
  std::size_t open_leaf_index() const noexcept {
    const NodeKind k = open_.back().kind;
    return k == NodeKind::paragraph || k == NodeKind::table ? open_.size() - 1 : static_cast<std::size_t>(no_index);
  }

  struct TakenSibling {
    std::size_t node_index;
    bool last_line_blank;
  };
  struct TakenLine {
    std::size_t start;
    StateId state;
  };
  void take(std::size_t depth, std::span<const Node> nodes, NodeId first_id, NodeId old_c, std::ptrdiff_t shift,
            std::span<const TakenSibling> siblings,
            std::span<const TakenLine> lines, std::size_t content_end_new, StateId next_line_state);

 private:
  struct Open {
    StateId entry = no_state;
    NodeKind kind = NodeKind::document;  
    std::uint32_t rec = no_index;        
    std::uint32_t node = no_index;       
    std::size_t start = 0;
    std::size_t content_end = 0;  
    std::size_t cover = 0;        
    std::uint32_t content = no_index;  
    bool fenced = false;
    char fence_char = 0;
    std::uint8_t fence_offset = 0;
    std::uint32_t fence_length = 0;
    std::uint8_t html_type = 0;
    bool list_blank = false;  
    bool tight_done = false;  
    bool table_tried = false;
    std::vector<Alignment> table_align;
    std::size_t table_rows = 0;          
    std::size_t table_filled_cells = 0;  
  };

  struct Rec {
    NodeKind kind = NodeKind::document;
    std::uint32_t parent = no_index;
    std::uint32_t first_child = no_index;
    std::uint32_t last_child = no_index;
    std::uint32_t next = no_index;
    std::uint32_t prev = no_index;
    std::size_t start_line = 0;
    std::size_t llb_time = 0;  
    bool llb = false;          
    bool checked = false;      
    bool open = true;
    std::uint32_t node = no_index;  
  };

  struct ParaLine {
    std::size_t prefix_start = 0;  
    std::size_t prefix_end = 0;
    std::size_t content_start = 0;
    std::size_t trimmed_end = 0;   
    std::size_t line_end = 0;      
    std::size_t row_start = 0;
    std::size_t text_end = 0;
  };

  struct PendingBlank {
    std::size_t prefix_start = 0;
    std::size_t prefix_end = 0;
    std::uint8_t virtual_spaces = 0;
    bool marker = false;  
  };

  std::size_t match_open_blocks(LineCursor& cur, const Line& line, bool& all_matched, bool& fence_closed,
                                bool& early_exit);
  void set_list_blank(std::size_t index, bool value);
  void reintern_from(std::size_t index);
  void open_new_blocks(LineCursor& cur, const Line& line, std::size_t last_matched, bool all_matched);
  void add_text(LineCursor& cur, const Line& line, std::size_t last_matched);

  std::size_t attach(NodeKind kind);
  Open& push(NodeKind kind, const OpenBlock& block, std::size_t parent, std::size_t start);
  void close_top();
  void close_to(std::size_t index);
  void close_matched_lists_first(std::size_t p);
  void close_paragraph(Open& o);
  void close_indented_code(Open& o);
  void close_list(Open& o);
  void emit_paragraph_lines(std::uint32_t parent, bool strip_pipe_escapes = false);
  void convert_to_setext(std::size_t index, std::uint8_t level, std::size_t marker_start, std::size_t marker_end,
                         const Line& line);
  void add_para_line(const Line& line, std::size_t prefix_end, std::size_t content_start,
                     std::size_t row_start);
  bool try_start_table(std::size_t index, const Line& line, std::size_t fns, std::string_view rest);
  bool try_add_table_row(std::size_t index, const Line& line, std::size_t fns, std::string_view rest);
  void add_table_row_nodes(const TableRowScan& row, std::size_t base, std::uint32_t row_node, bool is_delimiter,
                           std::span<const Alignment> aligns);
  void append_pipe_escaped(std::size_t start, std::size_t end, std::vector<Segment>& segments);
  void add_escape_node(std::size_t pos) { add_node(NodeKind::escape, pos, pos + 1, deferred_parent); }
  void add_literal_line(Open& o, const Line& line, LineCursor& cur);
  void add_prefix_node(std::size_t start, std::size_t end, std::uint8_t virtual_spaces = 0);
  std::uint32_t add_node(NodeKind kind, std::size_t start, std::size_t end, std::uint32_t parent);
  void set_item_has_child(std::size_t index);
  void set_item_empty(std::size_t index);
  void unlink_rec(std::uint32_t rec);
  bool take_paragraph_definitions(std::size_t index, bool keep_open);

  void set_end(std::uint32_t node_ref, std::size_t end);
  void set_last_line_blank(std::uint32_t node_ref, bool value);
  struct EndLlb {
    std::size_t end = 0;
    bool last_line_blank = false;
  };
  EndLlb effective_close(std::size_t open_index) const;
  void resolve_prefixes(std::uint32_t root_ref, std::size_t start_index);

  void llb_write(std::uint32_t rec, bool value);
  bool ends_with_blank_line(std::uint32_t rec);
  std::uint32_t new_rec(NodeKind kind, std::size_t parent_index);

  Features features_;
  StateArena* arena_;
  BlockOutput* out_ = nullptr;
  const TextSource* text_ = nullptr;  
  StateId state_ = document_state;
  std::size_t text_size_ = 0;
  std::size_t line_no_ = 0;
  std::size_t line_start_ = 0;
  std::size_t matched_pos_ = 0;    
  std::size_t matched_index_ = 0;  
  std::size_t thematic_fail_ = 0;  
  bool opened_ = false;            
  bool setext_ = false;            
  bool table_line_ = false;        
  bool table_started_ = false;     
  std::size_t flags_changed_from_ = SIZE_MAX;  
  bool force_next_line_state_ = false;
  StateId forced_next_line_state_ = no_state;

  std::vector<Open> open_;  
  std::vector<Rec> recs_;
  std::vector<ParaLine> para_;           
  std::vector<PendingBlank> pending_;    
  std::vector<std::pair<std::size_t, std::size_t>> taken_ranges_;
};

}  
