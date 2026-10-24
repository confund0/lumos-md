#include "md/autolink.hpp"

#include "md/unicode.hpp"

namespace lumos::md {

namespace {

constexpr bool is_ascii_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

constexpr bool is_ascii_alnum(char c) noexcept { return is_ascii_alpha(c) || (c >= '0' && c <= '9'); }

constexpr char to_lower_ascii(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

constexpr bool is_cmark_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool scheme_equals(std::string_view s, std::size_t start, std::size_t len, std::string_view scheme) noexcept {
  if (len != scheme.size()) return false;
  for (std::size_t k = 0; k < len; ++k) {
    if (to_lower_ascii(s[start + k]) != scheme[k]) return false;
  }
  return true;
}

bool protocol_ends_at(std::string_view s, std::size_t colon_pos, std::size_t floor, std::string_view protocol) noexcept {
  if (protocol.size() > colon_pos + 1 - floor) return false;
  const std::size_t start = colon_pos + 1 - protocol.size();
  if (s.substr(start, protocol.size()) != protocol) return false;
  if (start == floor) return true;
  return !is_ascii_alnum(s[start - 1]);
}

}  

bool is_autolink_host_char(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size()) return false;
  const Decoded d = decode_utf8(s, i);
  if (d.len == 1 && d.cp == 0xFFFD) return false;  
  return !is_unicode_whitespace(d.cp) && !is_autolink_punctuation(d.cp);
}

std::size_t autolink_domain(std::string_view rest, bool need_dot) noexcept {
  const std::size_t size = rest.size();
  if (size == 0) return 0;
  std::size_t i = 1;
  std::size_t np = 0, uscore1 = 0, uscore2 = 0;
  for (; i < size - 1; ++i) {
    if (rest[i] == '\\' && i < size - 2) ++i;
    if (rest[i] == '_') {
      ++uscore2;
    } else if (rest[i] == '.') {
      uscore1 = uscore2;
      uscore2 = 0;
      ++np;
    } else if (!is_autolink_host_char(rest, i) && rest[i] != '-') {
      break;
    }
  }
  if ((uscore1 > 0 || uscore2 > 0) && np <= 10) return 0;
  if (!need_dot) return i;
  return np ? i : 0;
}

std::size_t autolink_trim(std::string_view link) noexcept {
  std::size_t link_end = link.size();
  std::size_t opening = 0, closing = 0;
  for (std::size_t i = 0; i < link_end; ++i) {
    const char c = link[i];
    if (c == '<') {
      link_end = i;
      break;
    } else if (c == '(') {
      ++opening;
    } else if (c == ')') {
      ++closing;
    }
  }

  while (link_end > 0) {
    switch (link[link_end - 1]) {
      case ')':
        if (closing <= opening) return link_end;
        --closing;
        --link_end;
        break;
      case '?':
      case '!':
      case '.':
      case ',':
      case ':':
      case '*':
      case '_':
      case '~':
      case '\'':
      case '"':
        --link_end;
        break;
      case ';': {
        if (link_end < 2) {
          --link_end;
          break;
        }
        std::size_t new_end = link_end - 2;
        while (new_end > 0 && is_ascii_alpha(link[new_end])) --new_end;
        if (new_end < link_end - 2 && link[new_end] == '&') {
          link_end = new_end;
        } else {
          --link_end;
        }
        break;
      }
      default:
        return link_end;
    }
  }
  return link_end;
}

std::size_t match_www(std::string_view s, std::size_t i) noexcept {
  if (i > 0) {
    const char prev = s[i - 1];
    if (prev != '*' && prev != '_' && prev != '~' && prev != '(' && !is_cmark_space(prev)) return 0;
  }

  const std::string_view rest = s.substr(i);
  if (rest.size() < 4 || rest.substr(0, 4) != "www.") return 0;

  std::size_t link_end = autolink_domain(rest, true);
  if (link_end == 0) return 0;

  while (link_end < rest.size() && !is_cmark_space(rest[link_end]) && rest[link_end] != '<') ++link_end;

  return autolink_trim(rest.substr(0, link_end));
}

AutolinkRange match_url(std::string_view s, std::size_t colon) noexcept {
  const std::size_t size = s.size() - colon;
  if (size < 4 || s[colon + 1] != '/' || s[colon + 2] != '/') return {};

  std::size_t rewind = 0;
  while (rewind < colon && is_ascii_alpha(s[colon - rewind - 1])) ++rewind;

  const std::size_t scheme_start = colon - rewind;
  if (!scheme_equals(s, scheme_start, rewind, "http") && !scheme_equals(s, scheme_start, rewind, "https") &&
      !scheme_equals(s, scheme_start, rewind, "ftp")) {
    return {};
  }
  if (!is_autolink_host_char(s, colon + 3)) return {};

  std::size_t link_end = 3;
  const std::size_t domain_len = autolink_domain(s.substr(colon + 3), false);
  if (domain_len == 0) return {};
  link_end += domain_len;

  while (colon + link_end < s.size() && !is_cmark_space(s[colon + link_end]) && s[colon + link_end] != '<') {
    ++link_end;
  }

  link_end = autolink_trim(s.substr(colon, link_end));
  if (link_end == 0) return {};

  return {scheme_start, colon + link_end};
}

EmailMatch find_email(std::string_view run, std::size_t from) noexcept {
  const std::size_t size = run.size();
  std::size_t offset = from;
  bool restarted = false;
  std::size_t at = 0;
  bool auto_mailto = true;
  bool is_xmpp = false;
  std::size_t np = 0;

  while (true) {
    if (!restarted) {
      if (offset >= size) return {size, size, true};
      const std::size_t found = run.find('@', offset);
      if (found == std::string_view::npos) return {size, size, true};
      at = found;
      auto_mailto = true;
      is_xmpp = false;
      np = 0;
    }
    restarted = false;

    std::size_t left = at;
    while (left > offset) {
      const char c = run[left - 1];
      if (is_ascii_alnum(c) || c == '.' || c == '+' || c == '-' || c == '_') {
        --left;
        continue;
      }
      if (c == ':') {
        if (protocol_ends_at(run, left - 1, offset, "mailto:")) {
          auto_mailto = false;
          --left;
          continue;
        }
        if (protocol_ends_at(run, left - 1, offset, "xmpp:")) {
          auto_mailto = false;
          is_xmpp = true;
          --left;
          continue;
        }
      }
      break;
    }

    if (left == at) {
      offset = at + 1;  
      continue;
    }

    std::size_t link_end = 1;
    while (at + link_end < size) {
      const char c = run[at + link_end];
      if (is_ascii_alnum(c)) {
        ++link_end;
        continue;
      }
      if (c == '@') {
        offset = at + 1;
        at = at + link_end;
        restarted = true;
        break;
      }
      if (c == '.' && at + link_end + 1 < size && is_ascii_alnum(run[at + link_end + 1])) {
        ++np;
        ++link_end;
        continue;
      }
      if (c == '/' && is_xmpp) {
        ++link_end;
        continue;
      }
      if (c != '-' && c != '_') break;
      ++link_end;
    }
    if (restarted) continue;  

    const bool last_ok = is_ascii_alpha(run[at + link_end - 1]) || run[at + link_end - 1] == '.';
    if (link_end < 2 || np == 0 || !last_ok) {
      offset = at + link_end;  
      continue;
    }

    const std::size_t trimmed = autolink_trim(run.substr(at, link_end));
    if (trimmed == 0) {
      offset = at + 1;
      continue;
    }

    return {left, at + trimmed, auto_mailto};
  }
}

}  
