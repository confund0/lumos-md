#include "lumos/buffer.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "lumos/log.hpp"
#include "wipe.hpp"

namespace lumos {
namespace {

constexpr std::string_view kComp = "buffer";

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

bool valid_utf8(std::string_view s) {
  const std::size_t n = s.size();
  std::size_t i = 0;
  auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
  auto in = [](unsigned char c, unsigned lo, unsigned hi) { return c >= lo && c <= hi; };
  while (i < n) {
    const unsigned char c = byte(i);
    if (c < 0x80U) {
      ++i;
      continue;
    }
    std::size_t need = 0;
    unsigned lo = 0x80U;
    unsigned hi = 0xBFU;
    if (in(c, 0xC2U, 0xDFU)) {
      need = 1;
    } else if (c == 0xE0U) {
      need = 2;
      lo = 0xA0U;
    } else if (in(c, 0xE1U, 0xECU) || in(c, 0xEEU, 0xEFU)) {
      need = 2;
    } else if (c == 0xEDU) {
      need = 2;
      hi = 0x9FU;
    } else if (c == 0xF0U) {
      need = 3;
      lo = 0x90U;
    } else if (in(c, 0xF1U, 0xF3U)) {
      need = 3;
    } else if (c == 0xF4U) {
      need = 3;
      hi = 0x8FU;
    } else {
      return false;
    }
    if (n - i - 1 < need) return false;
    if (!in(byte(i + 1), lo, hi)) return false;
    for (std::size_t k = 2; k <= need; ++k) {
      if (!in(byte(i + k), 0x80U, 0xBFU)) return false;
    }
    i += need + 1;
  }
  return true;
}

std::string num(std::size_t v) { return std::to_string(v); }

constexpr std::size_t kBlock = 1024;  
constexpr std::size_t kCompactMin = std::size_t{1} << 20;  

std::size_t count_newlines(std::string_view s) {
  return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n'));
}

}  

void Buffer::Store::append(std::string_view s) {
  bytes += s;
  while (rank.size() * kBlock <= bytes.size()) {
    const std::size_t from = (rank.size() - 1) * kBlock;
    rank.push_back(rank.back() + count_newlines(std::string_view(bytes).substr(from, kBlock)));
  }
}

void Buffer::Store::rebuild() {
  rank.assign(1, 0);
  while (rank.size() * kBlock <= bytes.size()) {
    const std::size_t from = (rank.size() - 1) * kBlock;
    rank.push_back(rank.back() + count_newlines(std::string_view(bytes).substr(from, kBlock)));
  }
}

std::size_t Buffer::Store::newlines_before(std::size_t pos) const {
  const std::size_t b = pos / kBlock;
  return rank[b] + count_newlines(std::string_view(bytes).substr(b * kBlock, pos - b * kBlock));
}

std::size_t Buffer::Store::find_newline(std::size_t pos, std::size_t k) const {
  const std::size_t before = newlines_before(pos);
  const std::size_t target = before + k;
  const auto it = std::upper_bound(rank.begin(), rank.end(), target);
  const std::size_t b = static_cast<std::size_t>(it - rank.begin()) - 1;
  std::size_t at = std::max(pos, b * kBlock);
  std::size_t seen = at == pos ? before : rank[b];
  const std::string_view v(bytes);
  for (;;) {
    at = v.find('\n', at);
    if (at == std::string_view::npos) return bytes.size();  
    if (seen == target) return at;
    ++seen;
    ++at;
  }
}

Buffer::Piece Buffer::make_piece(std::uint8_t store, std::size_t start, std::size_t length) const {
  const Store& s = stores_[store];
  return Piece{store, start, length, s.newlines_before(start + length) - s.newlines_before(start)};
}

Buffer::Index Buffer::make_node(const Piece& p) {
  seed_ ^= seed_ << 13U;
  seed_ ^= seed_ >> 17U;
  seed_ ^= seed_ << 5U;
  TreeNode n;
  n.piece = p;
  n.priority = seed_;
  n.bytes = p.length;
  n.newlines = p.newlines;
  if (!free_.empty()) {
    const Index i = free_.back();
    free_.pop_back();
    nodes_[i] = n;
    return i;
  }
  nodes_.push_back(n);
  return static_cast<Index>(nodes_.size() - 1);
}

void Buffer::free_tree(Index t) {
  std::vector<Index> stack;
  if (t != nil) stack.push_back(t);
  while (!stack.empty()) {
    const Index i = stack.back();
    stack.pop_back();
    if (nodes_[i].left != nil) stack.push_back(nodes_[i].left);
    if (nodes_[i].right != nil) stack.push_back(nodes_[i].right);
    free_.push_back(i);
  }
}

void Buffer::update(Index t) {
  TreeNode& n = nodes_[t];
  n.bytes = bytes_of(n.left) + n.piece.length + bytes_of(n.right);
  n.newlines = newlines_of(n.left) + n.piece.newlines + newlines_of(n.right);
}

void Buffer::split(Index t, std::size_t offset, Index& left, Index& right) {
  if (t == nil) {
    left = nil;
    right = nil;
    return;
  }
  const std::size_t lb = bytes_of(nodes_[t].left);
  const std::size_t len = nodes_[t].piece.length;
  if (offset <= lb) {
    Index l = nil;
    Index r = nil;
    split(nodes_[t].left, offset, l, r);
    nodes_[t].left = r;
    update(t);
    left = l;
    right = t;
  } else if (offset >= lb + len) {
    Index l = nil;
    Index r = nil;
    split(nodes_[t].right, offset - lb - len, l, r);
    nodes_[t].right = l;
    update(t);
    left = t;
    right = r;
  } else {
    const std::size_t k = offset - lb;
    const Piece p = nodes_[t].piece;
    const Index tail = make_node(make_piece(p.store, p.start + k, p.length - k));
    nodes_[tail].priority = nodes_[t].priority;
    nodes_[tail].right = nodes_[t].right;
    nodes_[t].right = nil;
    nodes_[t].piece = Piece{p.store, p.start, k, p.newlines - nodes_[tail].piece.newlines};
    update(tail);
    update(t);
    left = t;
    right = tail;
  }
}

Buffer::Index Buffer::merge(Index left, Index right) {
  if (left == nil) return right;
  if (right == nil) return left;
  if (nodes_[left].priority > nodes_[right].priority) {
    const Index m = merge(nodes_[left].right, right);
    nodes_[left].right = m;
    update(left);
    return left;
  }
  const Index m = merge(left, nodes_[right].left);
  nodes_[right].left = m;
  update(right);
  return right;
}

bool Buffer::extend_last(Index t, std::size_t length, std::size_t newlines) {
  if (t == nil) return false;
  bool done = false;
  if (nodes_[t].right != nil) {
    done = extend_last(nodes_[t].right, length, newlines);
  } else {
    Piece& p = nodes_[t].piece;
    if (p.store == 1 && p.start + p.length == stores_[1].bytes.size()) {
      p.length += length;
      p.newlines += newlines;
      done = true;
    }
  }
  if (done) update(t);
  return done;
}

void Buffer::build(std::size_t length) {
  nodes_.clear();
  free_.clear();
  root_ = nil;
  if (length == 0) return;
  const std::uint8_t store = stores_[1].bytes.empty() ? 0 : 1;
  root_ = make_node(make_piece(store, 0, length));
}

template <typename Fn>
void Buffer::for_each_from(std::size_t offset, Fn fn) const {
  std::vector<Index> stack;
  Index t = root_;
  while (t != nil) {
    const TreeNode& n = nodes_[t];
    const std::size_t lb = bytes_of(n.left);
    if (offset < lb) {
      stack.push_back(t);
      t = n.left;
    } else if (offset < lb + n.piece.length) {
      if (!fn(piece_view(n.piece).substr(offset - lb))) return;
      t = n.right;
      break;
    } else {
      offset -= lb + n.piece.length;
      t = n.right;
    }
  }
  while (t != nil || !stack.empty()) {
    while (t != nil) {
      stack.push_back(t);
      t = nodes_[t].left;
    }
    t = stack.back();
    stack.pop_back();
    if (!fn(piece_view(nodes_[t].piece))) return;
    t = nodes_[t].right;
  }
}

std::size_t Buffer::piece_count() const { return nodes_.size() - free_.size(); }

Buffer::Buffer(std::string text) {
  stores_[0].bytes = std::move(text);
  stores_[0].rebuild();
  size_ = stores_[0].bytes.size();
  build(size_);
  log(LogLevel::debug, kComp, "load",
      {{"bytes", num(size_)}, {"utf8", valid_utf8(stores_[0].bytes) ? "valid" : "invalid"}});
}

std::size_t Buffer::size() const { return size_; }

std::string_view Buffer::piece_view(const Piece& p) const {
  return std::string_view(stores_[p.store].bytes).substr(p.start, p.length);
}

std::string Buffer::text() const {
  std::string out;
  out.reserve(size_);
  for_each_from(0, [&](std::string_view v) {
    out += v;
    return true;
  });
  return out;
}

std::string Buffer::slice(std::size_t offset, std::size_t length) const {
  if (offset > size_ || length > size_ - offset) {
    log(LogLevel::debug, kComp, "slice_out_of_range",
        {{"offset", num(offset)}, {"length", num(length)}, {"size", num(size_)}});
    throw std::out_of_range("Buffer::slice: range past end");
  }
  std::string out;
  out.reserve(length);
  if (length == 0) return out;
  for_each_from(offset, [&](std::string_view v) {
    out += v.substr(0, length - out.size());
    return out.size() < length;
  });
  return out;
}

char Buffer::at(std::size_t offset) const {
  if (offset >= size_) {
    log(LogLevel::debug, kComp, "at_out_of_range", {{"offset", num(offset)}, {"size", num(size_)}});
    throw std::out_of_range("Buffer::at: offset past end");
  }
  return chunk_at(offset).front();
}

std::string_view Buffer::chunk_at(std::size_t offset) const {
  if (offset > size_) {
    log(LogLevel::debug, kComp, "chunk_out_of_range",
        {{"offset", num(offset)}, {"size", num(size_)}});
    throw std::out_of_range("Buffer::chunk_at: offset past end");
  }
  Index t = root_;
  while (t != nil) {
    const TreeNode& n = nodes_[t];
    const std::size_t lb = bytes_of(n.left);
    if (offset < lb) {
      t = n.left;
    } else if (offset < lb + n.piece.length) {
      return piece_view(n.piece).substr(offset - lb);
    } else {
      offset -= lb + n.piece.length;
      t = n.right;
    }
  }
  return {};
}

void Buffer::validate(const Edit& e) const {
  if (e.offset > size_ || e.removed > size_ - e.offset) {
    log(LogLevel::debug, kComp, "edit_out_of_range",
        {{"offset", num(e.offset)}, {"removed", num(e.removed)}, {"size", num(size_)}});
    throw std::out_of_range("Buffer::apply: range past end");
  }
  const std::size_t end = e.offset + e.removed;
  for (const std::size_t at_pos : {e.offset, end}) {
    if (at_pos < size_ && is_continuation(at(at_pos))) {
      log(LogLevel::debug, kComp, "edit_splits_utf8",
          {{"offset", num(e.offset)}, {"removed", num(e.removed)}, {"at", num(at_pos)}});
      throw std::invalid_argument("Buffer::apply: range splits a UTF-8 code point");
    }
  }
  if (!valid_utf8(e.inserted)) {
    log(LogLevel::debug, kComp, "edit_invalid_utf8",
        {{"offset", num(e.offset)}, {"inserted_bytes", num(e.inserted.size())}});
    throw std::invalid_argument("Buffer::apply: inserted text is not valid UTF-8");
  }
}

Edit Buffer::apply_raw(const Edit& e) {
  Edit inverse{e.offset, e.inserted.size(), slice(e.offset, e.removed)};
  Index left = nil;
  Index rest = nil;
  Index mid = nil;
  Index right = nil;
  split(root_, e.offset, left, rest);
  split(rest, e.removed, mid, right);
  free_tree(mid);
  if (!e.inserted.empty()) {
    Store& add = stores_[1];
    const std::size_t newlines = count_newlines(e.inserted);
    if (!extend_last(left, e.inserted.size(), newlines)) {
      left = merge(left, make_node(Piece{1, add.bytes.size(), e.inserted.size(), newlines}));
    }
    add.append(e.inserted);
  }
  root_ = merge(left, right);
  size_ = size_ - e.removed + e.inserted.size();
  maybe_compact();
  return inverse;
}

void Buffer::maybe_compact() {
  const std::size_t dead = store_bytes() - size_;
  if (dead <= kCompactMin || dead <= size_) return;
  log(LogLevel::debug, kComp, "compact",
      {{"live", num(size_)}, {"dead", num(dead)}, {"pieces", num(piece_count())}});
  Store fresh;
  fresh.bytes = text();
  fresh.rebuild();
  stores_[0] = Store{};
  stores_[1] = std::move(fresh);
  build(size_);
}

void Buffer::bump_version() { ++version_; }

void Buffer::push_step(Step step) {
  undo_.push_back(std::move(step));
  if (undo_.size() > max_undo_) {
    log(LogLevel::debug, kComp, "undo_dropped",
        {{"steps", num(undo_.size() - max_undo_)}, {"max", num(max_undo_)}});
    while (undo_.size() > max_undo_) undo_.pop_front();
  }
}

void Buffer::set_max_undo(std::size_t steps) {
  max_undo_ = steps;
  if (undo_.size() > max_undo_) {
    log(LogLevel::debug, kComp, "undo_dropped",
        {{"steps", num(undo_.size() - max_undo_)}, {"max", num(max_undo_)}});
    while (undo_.size() > max_undo_) undo_.pop_front();
  }
}

void Buffer::wipe() {
  if (depth_ > 0) {
    log(LogLevel::debug, kComp, "wipe_in_transaction");
    throw std::logic_error("Buffer::wipe: transaction is open");
  }
  const auto wipe_step = [](Step& step) {
    for (Edit& e : step.forward) wipe_string(e.inserted);
    for (Edit& e : step.inverse) wipe_string(e.inserted);
  };
  for (Store& s : stores_) wipe_string(s.bytes);
  for (Step& step : undo_) wipe_step(step);
  for (Step& step : redo_) wipe_step(step);
  wipe_step(open_);
  log(LogLevel::debug, kComp, "wipe", {{"undo", num(undo_.size())}, {"redo", num(redo_.size())}});
  const std::size_t max_undo = max_undo_;
  const std::uint64_t version = version_;
  *this = Buffer();
  max_undo_ = max_undo;
  version_ = version;
  bump_version();
}

Edit Buffer::apply(const Edit& e) {
  validate(e);
  if (e.removed == 0 && e.inserted.empty()) {
    log(LogLevel::debug, kComp, "edit_empty_skipped", {{"offset", num(e.offset)}});
    return Edit{e.offset, 0, {}};
  }
  Edit inverse = apply_raw(e);
  bump_version();
  if (!redo_.empty()) {
    log(LogLevel::debug, kComp, "redo_cleared", {{"steps", num(redo_.size())}});
    redo_.clear();
  }
  if (depth_ > 0) {
    open_.forward.push_back(e);
    open_.inverse.push_back(inverse);
  } else {
    push_step(Step{{e}, {inverse}});
  }
  return inverse;
}

void Buffer::begin() { ++depth_; }

void Buffer::commit() {
  if (depth_ == 0) {
    log(LogLevel::debug, kComp, "commit_without_begin");
    throw std::logic_error("Buffer::commit: no open transaction");
  }
  if (--depth_ > 0) return;
  if (open_.forward.empty()) {
    log(LogLevel::debug, kComp, "commit_empty_skipped");
    return;
  }
  log(LogLevel::debug, kComp, "commit", {{"edits", num(open_.forward.size())}});
  push_step(std::move(open_));
  open_ = Step{};
}

bool Buffer::in_transaction() const { return depth_ > 0; }

std::vector<Edit> Buffer::undo() {
  if (depth_ > 0) {
    log(LogLevel::debug, kComp, "undo_in_transaction");
    throw std::logic_error("Buffer::undo: transaction is open");
  }
  if (undo_.empty()) return {};
  Step step = std::move(undo_.back());
  undo_.pop_back();
  std::vector<Edit> applied;
  applied.reserve(step.inverse.size());
  for (auto it = step.inverse.rbegin(); it != step.inverse.rend(); ++it) {
    apply_raw(*it);
    applied.push_back(*it);
  }
  bump_version();
  redo_.push_back(std::move(step));
  return applied;
}

std::vector<Edit> Buffer::redo() {
  if (depth_ > 0) {
    log(LogLevel::debug, kComp, "redo_in_transaction");
    throw std::logic_error("Buffer::redo: transaction is open");
  }
  if (redo_.empty()) return {};
  Step step = std::move(redo_.back());
  redo_.pop_back();
  for (const Edit& e : step.forward) apply_raw(e);
  bump_version();
  std::vector<Edit> applied = step.forward;
  push_step(std::move(step));
  return applied;
}

bool Buffer::can_undo() const { return depth_ == 0 && !undo_.empty(); }

bool Buffer::can_redo() const { return depth_ == 0 && !redo_.empty(); }

std::uint64_t Buffer::version() const { return version_; }

std::size_t Buffer::line_count() const { return newlines_of(root_) + 1; }

std::size_t Buffer::line_start(std::size_t line) const {
  if (line == 0) return 0;
  if (line >= line_count()) {
    log(LogLevel::debug, kComp, "line_out_of_range", {{"line", num(line)}, {"lines", num(line_count())}});
    throw std::out_of_range("Buffer::line_start: line past end");
  }
  std::size_t k = line;
  std::size_t base = 0;
  Index t = root_;
  while (t != nil) {
    const TreeNode& n = nodes_[t];
    const std::size_t ln = newlines_of(n.left);
    if (k <= ln) {
      t = n.left;
      continue;
    }
    k -= ln;
    const Piece& p = n.piece;
    if (k <= p.newlines) {
      const std::size_t pos = stores_[p.store].find_newline(p.start, k - 1);
      return base + bytes_of(n.left) + (pos - p.start) + 1;
    }
    k -= p.newlines;
    base += bytes_of(n.left) + p.length;
    t = n.right;
  }
  return size_;  
}

std::size_t Buffer::line_of(std::size_t offset) const {
  if (offset > size_) {
    log(LogLevel::debug, kComp, "line_of_out_of_range",
        {{"offset", num(offset)}, {"size", num(size_)}});
    throw std::out_of_range("Buffer::line_of: offset past end");
  }
  std::size_t line = 0;
  Index t = root_;
  while (t != nil) {
    const TreeNode& n = nodes_[t];
    const std::size_t lb = bytes_of(n.left);
    if (offset < lb) {
      t = n.left;
      continue;
    }
    line += newlines_of(n.left);
    const std::size_t local = offset - lb;
    const Piece& p = n.piece;
    if (local < p.length) {
      const Store& s = stores_[p.store];
      return line + s.newlines_before(p.start + local) - s.newlines_before(p.start);
    }
    line += p.newlines;
    offset = local - p.length;
    t = n.right;
  }
  return line;
}

}  
