#include "md/link_resolve.hpp"

#include "md/entities.hpp"
#include "md/ref_index.hpp"
#include "md/unicode.hpp"

namespace lumos::md {

std::string slice(const TextSource& text, std::size_t start, std::size_t end) {
  std::string s;
  for_chunks(text, start, end, [&](std::string_view c) { s.append(c); });
  return s;
}

bool is_skipped_literal_child(const Node& k) noexcept {
  return k.kind == NodeKind::block_continuation || (k.kind == NodeKind::escape && k.size() == 1);
}

std::string raw_inline_text(const SyntaxTree& tree, const TextSource& text, NodeId id, std::size_t from,
                            std::size_t to) {
  std::string s;
  bool prev_cr = false;
  walk_inline_bytes(tree, text, id, from, to, [&](char ch) {
    if (prev_cr && ch == '\n') {
      prev_cr = false;
      return;
    }
    prev_cr = ch == '\r';
    if (ch == '\r') {
      s += '\n';
    } else if (ch == '\0') {
      s += "\xEF\xBF\xBD";
    } else {
      s += ch;
    }
  });
  return s;
}

NodeId first_text_child(const SyntaxTree& tree, NodeId id) noexcept {
  for (NodeId c : tree.children(id))
    if (tree.node(c).kind == NodeKind::text) return c;
  return no_node;
}

std::string clean_link_text(std::string_view raw) {
  std::string decoded;
  decoded.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] == '&') {
      const std::size_t n = scan_entity(raw, i, &decoded);
      if (n > 0) {
        i += n;
        continue;
      }
    }
    decoded += raw[i++];
  }
  std::string out;
  out.reserve(decoded.size());
  for (std::size_t i = 0; i < decoded.size();) {
    if (decoded[i] == '\\' && i + 1 < decoded.size() && is_ascii_punctuation(decoded[i + 1])) {
      out += decoded[i + 1];
      i += 2;
    } else {
      out += decoded[i++];
    }
  }
  return out;
}

LinkParts link_parts(const SyntaxTree& tree, NodeId id) {
  LinkParts p;
  for (NodeId c : tree.children(id)) {
    switch (tree.node(c).kind) {
      case NodeKind::link_destination: p.destination = c; break;
      case NodeKind::link_title: p.title = c; break;
      case NodeKind::link_label: p.label = c; break;
      case NodeKind::link_text: p.text = c; break;
      default: break;
    }
  }
  return p;
}

std::string bracket_inner_text(const SyntaxTree& tree, const TextSource& text, NodeId id, bool has_brackets) {
  const Node& n = tree.node(id);
  std::string raw = raw_inline_text(tree, text, id, n.start, n.end);
  if (has_brackets && raw.size() >= 2) raw = raw.substr(1, raw.size() - 2);
  return raw;
}

std::string link_href(const SyntaxTree& tree, const TextSource& text, NodeId destination) {
  if (destination == no_node) return std::string();
  const Node& d = tree.node(destination);
  return clean_link_text(raw_inline_text(tree, text, destination, d.start, d.end));
}

std::string link_title_text(const SyntaxTree& tree, const TextSource& text, NodeId title) {
  if (title == no_node) return std::string();
  const Node& t = tree.node(title);
  std::string raw = raw_inline_text(tree, text, title, t.start, t.end);
  if (raw.size() >= 2) raw = raw.substr(1, raw.size() - 2);
  return clean_link_text(raw);
}

std::unordered_map<std::string, RefDef> build_ref_defs(const SyntaxTree& tree, const TextSource& text) {
  std::unordered_map<std::string, RefDef> map;
  const auto n = static_cast<NodeId>(tree.size());
  for (NodeId id = 0; id < n; ++id) {
    if (tree.node(id).kind != NodeKind::link_reference_definition) continue;
    const LinkParts p = link_parts(tree, id);
    if (p.label == no_node) continue;
    const std::string label = normalize_label(bracket_inner_text(tree, text, p.label, true));
    map.try_emplace(label, RefDef{p.destination, p.title});
  }
  return map;
}

std::string email_decoded_text(const SyntaxTree& tree, const TextSource& text, NodeId id) {
  std::string s;
  for (NodeId c : tree.children(id)) {
    const Node& k = tree.node(c);
    switch (k.kind) {
      case NodeKind::text: s += raw_inline_text(tree, text, c, k.start, k.end); break;
      case NodeKind::entity: scan_entity(slice(text, k.start, k.end), 0, &s); break;
      case NodeKind::escape: {
        const std::string raw = raw_inline_text(tree, text, c, k.start, k.end);
        if (raw.size() == 2) s += raw[1];
        break;
      }
      default: break;
    }
  }
  return s;
}

std::string autolink_url(const SyntaxTree& tree, const TextSource& text, NodeId id) {
  const Node& n = tree.node(id);
  if (n.variant == autolink_variant_extended_email || n.variant == autolink_variant_extended_email_plain) {
    std::string href;
    if (n.variant == autolink_variant_extended_email) href = "mailto:";
    href += email_decoded_text(tree, text, id);
    return href;
  }
  const NodeId text_id = first_text_child(tree, id);
  const Node& t = tree.node(text_id);
  std::string href;
  if (n.variant == autolink_variant_www || n.variant == autolink_variant_url) {
    if (n.variant == autolink_variant_www) href = "http://";
    href += raw_inline_text(tree, text, text_id, t.start, t.end);
  } else {
    if (n.variant == autolink_variant_email) href = "mailto:";
    href += slice(text, t.start, t.end);
  }
  return href;
}

Resolved resolve_reference(const SyntaxTree& tree, const TextSource& text, NodeId id,
                           const std::unordered_map<std::string, RefDef>& ref_defs) {
  const Node& n = tree.node(id);
  const LinkParts p = link_parts(tree, id);
  Resolved r;
  if (n.variant == link_variant_inline) {
    r.wrap = true;
    r.destination_node = p.destination;
    r.href = link_href(tree, text, p.destination);
    if (p.title != no_node) {
      r.has_title = true;
      r.title_node = p.title;
      r.title = link_title_text(tree, text, p.title);
    }
    return r;
  }
  const std::string raw_label = n.variant == link_variant_full ? bracket_inner_text(tree, text, p.label, true)
                                                                : bracket_inner_text(tree, text, p.text, false);
  const auto it = ref_defs.find(normalize_label(raw_label));
  if (it == ref_defs.end()) return r;  
  const RefDef& def = it->second;
  r.wrap = true;
  r.destination_node = def.destination;
  r.href = link_href(tree, text, def.destination);
  if (def.title != no_node) {
    r.has_title = true;
    r.title_node = def.title;
    r.title = link_title_text(tree, text, def.title);
  }
  return r;
}

}  
