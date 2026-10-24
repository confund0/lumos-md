#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/md/html_renderer.hpp"

namespace lumos {

enum class Severity : std::uint8_t { warning, error };
enum class DiagnosticCode : std::uint8_t { link_expansion_large };

struct Diagnostic {
  Severity severity = Severity::warning;
  DiagnosticCode code = DiagnosticCode::link_expansion_large;
  std::size_t start = 0, end = 0;  
  std::uint64_t value = 0;         
};

enum class UrlForm : std::uint8_t { relative_path, absolute_path, file_url, scheme_url, other };

struct LinkTarget {
  md::UrlKind kind = md::UrlKind::link;  
  UrlForm form = UrlForm::other;
  std::string url;         
  std::size_t start = 0, end = 0;  
};

struct PropertiesOptions {
  std::uint64_t link_expansion_limit = std::uint64_t{1} << 20;
};

struct Properties {
  std::size_t bytes = 0, characters = 0, words = 0, lines = 0;
  std::size_t headings = 0, paragraphs = 0, links = 0, images = 0, code_blocks = 0, tables = 0;
  std::size_t tasks_done = 0, tasks_open = 0;
  std::uint64_t link_expansion = 0;
  std::vector<LinkTarget> targets;
  std::vector<Diagnostic> diagnostics;
};

UrlForm url_form(std::string_view url) noexcept;

Properties properties(const Document& document, const PropertiesOptions& options = {});

}  
