#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumos {

enum class NodeKind : std::uint8_t {
  document,
  paragraph,
  heading,  
  block_quote,
  list,  
  list_item,
  code_block,  
  html_block,
  thematic_break,
  table,
  table_header_row,
  table_delimiter_row,
  table_row,
  table_cell,  
  table_delimiter_cell,  
  link_reference_definition,  
  front_matter,  

  emphasis,
  strong,
  strikethrough,
  code_span,
  link,
  image,
  autolink,
  html_inline,
  hard_break,  
  soft_break,  
  entity,  
  math,  
  text,  

  emphasis_delimiter,  
  heading_marker,  
  list_marker,  
  task_marker,  
  code_fence_delimiter,  
  info_string,  
  code_content,  
  code_span_delimiter,
  link_text,  
  link_delimiter,  
  link_destination,  
  link_title,  
  link_label,  
  escape,  
  block_quote_marker,  
  block_continuation,  
  table_pipe,
  math_delimiter,
};

inline constexpr std::size_t node_kind_count = static_cast<std::size_t>(NodeKind::math_delimiter) + 1;

inline constexpr std::uint8_t link_variant_inline = 0;
inline constexpr std::uint8_t link_variant_full = 1;
inline constexpr std::uint8_t link_variant_collapsed = 2;
inline constexpr std::uint8_t link_variant_shortcut = 3;

inline constexpr std::uint8_t autolink_variant_uri = 0;
inline constexpr std::uint8_t autolink_variant_email = 1;
inline constexpr std::uint8_t autolink_variant_www = 2;
inline constexpr std::uint8_t autolink_variant_url = 3;
inline constexpr std::uint8_t autolink_variant_extended_email = 4;
inline constexpr std::uint8_t autolink_variant_extended_email_plain = 5;

std::string_view kind_name(NodeKind kind) noexcept;
bool is_block(NodeKind kind) noexcept;
bool is_marker(NodeKind kind) noexcept;

enum class NodeClass : std::uint8_t { structure, content, marker };
NodeClass node_class(NodeKind kind) noexcept;

enum class Alignment : std::uint8_t { none, left, center, right };

using NodeId = std::uint32_t;
inline constexpr NodeId no_node = UINT32_MAX;

struct Node {
  NodeKind kind = NodeKind::text;
  std::size_t start = 0;  
  std::size_t end = 0;  
  NodeId parent = no_node;
  NodeId next_sibling = no_node;
  NodeId subtree_end = 0;  

  std::uint8_t level = 0;  
  bool setext = false;  
  bool ordered = false;  
  bool checked = false;  
  bool fenced = false;  
  Alignment align = Alignment::none;  
  std::uint8_t variant = 0;
  bool tight = false;  
  std::uint8_t virtual_spaces = 0;
  bool last_line_blank = false;

  std::size_t size() const noexcept { return end - start; }
  bool contains(std::size_t offset) const noexcept { return start <= offset && offset < end; }
};

std::string_view node_text(std::string_view source, const Node& node) noexcept;

class SyntaxTree;

struct TreeEdit {
  static constexpr std::uint32_t old_parent_bit = 0x8000'0000u;

  NodeId first = 0;  
  NodeId last = 0;
  std::vector<Node> nodes;  

  struct Remap {
    NodeId old_id = 0;
    std::uint32_t new_index = 0;
  };
  std::vector<Remap> remap;

  struct Ancestor {
    NodeId id = 0;  
    std::size_t new_end = 0;
    bool set_tight = false;
    bool tight = false;
    bool last_line_blank = false;
  };
  std::vector<Ancestor> ancestors;

  std::ptrdiff_t delta = 0;
};

class ChildRange {
 public:
  class iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = NodeId;
    using difference_type = std::ptrdiff_t;
    using pointer = const NodeId*;
    using reference = NodeId;

    iterator() = default;
    iterator(const SyntaxTree* tree, NodeId id) : tree_(tree), id_(id) {}
    NodeId operator*() const noexcept { return id_; }
    iterator& operator++() noexcept;
    iterator operator++(int) noexcept {
      iterator old = *this;
      ++*this;
      return old;
    }
    bool operator==(const iterator& other) const noexcept { return id_ == other.id_; }

   private:
    const SyntaxTree* tree_ = nullptr;
    NodeId id_ = no_node;
  };

  ChildRange(const SyntaxTree* tree, NodeId first) : tree_(tree), first_(first) {}
  iterator begin() const noexcept { return {tree_, first_}; }
  iterator end() const noexcept { return {tree_, no_node}; }
  bool empty() const noexcept { return first_ == no_node; }

 private:
  const SyntaxTree* tree_;
  NodeId first_;
};

class SyntaxTree {
 public:
  SyntaxTree();
  explicit SyntaxTree(std::vector<Node> nodes);

  std::span<const Node> nodes() const noexcept { return nodes_; }
  std::size_t size() const noexcept { return nodes_.size(); }
  const Node& node(NodeId id) const { return nodes_.at(id); }
  const Node& operator[](NodeId id) const { return nodes_.at(id); }
  NodeId root() const noexcept { return 0; }

  NodeId first_child(NodeId id) const noexcept;
  ChildRange children(NodeId id) const noexcept { return {this, first_child(id)}; }
  std::size_t depth(NodeId id) const noexcept;

  NodeId node_at(std::size_t offset) const noexcept;

  std::string to_debug_string() const;

  std::optional<std::string> validate(std::size_t text_size) const;

  std::optional<std::string> validate_coverage(std::string_view text) const;

  void splice(NodeId first, NodeId last, std::vector<Node> nodes, std::ptrdiff_t delta, std::size_t text_size);

  void apply_tree_edits(std::span<const TreeEdit> edits, std::size_t text_size);

 private:
  void link() noexcept;

  std::vector<Node> nodes_;
};

inline ChildRange::iterator& ChildRange::iterator::operator++() noexcept {
  id_ = tree_->node(id_).next_sibling;
  return *this;
}

}  
