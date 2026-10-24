#pragma once

#include <cstddef>
#include <string_view>

namespace lumos::md {

bool is_autolink_host_char(std::string_view s, std::size_t i) noexcept;

std::size_t autolink_domain(std::string_view rest, bool need_dot) noexcept;

std::size_t autolink_trim(std::string_view link) noexcept;

std::size_t match_www(std::string_view s, std::size_t i) noexcept;

struct AutolinkRange {
  std::size_t start = 0;
  std::size_t end = 0;
};
AutolinkRange match_url(std::string_view s, std::size_t colon) noexcept;

struct EmailMatch {
  std::size_t start = 0;
  std::size_t end = 0;
  bool mailto = true;
};
EmailMatch find_email(std::string_view run, std::size_t from) noexcept;

}  
