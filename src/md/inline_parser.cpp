#include "md/inline_parser.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "md/autolink.hpp"
#include "md/entities.hpp"
#include "md/link_syntax.hpp"
#include "md/scanners.hpp"
#include "md/unicode.hpp"
#include "lumos/syntax.hpp"

namespace lumos::md {

namespace {

constexpr std::uint32_t kNone = UINT32_MAX;

constexpr bool is_ascii_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_ascii_alnum(char c) noexcept { return is_ascii_alpha(c) || (c >= '0' && c <= '9'); }

constexpr std::size_t kMaxBackticks = 1000;

bool decoded_is_whitespace(Decoded d) noexcept {
  if (d.len == 1 && d.cp == 0xFFFD) return true;
  return is_unicode_whitespace(d.cp);
}
bool decoded_is_punct(Decoded d) noexcept {
  if (d.len == 1 && d.cp == 0xFFFD) return false;
  return is_unicode_punctuation(d.cp);
}

struct Temp {
  Node node;  
  std::uint32_t parent = kNone;
  std::uint32_t first_child = kNone;
  std::uint32_t last_child = kNone;
  std::uint32_t prev = kNone;
  std::uint32_t next = kNone;
};

enum class DelimKind : std::uint8_t { star, underscore, tilde };

struct Delimiter {
  DelimKind kind = DelimKind::star;
  std::uint32_t text_node = kNone;  
  std::size_t pos = 0;              
  std::uint32_t length = 0;         
  std::uint32_t orig_length = 0;    
  bool can_open = false;
  bool can_close = false;
  std::uint32_t prev = kNone;
  std::uint32_t next = kNone;
};

struct OpenersBottom {
  explicit OpenersBottom(std::size_t bottom) noexcept {
    for (auto& a : at) for (auto& b : a) for (auto& c : b) c = bottom;
  }
  std::size_t at[3][3][2];
};

int kind_index(DelimKind k) noexcept { return k == DelimKind::star ? 0 : k == DelimKind::underscore ? 1 : 2; }

struct Bracket {
  std::uint32_t text_node = kNone;  
  std::size_t pos = 0;
  bool image = false;
  bool bracket_after = false;  
  std::uint32_t prev = kNone;
};

struct EmailPiece {
  std::uint32_t node = kNone;
  std::size_t decoded_start = 0;
  std::size_t decoded_end = 0;
  std::size_t buf_start = 0;
  NodeKind kind = NodeKind::text;
};

struct FlattenFrame {
  std::uint32_t next_sibling;
  NodeId parent_out;
};

struct ImplBuffers {
  std::vector<Temp> temp;
  std::string email_decoded;
  std::vector<EmailPiece> email_pieces;
  std::vector<Delimiter> delims;
  std::vector<Bracket> brackets;
  std::vector<std::pair<std::size_t, std::size_t>> backtick_runs;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> runs_by_length;
  std::unordered_map<std::uint32_t, std::size_t> run_cursor;
  std::vector<std::uint32_t> email_stack;
  std::vector<FlattenFrame> flatten_stack;

  void clear() noexcept {
    temp.clear();
    email_decoded.clear();
    email_pieces.clear();
    delims.clear();
    brackets.clear();
    backtick_runs.clear();
    runs_by_length.clear();
    run_cursor.clear();
    email_stack.clear();
    flatten_stack.clear();
  }
};

class Impl {
 public:
  Impl(const Features& features, ImplBuffers& b) noexcept
      : features_(features),
        temp_(b.temp),
        email_decoded_(b.email_decoded),
        email_pieces_(b.email_pieces),
        delims_(b.delims),
        brackets_(b.brackets),
        backtick_runs_(b.backtick_runs),
        runs_by_length_(b.runs_by_length),
        run_cursor_(b.run_cursor),
        email_stack_(b.email_stack),
        flatten_stack_(b.flatten_stack) {
    b.clear();
  }

  void run(const ContentBuffer& content, LabelLookup& lookup, std::vector<InlineNode>& out) {
    text_ = content.text();
    lookup_ = &lookup;
    const std::size_t n = text_.size();
    std::size_t pos = 0;
    while (pos < n) {
      switch (text_[pos]) {
        case '\\': pos = handle_backslash(pos); break;
        case '`': pos = handle_backtick(pos); break;
        case '*': pos = handle_delim(pos, DelimKind::star); break;
        case '_': pos = handle_delim(pos, DelimKind::underscore); break;
        case '~':
          pos = features_.strikethrough ? handle_delim(pos, DelimKind::tilde) : pos + 1;
          break;
        case '<': pos = handle_angle(pos); break;
        case '&': pos = handle_entity(pos); break;
        case 'w': pos = features_.extended_autolinks ? handle_www(pos) : pos + 1; break;
        case ':': pos = features_.extended_autolinks ? handle_url(pos) : pos + 1; break;
        case '[': pos = handle_open_bracket(pos); break;
        case '!': pos = handle_bang(pos); break;
        case ']': pos = handle_close_bracket(pos); break;
        case '\r':
        case '\n': pos = handle_newline(pos); break;
        default: ++pos; break;
      }
    }
    flush_text(n);
    process_emphasis(0);
    if (features_.extended_autolinks) scan_emails();
    flatten(content, out);
  }

 private:

  std::uint32_t make(NodeKind kind, std::size_t start, std::size_t end) {
    Temp t;
    t.node.kind = kind;
    t.node.start = start;
    t.node.end = end;
    temp_.push_back(t);
    return static_cast<std::uint32_t>(temp_.size() - 1);
  }

  void append_root(std::uint32_t idx) {
    temp_[idx].parent = kNone;
    temp_[idx].prev = root_last_;
    temp_[idx].next = kNone;
    if (root_last_ != kNone) temp_[root_last_].next = idx;
    else root_first_ = idx;
    root_last_ = idx;
  }

  void append_wrapped(NodeKind kind, NodeKind delim, std::size_t start, std::size_t a, std::size_t b,
                      std::size_t end, std::uint8_t variant = 0) {
    const std::uint32_t parent = make(kind, start, end);
    temp_[parent].node.variant = variant;
    const std::uint32_t kids[3] = {make(delim, start, a), make(NodeKind::text, a, b), make(delim, b, end)};
    for (std::uint32_t k : kids) {
      temp_[k].parent = parent;
      temp_[k].prev = temp_[parent].last_child;
      if (temp_[parent].last_child != kNone) temp_[temp_[parent].last_child].next = k;
      else temp_[parent].first_child = k;
      temp_[parent].last_child = k;
    }
    append_root(parent);
  }

  void flush_text(std::size_t end) {
    if (end > run_start_) append_root(make(NodeKind::text, run_start_, end));
  }

  void append_extended_autolink(std::size_t start, std::size_t end, std::uint8_t variant) {
    const std::uint32_t parent = make(NodeKind::autolink, start, end);
    temp_[parent].node.variant = variant;
    const std::uint32_t child = make(NodeKind::text, start, end);
    temp_[child].parent = parent;
    temp_[parent].first_child = child;
    temp_[parent].last_child = child;
    append_root(parent);
  }

  std::size_t handle_backslash(std::size_t pos) {
    const std::size_t next = pos + 1;
    if (next < text_.size() && is_ascii_punctuation(text_[next])) {
      flush_text(pos);
      append_root(make(NodeKind::escape, pos, next + 1));
      run_start_ = next + 1;
      return next + 1;
    }
    if (next < text_.size() && (text_[next] == '\r' || text_[next] == '\n')) {
      std::size_t end = next;
      if (text_[end] == '\r') ++end;
      if (end < text_.size() && text_[end] == '\n') ++end;
      flush_text(pos);
      append_root(make(NodeKind::hard_break, pos, end));
      run_start_ = end;
      return end;
    }
    return pos + 1;  
  }

  std::size_t handle_newline(std::size_t pos) {
    std::size_t end = pos;
    if (text_[end] == '\r') ++end;
    if (end < text_.size() && text_[end] == '\n') ++end;
    const bool hard = pos >= 2 && text_[pos - 1] == ' ' && text_[pos - 2] == ' ';
    std::size_t ws_start = pos;
    while (ws_start > run_start_ && is_cmark_space(text_[ws_start - 1])) --ws_start;
    flush_text(ws_start);
    append_root(make(hard ? NodeKind::hard_break : NodeKind::soft_break, ws_start, end));
    run_start_ = end;
    return end;
  }

  std::size_t handle_entity(std::size_t pos) {
    const std::size_t len = scan_entity(text_, pos, nullptr);
    if (len == 0) return pos + 1;
    flush_text(pos);
    append_root(make(NodeKind::entity, pos, pos + len));
    run_start_ = pos + len;
    return pos + len;
  }

  std::size_t handle_delim(std::size_t pos, DelimKind kind) {
    const char c = text_[pos];
    std::size_t end = pos;
    while (end < text_.size() && text_[end] == c) ++end;
    const auto len = static_cast<std::uint32_t>(end - pos);
    if (kind == DelimKind::tilde && len > 2) return end;  

    std::size_t before_pos = pos;
    while (before_pos > 0 && features_.strikethrough && text_[before_pos - 1] == '~') --before_pos;
    std::size_t after_pos = end;
    while (after_pos < text_.size() && features_.strikethrough && text_[after_pos] == '~') ++after_pos;

    const bool before_ws = before_pos == 0 || decoded_is_whitespace(decode_utf8_before(text_, before_pos));
    const bool before_punct = before_pos != 0 && decoded_is_punct(decode_utf8_before(text_, before_pos));
    const bool after_ws = after_pos == text_.size() || decoded_is_whitespace(decode_utf8(text_, after_pos));
    const bool after_punct = after_pos != text_.size() && decoded_is_punct(decode_utf8(text_, after_pos));
    const bool left_flanking = !after_ws && (!after_punct || before_ws || before_punct);
    const bool right_flanking = !before_ws && (!before_punct || after_ws || after_punct);

    bool can_open = left_flanking;
    bool can_close = right_flanking;
    if (kind == DelimKind::underscore) {
      can_open = left_flanking && (!right_flanking || before_punct);
      can_close = right_flanking && (!left_flanking || after_punct);
    }
    if (!can_open && !can_close) return end;

    flush_text(pos);
    const std::uint32_t node = make(NodeKind::text, pos, end);
    append_root(node);
    Delimiter d;
    d.kind = kind;
    d.text_node = node;
    d.pos = pos;
    d.length = len;
    d.orig_length = len;
    d.can_open = can_open;
    d.can_close = can_close;
    d.prev = delim_last_;
    delims_.push_back(d);
    const auto idx = static_cast<std::uint32_t>(delims_.size() - 1);
    if (delim_last_ != kNone) delims_[delim_last_].next = idx;
    else delim_first_ = idx;
    delim_last_ = idx;
    run_start_ = end;
    return end;
  }

  void remove_delim(std::uint32_t idx) {
    const std::uint32_t p = delims_[idx].prev;
    const std::uint32_t n = delims_[idx].next;
    if (p != kNone) delims_[p].next = n;
    else delim_first_ = n;
    if (n != kNone) delims_[n].prev = p;
    else delim_last_ = p;
  }

  void scan_backticks_once() {
    if (backticks_scanned_) return;
    backticks_scanned_ = true;
    const std::size_t n = text_.size();
    std::size_t i = 0;
    while (i < n) {
      if (text_[i] != '`') { ++i; continue; }
      std::size_t j = i;
      while (j < n && text_[j] == '`') ++j;
      backtick_runs_.push_back({i, j});
      const auto len = static_cast<std::uint32_t>(j - i);
      if (len <= kMaxBackticks)
        runs_by_length_[len].push_back(static_cast<std::uint32_t>(backtick_runs_.size() - 1));
      i = j;
    }
  }

  std::size_t handle_backtick(std::size_t pos) {
    scan_backticks_once();
    std::size_t end = pos;
    while (end < text_.size() && text_[end] == '`') ++end;
    const auto len = static_cast<std::uint32_t>(end - pos);
    if (len > kMaxBackticks) return end;
    std::vector<std::uint32_t>& vec = runs_by_length_[len];
    std::size_t& cursor = run_cursor_[len];
    while (cursor < vec.size() && backtick_runs_[vec[cursor]].first <= pos) ++cursor;
    if (cursor < vec.size()) {
      const auto [cs, ce] = backtick_runs_[vec[cursor]];
      ++cursor;
      flush_text(pos);
      append_wrapped(NodeKind::code_span, NodeKind::code_span_delimiter, pos, end, cs, ce);
      run_start_ = ce;
      return ce;
    }
    return end;  
  }

  std::size_t scan_autolink_uri(std::size_t p) const {
    const std::size_t n = text_.size();
    std::size_t i = p;
    if (i >= n || !is_ascii_alpha(text_[i])) return 0;
    ++i;
    std::size_t extra = 0;
    while (i < n && extra < 31 && (is_ascii_alnum(text_[i]) || text_[i] == '+' || text_[i] == '-' ||
                                   text_[i] == '.')) {
      ++i;
      ++extra;
    }
    if (extra < 1 || i >= n || text_[i] != ':') return 0;
    ++i;
    while (i < n) {
      if (text_[i] == '>') return (i + 1) - p;
      const auto c = static_cast<unsigned char>(text_[i]);
      if (c <= 0x20 || text_[i] == '<') return 0;
      ++i;
    }
    return 0;
  }

  bool is_local_char(char c) const noexcept {
    if (is_ascii_alnum(c)) return true;
    return std::string_view(".!#$%&'*+/=?^_`{|}~-").find(c) != std::string_view::npos;
  }

  std::size_t scan_label(std::size_t i) const {
    const std::size_t n = text_.size();
    if (i >= n || !is_ascii_alnum(text_[i])) return i;
    std::size_t j = i + 1;
    std::size_t last_alnum = i;
    while (j < n && (j - i) < 63 && (is_ascii_alnum(text_[j]) || text_[j] == '-')) {
      if (is_ascii_alnum(text_[j])) last_alnum = j;
      ++j;
    }
    return last_alnum + 1;
  }

  std::size_t scan_autolink_email(std::size_t p) const {
    const std::size_t n = text_.size();
    std::size_t i = p;
    if (i >= n || !is_local_char(text_[i])) return 0;
    while (i < n && is_local_char(text_[i])) ++i;
    if (i >= n || text_[i] != '@') return 0;
    ++i;
    std::size_t lab_end = scan_label(i);
    if (lab_end == i) return 0;
    i = lab_end;
    for (;;) {
      if (i < n && text_[i] == '.') {
        const std::size_t next_end = scan_label(i + 1);
        if (next_end == i + 1) break;
        i = next_end;
        continue;
      }
      break;
    }
    if (i < n && text_[i] == '>') return (i + 1) - p;
    return 0;
  }

  std::size_t scan_html_comment(std::size_t pos) {
    const std::size_t n = text_.size();
    if (pos + 4 < n && text_[pos + 4] == '>') return 5;
    if (pos + 5 < n && text_[pos + 4] == '-' && text_[pos + 5] == '>') return 6;
    if (no_comment_end_after_ != SIZE_MAX && pos >= no_comment_end_after_) return 0;
    std::size_t i = pos + 4;
    for (;;) {
      if (i >= n) {
        no_comment_end_after_ = std::min(no_comment_end_after_, pos);
        return 0;
      }
      if (text_[i] == '-') {
        if (i + 1 < n && text_[i + 1] == '-') {
          if (i + 2 < n && text_[i + 2] == '>') return (i + 3) - pos;
          if (i + 2 >= n) {
            no_comment_end_after_ = std::min(no_comment_end_after_, pos);
            return 0;
          }
          i += 3;
          continue;
        }
        ++i;
        continue;
      }
      std::size_t j = i;
      while (j < n && text_[j] != '-') ++j;
      if (j >= n) {
        no_comment_end_after_ = std::min(no_comment_end_after_, pos);
        return 0;
      }
      i = j;
    }
  }

  std::size_t scan_html_pi(std::size_t pos) {
    if (no_pi_end_after_ != SIZE_MAX && pos >= no_pi_end_after_) return 0;
    const std::size_t start = pos + 2;
    const std::size_t close = text_.find("?>", start);
    if (close == std::string_view::npos) {
      no_pi_end_after_ = std::min(no_pi_end_after_, pos);
      return 0;
    }
    return (close + 2) - pos;
  }

  std::size_t scan_html_cdata(std::size_t pos) {
    if (no_cdata_end_after_ != SIZE_MAX && pos >= no_cdata_end_after_) return 0;
    const std::size_t n = text_.size();
    constexpr std::string_view lit = "CDATA[";
    if (pos + 3 + lit.size() > n || text_.substr(pos + 3, lit.size()) != lit) return 0;
    const std::size_t start = pos + 3 + lit.size();
    const std::size_t close = text_.find("]]>", start);
    if (close == std::string_view::npos) {
      no_cdata_end_after_ = std::min(no_cdata_end_after_, pos);
      return 0;
    }
    return (close + 3) - pos;
  }

  std::size_t scan_html_declaration(std::size_t pos) {
    if (no_decl_end_after_ != SIZE_MAX && pos >= no_decl_end_after_) return 0;
    const std::size_t n = text_.size();
    if (pos + 2 >= n || !is_ascii_alpha(text_[pos + 2])) return 0;
    const std::size_t close = text_.find('>', pos + 2);
    if (close == std::string_view::npos) {
      no_decl_end_after_ = std::min(no_decl_end_after_, pos);
      return 0;
    }
    return (close + 1) - pos;
  }

  std::size_t scan_raw_html(std::size_t pos) {
    const std::size_t n = text_.size();
    const std::size_t p = pos + 1;
    if (p < n && text_[p] == '!') {
      if (p + 2 < n && text_[p + 1] == '-' && text_[p + 2] == '-') return scan_html_comment(pos);
      if (p + 1 < n && text_[p + 1] == '[') return scan_html_cdata(pos);
      return scan_html_declaration(pos);
    }
    if (p < n && text_[p] == '?') return scan_html_pi(pos);
    const std::size_t tag_len = scan_html_tag(text_.substr(p));
    return tag_len == 0 ? 0 : 1 + tag_len;
  }

  std::size_t handle_angle(std::size_t pos) {
    if (const std::size_t n = scan_autolink_uri(pos + 1)) {
      flush_text(pos);
      append_wrapped(NodeKind::autolink, NodeKind::link_delimiter, pos, pos + 1, pos + n, pos + 1 + n, 0);
      run_start_ = pos + 1 + n;
      return pos + 1 + n;
    }
    if (const std::size_t n = scan_autolink_email(pos + 1)) {
      flush_text(pos);
      append_wrapped(NodeKind::autolink, NodeKind::link_delimiter, pos, pos + 1, pos + n, pos + 1 + n, 1);
      run_start_ = pos + 1 + n;
      return pos + 1 + n;
    }
    if (const std::size_t n = scan_raw_html(pos)) {
      flush_text(pos);
      append_root(make(NodeKind::html_inline, pos, pos + n));
      run_start_ = pos + n;
      return pos + n;
    }
    return pos + 1;  
  }

  std::size_t handle_www(std::size_t pos) {
    if (bracket_last_ != kNone) return pos + 1;
    const std::size_t len = match_www(text_, pos);
    if (len == 0) return pos + 1;
    flush_text(pos);
    append_extended_autolink(pos, pos + len, autolink_variant_www);
    run_start_ = pos + len;
    return pos + len;
  }

  std::size_t handle_url(std::size_t pos) {
    if (bracket_last_ != kNone) return pos + 1;
    const AutolinkRange r = match_url(text_, pos);
    if (r.start == r.end) return pos + 1;
    flush_text(r.start);
    append_extended_autolink(r.start, r.end, autolink_variant_url);
    run_start_ = r.end;
    return r.end;
  }

  void push_bracket(bool image, std::uint32_t text_node, std::size_t pos) {
    if (bracket_last_ != kNone) brackets_[bracket_last_].bracket_after = true;
    Bracket b;
    b.text_node = text_node;
    b.image = image;
    b.pos = pos;
    b.prev = bracket_last_;
    brackets_.push_back(b);
    bracket_last_ = static_cast<std::uint32_t>(brackets_.size() - 1);
    if (!image) no_link_openers_ = false;
  }

  void pop_bracket() { bracket_last_ = brackets_[bracket_last_].prev; }

  std::size_t handle_open_bracket(std::size_t pos) {
    flush_text(pos);
    const std::uint32_t node = make(NodeKind::text, pos, pos + 1);
    append_root(node);
    run_start_ = pos + 1;
    push_bracket(false, node, pos + 1);
    return pos + 1;
  }

  std::size_t handle_bang(std::size_t pos) {
    if (pos + 1 < text_.size() && text_[pos + 1] == '[') {
      flush_text(pos);
      const std::uint32_t node = make(NodeKind::text, pos, pos + 2);
      append_root(node);
      run_start_ = pos + 2;
      push_bracket(true, node, pos + 2);
      return pos + 2;
    }
    return pos + 1;  
  }

  std::uint32_t wrap_middle_as(NodeKind kind, std::uint32_t first, std::uint32_t last) {
    const std::uint32_t wrapper = make(kind, temp_[first].node.start, temp_[last].node.end);
    temp_[wrapper].first_child = first;
    temp_[wrapper].last_child = last;
    for (std::uint32_t m = first;; m = temp_[m].next) {
      temp_[m].parent = wrapper;
      if (m == last) break;
    }
    temp_[first].prev = kNone;
    temp_[last].next = kNone;
    return wrapper;
  }

  std::uint32_t finish_wrapper(NodeKind kind, std::uint8_t variant, std::uint32_t root_prev, std::uint32_t root_next,
                               const std::vector<std::uint32_t>& kids) {
    const std::uint32_t wrapper = make(kind, temp_[kids.front()].node.start, temp_[kids.back()].node.end);
    temp_[wrapper].node.variant = variant;
    std::uint32_t prev = kNone;
    for (std::uint32_t k : kids) {
      temp_[k].parent = wrapper;
      temp_[k].prev = prev;
      if (prev != kNone) temp_[prev].next = k;
      else temp_[wrapper].first_child = k;
      prev = k;
    }
    temp_[prev].next = kNone;
    temp_[wrapper].last_child = prev;
    temp_[wrapper].parent = kNone;
    temp_[wrapper].prev = root_prev;
    temp_[wrapper].next = root_next;
    if (root_prev != kNone) temp_[root_prev].next = wrapper;
    else root_first_ = wrapper;
    if (root_next != kNone) temp_[root_next].prev = wrapper;
    else root_last_ = wrapper;
    return wrapper;
  }

  void finish_link(std::uint32_t bracket_idx, std::uint8_t variant, const std::vector<std::uint32_t>& after_closer) {
    const Bracket ob = brackets_[bracket_idx];
    const std::uint32_t ot_idx = ob.text_node;
    temp_[ot_idx].node.kind = NodeKind::link_delimiter;

    process_emphasis(ob.pos);

    const std::uint32_t closer_delim = after_closer.front();
    const std::uint32_t mid_first = temp_[ot_idx].next;
    const std::uint32_t mid_last = temp_[closer_delim].prev;
    const bool has_middle = mid_first != closer_delim;

    std::vector<std::uint32_t> kids;
    kids.reserve(after_closer.size() + 2);
    kids.push_back(ot_idx);
    if (has_middle) kids.push_back(wrap_middle_as(NodeKind::link_text, mid_first, mid_last));
    kids.insert(kids.end(), after_closer.begin(), after_closer.end());

    const std::uint32_t root_prev = temp_[ot_idx].prev;
    const NodeKind result_kind = ob.image ? NodeKind::image : NodeKind::link;
    finish_wrapper(result_kind, variant, root_prev, kNone, kids);

    pop_bracket();
    if (!ob.image) no_link_openers_ = true;
  }

  std::size_t handle_close_bracket(std::size_t pos) {
    if (bracket_last_ == kNone) return pos + 1;  
    const std::uint32_t bidx = bracket_last_;
    const Bracket& ob = brackets_[bidx];
    const bool is_image = ob.image;

    if (!is_image && no_link_openers_) {
      pop_bracket();
      return pos + 1;  
    }

    const std::size_t close_pos = pos;
    const std::size_t after = pos + 1;

    bool matched = false;
    std::uint8_t variant = 0;
    std::size_t end = after;
    bool have_dest = false, dest_pointy = false;
    std::size_t dest_lt = 0, dest_start = 0, dest_end = 0, dest_gt = 0;
    bool have_title = false;
    std::size_t title_start = 0, title_end = 0;
    std::size_t paren_open = 0, paren_close = 0;
    bool have_label = false;
    std::size_t label_start = 0, label_end = 0;

    if (after < text_.size() && text_[after] == '(') {
      const std::size_t p0 = after + 1;
      const std::size_t dstart = skip_link_whitespace(text_, p0, true);
      bool ok = false;
      std::size_t dend = dstart;
      bool pointy = false;
      if (dstart < text_.size() && text_[dstart] == ')') {
        ok = true;  
      } else if (const auto d = scan_link_destination(text_, dstart)) {
        ok = true;
        dend = d->end;
        pointy = d->pointy;
      }
      if (ok) {
        const std::size_t after_dest = skip_link_whitespace(text_, dend, true);
        std::size_t tstart = dend, tend = dend;
        if (after_dest != dend) {
          if (const auto t = scan_link_title(text_, after_dest)) {
            tstart = after_dest;
            tend = *t;
          }
        }
        const std::size_t endall = skip_link_whitespace(text_, tend, true);
        if (endall < text_.size() && text_[endall] == ')') {
          matched = true;
          variant = link_variant_inline;
          paren_open = after;
          paren_close = endall;
          end = endall + 1;
          if (dend != dstart) {
            have_dest = true;
            dest_pointy = pointy;
            if (pointy) {
              dest_lt = dstart;
              dest_gt = dend - 1;
              dest_start = dstart + 1;
              dest_end = dend - 1;
              if (dest_start == dest_end) have_dest = false;  
            } else {
              dest_start = dstart;
              dest_end = dend;
            }
          }
          if (tend != tstart) {
            have_title = true;
            title_start = tstart;
            title_end = tend;
          }
        }
      }
    }

    if (!matched) {
      bool found_label = false, label_empty = false;
      std::size_t lbl_end = after;
      if (after < text_.size() && text_[after] == '[') {
        if (const auto e = scan_link_label(text_, after, &label_empty)) {
          found_label = true;
          lbl_end = *e;
        }
      }
      std::size_t raw_start = 0, raw_end = 0;
      bool use_label = false;
      if (found_label && !label_empty) {
        raw_start = after + 1;
        raw_end = lbl_end - 1;
        use_label = true;
        variant = link_variant_full;
        have_label = true;
        label_start = after;
        label_end = lbl_end;
        end = lbl_end;
      } else if (!ob.bracket_after) {
        raw_start = ob.pos;
        raw_end = close_pos;
        use_label = true;
        variant = found_label ? link_variant_collapsed : link_variant_shortcut;
        if (found_label) {
          have_label = true;
          label_start = after;
          label_end = lbl_end;
          end = lbl_end;
        } else {
          end = after;
        }
      }
      if (use_label) matched = lookup_->is_defined(text_.substr(raw_start, raw_end - raw_start));
    }

    if (!matched) {
      pop_bracket();
      return pos + 1;  
    }

    flush_text(pos);
    std::vector<std::uint32_t> after_closer;
    after_closer.push_back(make(NodeKind::link_delimiter, pos, pos + 1));
    append_root(after_closer.back());
    if (variant == link_variant_inline) {
      after_closer.push_back(make(NodeKind::link_delimiter, paren_open, paren_open + 1));
      append_root(after_closer.back());
      if (have_dest) {
        if (dest_pointy) {
          after_closer.push_back(make(NodeKind::link_delimiter, dest_lt, dest_lt + 1));
          append_root(after_closer.back());
          after_closer.push_back(make(NodeKind::link_destination, dest_start, dest_end));
          append_root(after_closer.back());
          after_closer.push_back(make(NodeKind::link_delimiter, dest_gt, dest_gt + 1));
          append_root(after_closer.back());
        } else {
          after_closer.push_back(make(NodeKind::link_destination, dest_start, dest_end));
          append_root(after_closer.back());
        }
      } else if (dest_pointy) {
        after_closer.push_back(make(NodeKind::link_delimiter, dest_lt, dest_lt + 1));
        append_root(after_closer.back());
        after_closer.push_back(make(NodeKind::link_delimiter, dest_gt, dest_gt + 1));
        append_root(after_closer.back());
      }
      if (have_title) {
        after_closer.push_back(make(NodeKind::link_title, title_start, title_end));
        append_root(after_closer.back());
      }
      after_closer.push_back(make(NodeKind::link_delimiter, paren_close, paren_close + 1));
      append_root(after_closer.back());
    } else if (have_label) {
      after_closer.push_back(make(NodeKind::link_label, label_start, label_end));
      append_root(after_closer.back());
    }

    run_start_ = end;
    finish_link(bidx, variant, after_closer);
    return end;
  }

  std::uint32_t wrap_range(std::uint32_t ot_idx, std::uint32_t ct_idx, std::uint32_t use, NodeKind kind) {
    const auto o_full = static_cast<std::uint32_t>(temp_[ot_idx].node.end - temp_[ot_idx].node.start);
    const auto c_full = static_cast<std::uint32_t>(temp_[ct_idx].node.end - temp_[ct_idx].node.start);

    const std::uint32_t mid_first = temp_[ot_idx].next;
    const std::uint32_t mid_last = temp_[ct_idx].prev;
    const bool has_middle = mid_first != ct_idx;

    const std::uint32_t root_prev = (use == o_full) ? temp_[ot_idx].prev : ot_idx;
    const std::uint32_t root_next = (use == c_full) ? temp_[ct_idx].next : ct_idx;

    std::uint32_t open_delim;
    if (use == o_full) {
      open_delim = ot_idx;
      temp_[ot_idx].node.kind = NodeKind::emphasis_delimiter;
    } else {
      const std::size_t dend = temp_[ot_idx].node.end;
      open_delim = make(NodeKind::emphasis_delimiter, dend - use, dend);
      temp_[ot_idx].node.end = dend - use;
    }
    std::uint32_t close_delim;
    if (use == c_full) {
      close_delim = ct_idx;
      temp_[ct_idx].node.kind = NodeKind::emphasis_delimiter;
    } else {
      const std::size_t dstart = temp_[ct_idx].node.start;
      close_delim = make(NodeKind::emphasis_delimiter, dstart, dstart + use);
      temp_[ct_idx].node.start = dstart + use;
    }

    const std::uint32_t wrapper = make(kind, temp_[open_delim].node.start, temp_[close_delim].node.end);
    temp_[open_delim].parent = wrapper;
    temp_[close_delim].parent = wrapper;
    if (has_middle) {
      temp_[open_delim].next = mid_first;
      temp_[mid_first].prev = open_delim;
      for (std::uint32_t m = mid_first;; m = temp_[m].next) {
        temp_[m].parent = wrapper;
        if (m == mid_last) break;
      }
      temp_[mid_last].next = close_delim;
      temp_[close_delim].prev = mid_last;
    } else {
      temp_[open_delim].next = close_delim;
      temp_[close_delim].prev = open_delim;
    }
    temp_[open_delim].prev = kNone;
    temp_[close_delim].next = kNone;
    temp_[wrapper].first_child = open_delim;
    temp_[wrapper].last_child = close_delim;

    temp_[wrapper].parent = kNone;
    temp_[wrapper].prev = root_prev;
    temp_[wrapper].next = root_next;
    if (root_prev != kNone) temp_[root_prev].next = wrapper;
    else root_first_ = wrapper;
    if (root_next != kNone) temp_[root_next].prev = wrapper;
    else root_last_ = wrapper;
    return wrapper;
  }

  std::uint32_t insert_emph(std::uint32_t opener_idx, std::uint32_t closer_idx) {
    Delimiter& od = delims_[opener_idx];
    Delimiter& cd = delims_[closer_idx];
    const std::uint32_t ot_idx = od.text_node;
    const std::uint32_t ct_idx = cd.text_node;
    const auto o_full = static_cast<std::uint32_t>(temp_[ot_idx].node.end - temp_[ot_idx].node.start);
    const auto c_full = static_cast<std::uint32_t>(temp_[ct_idx].node.end - temp_[ct_idx].node.start);
    const std::uint32_t use = (o_full >= 2 && c_full >= 2) ? 2 : 1;

    std::uint32_t d = cd.prev;
    while (d != kNone && d != opener_idx) {
      const std::uint32_t p = delims_[d].prev;
      remove_delim(d);
      d = p;
    }

    wrap_range(ot_idx, ct_idx, use, use == 1 ? NodeKind::emphasis : NodeKind::strong);
    od.length -= use;
    cd.length -= use;

    std::uint32_t result = closer_idx;
    if (temp_[ot_idx].node.kind == NodeKind::emphasis_delimiter) remove_delim(opener_idx);
    if (temp_[ct_idx].node.kind == NodeKind::emphasis_delimiter) {
      result = cd.next;
      remove_delim(closer_idx);
    }
    return result;
  }

  std::uint32_t insert_strike(std::uint32_t opener_idx, std::uint32_t closer_idx) {
    const std::uint32_t result = delims_[closer_idx].next;
    const std::uint32_t ot_idx = delims_[opener_idx].text_node;
    const std::uint32_t ct_idx = delims_[closer_idx].text_node;
    const auto o_full = static_cast<std::uint32_t>(temp_[ot_idx].node.end - temp_[ot_idx].node.start);
    const auto c_full = static_cast<std::uint32_t>(temp_[ct_idx].node.end - temp_[ct_idx].node.start);
    if (o_full == c_full) wrap_range(ot_idx, ct_idx, o_full, NodeKind::strikethrough);
    std::uint32_t d = closer_idx;
    for (;;) {
      const std::uint32_t p = delims_[d].prev;
      remove_delim(d);
      if (d == opener_idx) break;
      d = p;
    }
    return result;
  }

  void process_emphasis(std::size_t stack_bottom) {
    std::uint32_t closer = kNone;
    {
      std::uint32_t candidate = delim_last_;
      while (candidate != kNone && delims_[candidate].pos >= stack_bottom) {
        closer = candidate;
        candidate = delims_[candidate].prev;
      }
    }
    OpenersBottom bottom(stack_bottom);
    while (closer != kNone) {
      if (!delims_[closer].can_close) {
        closer = delims_[closer].next;
        continue;
      }
      const DelimKind kind = delims_[closer].kind;
      const int kidx = kind_index(kind);
      const int mod3 = static_cast<int>(delims_[closer].orig_length % 3);
      const bool closer_can_open = delims_[closer].can_open;
      const int coidx = kind == DelimKind::tilde ? 0 : (closer_can_open ? 1 : 0);
      const std::size_t floor = bottom.at[kidx][mod3][coidx];
      const std::size_t closer_pos = delims_[closer].pos;
      const std::uint32_t cc_len = delims_[closer].orig_length;

      std::uint32_t opener = delims_[closer].prev;
      bool found = false;
      while (opener != kNone && delims_[opener].pos >= floor) {
        if (delims_[opener].can_open && delims_[opener].kind == kind) {
          const bool ambivalent = closer_can_open || delims_[opener].can_close;
          const std::uint32_t co_len = delims_[opener].orig_length;
          if (!ambivalent || cc_len % 3 == 0 || (co_len + cc_len) % 3 != 0) {
            found = true;
            break;
          }
        }
        opener = delims_[opener].prev;
      }

      const std::uint32_t old_closer = closer;
      if (found) {
        closer = (kind == DelimKind::tilde) ? insert_strike(opener, closer) : insert_emph(opener, closer);
      } else {
        const std::uint32_t next_closer = delims_[old_closer].next;
        bottom.at[kidx][mod3][coidx] = closer_pos;
        if (!delims_[old_closer].can_open) remove_delim(old_closer);
        closer = next_closer;
      }
    }
    while (delim_last_ != kNone && delims_[delim_last_].pos >= stack_bottom) remove_delim(delim_last_);
  }

  static bool is_email_run_kind(NodeKind k) noexcept {
    return k == NodeKind::text || k == NodeKind::escape || k == NodeKind::entity;
  }

  static bool is_email_descend_kind(NodeKind k) noexcept {
    return k == NodeKind::emphasis || k == NodeKind::strong || k == NodeKind::strikethrough ||
           k == NodeKind::image || k == NodeKind::link_text;
  }

  void build_email_run(std::uint32_t first, std::uint32_t last, std::string& decoded,
                       std::vector<EmailPiece>& pieces) {
    decoded.clear();
    pieces.clear();
    for (std::uint32_t cur = first;; cur = temp_[cur].next) {
      const Node& k = temp_[cur].node;
      const std::size_t dstart = decoded.size();
      if (k.kind == NodeKind::text) {
        decoded.append(text_.substr(k.start, k.end - k.start));
      } else if (k.kind == NodeKind::escape) {
        decoded += text_[k.start + 1];  
      } else {
        scan_entity(text_.substr(k.start, k.end - k.start), 0, &decoded);
      }
      pieces.push_back({cur, dstart, decoded.size(), k.start, k.kind});
      if (cur == last) break;
    }
  }

  std::uint32_t split_text_at(std::uint32_t t_idx, std::size_t x) {
    const std::size_t end = temp_[t_idx].node.end;
    temp_[t_idx].node.end = x;
    const std::uint32_t new_idx = make(NodeKind::text, x, end);
    temp_[new_idx].parent = temp_[t_idx].parent;
    temp_[new_idx].prev = t_idx;
    temp_[new_idx].next = temp_[t_idx].next;
    if (temp_[t_idx].next != kNone) temp_[temp_[t_idx].next].prev = new_idx;
    else if (temp_[t_idx].parent != kNone) temp_[temp_[t_idx].parent].last_child = new_idx;
    else root_last_ = new_idx;
    temp_[t_idx].next = new_idx;
    return new_idx;
  }

  std::uint32_t resolve_email_split(std::vector<EmailPiece>& pieces, std::size_t& cursor, std::size_t pos,
                                    bool at_start) {
    if (at_start) {
      while (cursor + 1 < pieces.size() && pieces[cursor].decoded_end <= pos) ++cursor;
    } else {
      while (cursor + 1 < pieces.size() && pieces[cursor].decoded_end < pos) ++cursor;
    }
    EmailPiece& p = pieces[cursor];
    if (at_start ? (pos == p.decoded_start) : (pos == p.decoded_end)) return p.node;
    if (p.kind != NodeKind::text) return kNone;
    const std::size_t buf_pos = p.buf_start + (pos - p.decoded_start);
    const std::uint32_t new_idx = split_text_at(p.node, buf_pos);
    const std::uint32_t result = at_start ? new_idx : p.node;
    p.node = new_idx;
    p.buf_start = buf_pos;
    p.decoded_start = pos;
    return result;
  }

  std::uint32_t wrap_sibling_range(std::uint32_t first_idx, std::uint32_t last_idx, NodeKind kind,
                                   std::uint8_t variant) {
    const std::uint32_t parent = temp_[first_idx].parent;
    const std::uint32_t prev = temp_[first_idx].prev;
    const std::uint32_t next = temp_[last_idx].next;

    const std::uint32_t wrapper = make(kind, temp_[first_idx].node.start, temp_[last_idx].node.end);
    temp_[wrapper].node.variant = variant;
    temp_[wrapper].first_child = first_idx;
    temp_[wrapper].last_child = last_idx;
    for (std::uint32_t m = first_idx;; m = temp_[m].next) {
      temp_[m].parent = wrapper;
      if (m == last_idx) break;
    }
    temp_[first_idx].prev = kNone;
    temp_[last_idx].next = kNone;

    temp_[wrapper].parent = parent;
    temp_[wrapper].prev = prev;
    temp_[wrapper].next = next;
    if (prev != kNone) temp_[prev].next = wrapper;
    else if (parent != kNone) temp_[parent].first_child = wrapper;
    else root_first_ = wrapper;
    if (next != kNone) temp_[next].prev = wrapper;
    else if (parent != kNone) temp_[parent].last_child = wrapper;
    else root_last_ = wrapper;
    return wrapper;
  }

  void scan_email_run(std::uint32_t first, std::uint32_t last) {
    const std::size_t raw_start = temp_[first].node.start;
    bool maybe = text_.substr(raw_start, temp_[last].node.end - raw_start).find('@') != std::string_view::npos;
    for (std::uint32_t cur = first; !maybe; cur = temp_[cur].next) {
      maybe = temp_[cur].node.kind == NodeKind::entity;
      if (cur == last) break;
    }
    if (!maybe) return;
    std::string& decoded = email_decoded_;
    std::vector<EmailPiece>& pieces = email_pieces_;
    build_email_run(first, last, decoded, pieces);
    std::size_t cursor = 0;
    std::size_t from = 0;
    for (;;) {
      const EmailMatch m = find_email(decoded, from);
      if (m.start == m.end) break;
      const std::uint32_t node_lo = resolve_email_split(pieces, cursor, m.start, true);
      const std::uint32_t node_hi = resolve_email_split(pieces, cursor, m.end, false);
      from = m.end;
      if (node_lo == kNone || node_hi == kNone) continue;  
      const std::uint8_t variant =
          m.mailto ? autolink_variant_extended_email : autolink_variant_extended_email_plain;
      wrap_sibling_range(node_lo, node_hi, NodeKind::autolink, variant);
    }
  }

  void scan_emails() {
    std::vector<std::uint32_t>& stack = email_stack_;
    std::uint32_t cur = root_first_;
    while (true) {
      if (cur == kNone) {
        if (stack.empty()) break;
        cur = stack.back();
        stack.pop_back();
        continue;
      }
      const NodeKind k = temp_[cur].node.kind;
      if (is_email_run_kind(k)) {
        std::uint32_t run_last = cur;
        while (temp_[run_last].next != kNone && is_email_run_kind(temp_[temp_[run_last].next].node.kind)) {
          run_last = temp_[run_last].next;
        }
        const std::uint32_t after = temp_[run_last].next;
        scan_email_run(cur, run_last);
        cur = after;
        continue;
      }
      if (is_email_descend_kind(k)) {
        const std::uint32_t next_sib = temp_[cur].next;
        stack.push_back(next_sib);
        cur = temp_[cur].first_child;
        continue;
      }
      cur = temp_[cur].next;
    }
  }

  void flatten(const ContentBuffer& content, std::vector<InlineNode>& out) {
    out.clear();
    std::vector<FlattenFrame>& stack = flatten_stack_;
    std::uint32_t cur = root_first_;
    NodeId parent_out = no_node;
    std::size_t seg = 0;
    while (true) {
      if (cur != kNone) {
        if (temp_[cur].node.kind == NodeKind::text) {
          std::uint32_t nx = temp_[cur].next;
          while (nx != kNone && temp_[nx].node.kind == NodeKind::text &&
                 temp_[nx].node.start == temp_[cur].node.end) {
            temp_[cur].node.end = temp_[nx].node.end;
            nx = temp_[nx].next;
          }
          temp_[cur].next = nx;
        }
        InlineNode on;
        on.node = temp_[cur].node;
        on.node.start = content.source_start(temp_[cur].node.start, seg);
        std::size_t end_seg = seg;
        on.node.end = content.source_end(temp_[cur].node.end, end_seg);
        on.node.parent = parent_out;
        out.push_back(on);
        const auto my_out_index = static_cast<NodeId>(out.size() - 1);
        const std::uint32_t first_child = temp_[cur].first_child;
        const std::uint32_t next_sib = temp_[cur].next;
        if (first_child != kNone) {
          stack.push_back({next_sib, parent_out});
          cur = first_child;
          parent_out = my_out_index;
        } else {
          cur = next_sib;
        }
      } else {
        if (stack.empty()) break;
        cur = stack.back().next_sibling;
        parent_out = stack.back().parent_out;
        stack.pop_back();
      }
    }
  }

  Features features_;
  std::string_view text_;
  std::size_t run_start_ = 0;

  std::vector<Temp>& temp_;
  std::string& email_decoded_;
  std::vector<EmailPiece>& email_pieces_;
  std::uint32_t root_first_ = kNone;
  std::uint32_t root_last_ = kNone;

  std::vector<Delimiter>& delims_;
  std::uint32_t delim_first_ = kNone;
  std::uint32_t delim_last_ = kNone;

  std::vector<Bracket>& brackets_;
  std::uint32_t bracket_last_ = kNone;
  bool no_link_openers_ = false;
  LabelLookup* lookup_ = nullptr;

  bool backticks_scanned_ = false;
  std::vector<std::pair<std::size_t, std::size_t>>& backtick_runs_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& runs_by_length_;
  std::unordered_map<std::uint32_t, std::size_t>& run_cursor_;
  std::vector<std::uint32_t>& email_stack_;
  std::vector<FlattenFrame>& flatten_stack_;

  std::size_t no_comment_end_after_ = SIZE_MAX;
  std::size_t no_pi_end_after_ = SIZE_MAX;
  std::size_t no_cdata_end_after_ = SIZE_MAX;
  std::size_t no_decl_end_after_ = SIZE_MAX;
};

}  

struct InlineParser::Buffers : ImplBuffers {};

InlineParser::InlineParser(const Features& features) : features_(features), buffers_(std::make_unique<Buffers>()) {}

InlineParser::~InlineParser() = default;

void InlineParser::parse(const ContentBuffer& content, LabelLookup& lookup, std::vector<InlineNode>& out) {
  Impl(features_, *buffers_).run(content, lookup, out);
}

}  
