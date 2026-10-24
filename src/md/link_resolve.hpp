#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"

namespace lumos::md {

template <class Fn>
void for_chunks(const TextSource& text, std::size_t start, std::size_t end, Fn&& fn) {
  while (start < end) {
    std::string_view c = text.chunk_at(start);
    if (c.empty()) break;
    if (c.size() > end - start) c = c.substr(0, end - start);
    fn(c);
    start += c.size();
  }
}

std::string slice(const TextSource& text, std::size_t start, std::size_t end);

bool is_skipped_literal_child(const Node& k) noexcept;

template <class FnChar>
void walk_inline_bytes(const SyntaxTree& tree, const TextSource& text, NodeId id, std::size_t from, std::size_t to,
                       FnChar&& fn) {
  std::size_t pos = from;
  for (NodeId c : tree.children(id)) {
    const Node& k = tree.node(c);
    if (!is_skipped_literal_child(k) || k.end <= from || k.start >= to) continue;
    if (k.start > pos) for_chunks(text, pos, k.start, [&](std::string_view chunk) { for (char ch : chunk) fn(ch); });
    for (std::uint8_t v = 0; v < k.virtual_spaces; ++v) fn(' ');
    pos = k.end;
  }
  if (to > pos) for_chunks(text, pos, to, [&](std::string_view chunk) { for (char ch : chunk) fn(ch); });
}

std::string raw_inline_text(const SyntaxTree& tree, const TextSource& text, NodeId id, std::size_t from,
                            std::size_t to);

NodeId first_text_child(const SyntaxTree& tree, NodeId id) noexcept;

std::string clean_link_text(std::string_view raw);

struct LinkParts {
  NodeId destination = no_node;
  NodeId title = no_node;
  NodeId label = no_node;  
  NodeId text = no_node;   
};
LinkParts link_parts(const SyntaxTree& tree, NodeId id);

std::string bracket_inner_text(const SyntaxTree& tree, const TextSource& text, NodeId id, bool has_brackets);

std::string link_href(const SyntaxTree& tree, const TextSource& text, NodeId destination);

std::string link_title_text(const SyntaxTree& tree, const TextSource& text, NodeId title);

struct RefDef {
  NodeId destination = no_node;
  NodeId title = no_node;
};
std::unordered_map<std::string, RefDef> build_ref_defs(const SyntaxTree& tree, const TextSource& text);

std::string email_decoded_text(const SyntaxTree& tree, const TextSource& text, NodeId id);

std::string autolink_url(const SyntaxTree& tree, const TextSource& text, NodeId id);

struct Resolved {
  bool wrap = false;
  std::string href;
  bool has_title = false;
  std::string title;
  NodeId destination_node = no_node;
  NodeId title_node = no_node;
};
Resolved resolve_reference(const SyntaxTree& tree, const TextSource& text, NodeId id,
                           const std::unordered_map<std::string, RefDef>& ref_defs);

}  
