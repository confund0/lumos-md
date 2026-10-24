#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"

namespace lumos::md {

enum class UrlKind : std::uint8_t { link, image };

enum class NodeOutput : std::uint8_t {
  keep,          
  replace_tags,  
  replace_all,   
};

struct HtmlOptions {
  bool unsafe = false;
  bool tagfilter = false;
  bool hard_breaks = false;
  std::function<std::optional<std::string>(UrlKind kind, std::string_view url)> url_hook;
  std::function<NodeOutput(const SyntaxTree& tree, const TextSource& text, NodeId id, bool entering,
                           std::string& out)>
      node_hook;
};

bool is_dangerous_url(std::string_view url) noexcept;

void append_escaped_href(std::string_view url, std::string& out);
void append_escaped_html(std::string_view text, std::string& out);

void render_html(const SyntaxTree& tree, const TextSource& text, const HtmlOptions& options, std::string& out);

std::string render_html(const SyntaxTree& tree, const TextSource& text, const HtmlOptions& options = {});

}  
