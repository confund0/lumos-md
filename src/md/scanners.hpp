#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "lumos/syntax.hpp"

namespace lumos::md {

constexpr bool is_cmark_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool scan_thematic_break(std::string_view s, std::size_t* fail_at) noexcept;

std::uint8_t scan_atx_heading_start(std::string_view s, std::size_t* content_start) noexcept;

struct AtxContent {
  std::size_t content_end = 0;
  std::size_t closing_start = 0;
  std::size_t closing_end = 0;
};
AtxContent scan_atx_content(std::string_view s, std::size_t from) noexcept;

std::size_t scan_fence_open(std::string_view s) noexcept;

std::size_t scan_fence_close(std::string_view s) noexcept;

std::uint8_t scan_setext_underline(std::string_view s, std::size_t* run_end) noexcept;

struct ListMarker {
  bool ordered = false;
  char ch = 0;                
  std::uint32_t number = 0;   
  std::size_t width = 0;      
};
std::optional<ListMarker> scan_list_marker(std::string_view s, bool interrupts_paragraph) noexcept;

std::uint8_t scan_html_block_start(std::string_view s) noexcept;
std::uint8_t scan_html_block_start_7(std::string_view s) noexcept;

bool html_block_ends_on_line(std::uint8_t type, std::string_view s) noexcept;

std::size_t scan_html_tag(std::string_view s) noexcept;

bool is_valid_utf8(std::string_view s) noexcept;

struct TableToken {
  bool pipe = false;      
  std::size_t start = 0;  
  std::size_t end = 0;
};
struct TableRowScan {
  std::vector<TableToken> tokens;  
  std::size_t cells = 0;           
};
std::optional<TableRowScan> scan_table_row(std::string_view s) noexcept;
bool is_table_row(std::string_view s) noexcept;

struct TableDelimiterScan {
  TableRowScan row;
  std::vector<Alignment> align;
};
std::optional<TableDelimiterScan> scan_table_delimiter_row(std::string_view s) noexcept;

struct TaskMarker {
  std::size_t start = 0, end = 0;  
  bool checked = false;
};
std::optional<TaskMarker> scan_task_marker(std::string_view line, std::size_t bullet, std::size_t after) noexcept;

}  
