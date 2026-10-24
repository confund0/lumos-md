#include "lumos/tools.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

#include "lumos/log.hpp"
#include "md/link_resolve.hpp"
#include "md/unicode.hpp"

namespace lumos {
namespace {

constexpr std::string_view kComp = "tools";

std::string num(std::uint64_t v) { return std::to_string(v); }

bool is_cjk_word_char(char32_t cp) noexcept {
  return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x3FFFF);
}

bool is_word_bearing(NodeKind kind) noexcept {
  return kind == NodeKind::text || kind == NodeKind::code_content || kind == NodeKind::entity ||
         kind == NodeKind::escape || kind == NodeKind::math;
}

void close_word(bool& in_word, std::size_t& words) {
  if (in_word) {
    ++words;
    in_word = false;
  }
}

void scan_word_text(const std::string& bytes, bool& in_word, std::size_t& words) {
  std::size_t i = 0;
  while (i < bytes.size()) {
    const md::Decoded d = md::decode_utf8(bytes, i);
    const std::size_t len = d.len > 0 ? d.len : 1;
    if (md::is_unicode_whitespace(d.cp)) {
      close_word(in_word, words);
    } else if (is_cjk_word_char(d.cp)) {
      close_word(in_word, words);
      ++words;
    } else {
      in_word = true;
    }
    i += len;
  }
}

std::size_t count_characters(const TextSource& text) {
  const std::size_t size = text.size();
  std::size_t count = 0;
  std::size_t offset = 0;
  while (offset < size) {
    const std::string_view chunk = text.chunk_at(offset);
    if (chunk.empty()) break;
    const bool last_chunk = offset + chunk.size() >= size;
    std::size_t i = 0;
    while (i < chunk.size()) {
      const auto b0 = static_cast<unsigned char>(chunk[i]);
      std::size_t need = 1;
      if (b0 >= 0xC2 && b0 <= 0xDF) need = 2;
      else if (b0 >= 0xE0 && b0 <= 0xEF) need = 3;
      else if (b0 >= 0xF0 && b0 <= 0xF4) need = 4;
      if (!last_chunk && chunk.size() - i < need) {
        const std::size_t avail = size - (offset + i);
        const std::string joined = text.slice(offset + i, std::min(need, avail));
        const md::Decoded d = md::decode_utf8(joined, 0);
        ++count;
        i += d.len > 0 ? d.len : 1;
        continue;
      }
      const md::Decoded d = md::decode_utf8(chunk, i);
      ++count;
      i += d.len > 0 ? d.len : 1;
    }
    offset += chunk.size();
  }
  return count;
}

bool drive_letter_colon(std::string_view s) noexcept {
  return s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) != 0 && s[1] == ':';
}

bool is_scheme_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '.' || c == '-';
}

}  

UrlForm url_form(std::string_view url) noexcept {
  if (url.empty() || url.front() == '#' || url.front() == '?') return UrlForm::other;
  std::size_t i = 0;
  while (i < url.size() && is_scheme_char(url[i])) ++i;
  const bool has_scheme = i < url.size() && url[i] == ':' && i >= 1 && std::isalpha(static_cast<unsigned char>(url[0])) != 0;
  if (has_scheme) {
    if (i == 1) return UrlForm::absolute_path;  
    if (i == 4 && std::tolower(static_cast<unsigned char>(url[0])) == 'f' &&
        std::tolower(static_cast<unsigned char>(url[1])) == 'i' &&
        std::tolower(static_cast<unsigned char>(url[2])) == 'l' &&
        std::tolower(static_cast<unsigned char>(url[3])) == 'e') {
      return UrlForm::file_url;
    }
    return UrlForm::scheme_url;
  }
  if (url.size() >= 2 && url[0] == '/' && url[1] == '/') return UrlForm::scheme_url;  
  if (url.front() == '/' || url.front() == '\\') return UrlForm::absolute_path;
  if (drive_letter_colon(url)) return UrlForm::absolute_path;
  return UrlForm::relative_path;
}

Properties properties(const Document& document, const PropertiesOptions& options) {
  const SyntaxTree& tree = document.tree();
  const TextSource& text = document.buffer();
  Properties result;
  result.bytes = document.size();
  result.lines = text.line_count();
  result.characters = count_characters(text);

  const std::unordered_map<std::string, md::RefDef> ref_defs = md::build_ref_defs(tree, text);

  bool in_word = false;
  std::vector<NodeId> open_blocks;  
  const auto node_count = static_cast<NodeId>(tree.size());
  for (NodeId id = 0; id < node_count; ++id) {
    const Node& node = tree.node(id);
    while (!open_blocks.empty() && id >= open_blocks.back()) {
      open_blocks.pop_back();
      close_word(in_word, result.words);
    }
    if (is_block(node.kind)) {
      open_blocks.push_back(node.subtree_end);
      close_word(in_word, result.words);
    } else if (node_class(node.kind) == NodeClass::marker && node.kind != NodeKind::emphasis_delimiter) {
      close_word(in_word, result.words);
    } else if (is_word_bearing(node.kind)) {
      scan_word_text(md::raw_inline_text(tree, text, id, node.start, node.end), in_word, result.words);
    }

    switch (node.kind) {
      case NodeKind::heading: ++result.headings; break;
      case NodeKind::paragraph: ++result.paragraphs; break;
      case NodeKind::code_block: ++result.code_blocks; break;
      case NodeKind::table: ++result.tables; break;
      case NodeKind::task_marker:
        if (node.checked) ++result.tasks_done; else ++result.tasks_open;
        break;
      default: break;
    }

    if (node.kind == NodeKind::link || node.kind == NodeKind::image) {
      if (node.kind == NodeKind::image) ++result.images; else ++result.links;
      const md::Resolved r = md::resolve_reference(tree, text, id, ref_defs);
      if (r.wrap) {
        LinkTarget target;
        target.kind = node.kind == NodeKind::image ? md::UrlKind::image : md::UrlKind::link;
        target.url = r.href;
        target.form = url_form(target.url);
        target.start = node.start;
        target.end = node.end;
        result.targets.push_back(std::move(target));
        if (node.variant != link_variant_inline) {
          std::uint64_t expansion = 0;
          if (r.destination_node != no_node) expansion += tree.node(r.destination_node).size();
          if (r.title_node != no_node) expansion += tree.node(r.title_node).size();
          result.link_expansion += expansion;
        }
      }
    } else if (node.kind == NodeKind::autolink) {
      ++result.links;
      LinkTarget target;
      target.kind = md::UrlKind::link;
      target.url = md::autolink_url(tree, text, id);
      target.form = url_form(target.url);
      target.start = node.start;
      target.end = node.end;
      result.targets.push_back(std::move(target));
    }
  }
  close_word(in_word, result.words);  

  if (result.link_expansion > options.link_expansion_limit) {
    Diagnostic d;
    d.severity = Severity::warning;
    d.code = DiagnosticCode::link_expansion_large;
    d.value = result.link_expansion;
    result.diagnostics.push_back(d);
  }

  log(LogLevel::debug, kComp, "properties",
      {{"bytes", num(result.bytes)}, {"words", num(result.words)}, {"targets", num(result.targets.size())}});
  return result;
}

}  
