#include "lumos/qt/source_editor.hpp"

#include <QClipboard>
#include <QDrag>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextBoundaryFinder>
#include <QTextLine>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "lumos/commands.hpp"
#include "lumos/log.hpp"

namespace lumos::qt {
namespace {

constexpr std::string_view kComp = "qt";

std::string num(std::size_t v) { return std::to_string(v); }

bool debug_on() { return log_enabled(LogLevel::debug); }

bool printable(const QString& s) {
  if (s.isEmpty()) return false;
  for (char32_t c : s.toUcs4()) {
    if (QChar::category(c) == QChar::Other_Control) return false;
  }
  return true;
}

bool is_space_text(std::string_view s) {
  return !s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r');
}

std::size_t transform(std::size_t pos, const Edit& e) {
  if (pos <= e.offset) return pos;
  if (pos >= e.offset + e.removed) return pos - e.removed + e.inserted.size();
  return e.offset;
}

std::vector<std::pair<std::size_t, std::size_t>> touched_ranges(const Change& change) {
  std::vector<std::pair<std::size_t, std::size_t>> out;
  const auto map_through = [](std::pair<std::size_t, std::size_t>& r, const TextRegion& g) {
    auto move = [&g](std::size_t pos, bool end) {
      if (pos < g.start || (pos == g.start && !end)) return pos;
      if (pos >= g.old_end) return pos - g.old_end + g.new_end;
      return end ? g.new_end : g.start;
    };
    r.first = move(r.first, false);
    r.second = move(r.second, true);
  };
  const auto regions = change.tree_changes;
  for (std::size_t i = 0; i < regions.size(); ++i) {
    std::pair<std::size_t, std::size_t> r{regions[i].start, regions[i].new_end};
    for (std::size_t j = i + 1; j < regions.size(); ++j) map_through(r, regions[j]);
    out.push_back(r);
  }
  if (!change.edits.empty()) {
    const TextRegion e = merge_edits(change.edits);
    out.emplace_back(e.start, e.new_end);
  }
  return out;
}

QList<QTextLayout::FormatRange> shift_formats(const QList<QTextLayout::FormatRange>& in, int at, int len) {
  QList<QTextLayout::FormatRange> out;
  for (QTextLayout::FormatRange r : in) {
    const int end = r.start + r.length;
    if (end <= at) {
      out.append(r);
    } else if (r.start >= at) {
      r.start += len;
      out.append(r);
    } else {
      QTextLayout::FormatRange head = r;
      head.length = at - r.start;
      out.append(head);
      r.start = at + len;
      r.length = end - at;
      out.append(r);
    }
  }
  return out;
}

qreal average_char_width(const QFontMetricsF& fm) {
  const qreal avg = fm.averageCharWidth();
  return avg > 0 && avg <= fm.maxWidth() ? avg : fm.horizontalAdvance(QLatin1Char('0'));
}

}  

SourceEditor::SourceEditor(QWidget* parent) : SourceEditor(Dialect{}, parent) {}

SourceEditor::SourceEditor(const Dialect& dialect, QWidget* parent)
    : QAbstractScrollArea(parent), doc_(dialect), styles_(StyleTable::make_default(font(), palette())) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setCursor(Qt::IBeamCursor);
  setAcceptDrops(true);
  viewport()->setAcceptDrops(true);
  doc_.add_listener([this](const Change& c) { on_change(c); });
  rebuild_lines();
  restyle();
}

SourceEditor::~SourceEditor() = default;

void SourceEditor::set_text(std::string text, bool keep_position) {
  close_group();
  clear_preedit();
  const std::size_t nl = text.find('\n');
  newline_ = (nl != std::string::npos && nl > 0 && text[nl - 1] == '\r') ? "\r\n" : "\n";
  if (debug_on())
    log(LogLevel::debug, kComp, "load",
        {{"bytes", num(text.size())}, {"newline", newline_ == "\n" ? "lf" : "crlf"},
         {"keep_position", keep_position ? "1" : "0"}});
  const int scroll = keep_position ? scroll_y() : 0;
  if (!keep_position) {
    cursor_ = 0;
    anchor_ = 0;
  }
  images_.clear();
  pending_image_requests_.clear();
  doc_.load(std::move(text));
  desired_x_ = -1;
  ensure_layout();
  verticalScrollBar()->setValue(scroll);
  emit cursorPositionChanged();
}

void SourceEditor::wipe() {
  close_group();
  clear_preedit();
  cursor_ = 0;
  anchor_ = 0;
  doc_.wipe();
  desired_x_ = -1;
  ensure_layout();
  verticalScrollBar()->setValue(0);
  emit cursorPositionChanged();
}

void SourceEditor::replace_text(std::string_view text) {
  close_group();
  clear_preedit();
  const std::size_t old_size = doc_.size();
  const std::string old = doc_.text();
  const std::size_t max = std::min(old_size, text.size());
  std::size_t prefix = 0;
  while (prefix < max && old[prefix] == text[prefix]) ++prefix;
  std::size_t suffix = 0;
  while (suffix < max - prefix && old[old_size - 1 - suffix] == text[text.size() - 1 - suffix]) ++suffix;
  const auto continuation = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; };
  while (prefix > 0 && ((prefix < old_size && continuation(old[prefix])) ||
                        (prefix < text.size() && continuation(text[prefix]))))
    --prefix;
  while (suffix > 0 && continuation(old[old_size - suffix])) --suffix;
  if (prefix == old_size && prefix == text.size()) return;
  const Edit e{prefix, old_size - suffix - prefix, std::string(text.substr(prefix, text.size() - suffix - prefix))};
  if (debug_on())
    log(LogLevel::debug, kComp, "replace_text",
        {{"bytes", num(text.size())}, {"offset", num(e.offset)}, {"removed", num(e.removed)},
         {"inserted", num(e.inserted.size())}});
  try {
    doc_.apply(e);
  } catch (const std::exception& ex) {
    log(LogLevel::debug, kComp, "edit_failed",
        {{"offset", num(e.offset)}, {"removed", num(e.removed)}, {"reason", ex.what()}});
    return;
  }
  desired_x_ = -1;
  emit cursorPositionChanged();
}

void SourceEditor::mark_saved() {
  close_group();
  doc_.mark_saved();
  check_modified();
}

void SourceEditor::mark_unsaved() {
  close_group();
  doc_.mark_unsaved();
  check_modified();
}

bool SourceEditor::canUndo() const {
  return !read_only_ && (doc_.can_undo() || (group_ != Group::none && doc_.version() != group_version_));
}

bool SourceEditor::canRedo() const { return !read_only_ && doc_.can_redo(); }

void SourceEditor::check_undo_state() {
  const bool u = canUndo();
  const bool r = canRedo();
  if (u != can_undo_) {
    can_undo_ = u;
    emit canUndoChanged(u);
  }
  if (r != can_redo_) {
    can_redo_ = r;
    emit canRedoChanged(r);
  }
}

void SourceEditor::set_read_only(bool on) {
  if (on == read_only_) return;
  if (debug_on()) log(LogLevel::debug, kComp, "read_only", {{"on", on ? "1" : "0"}});
  close_group();
  if (on && !preedit_.isEmpty()) {
    QGuiApplication::inputMethod()->reset();
    clear_preedit();
  }
  read_only_ = on;
  setAttribute(Qt::WA_InputMethodEnabled, !on);
  if (hasFocus()) QGuiApplication::inputMethod()->update(Qt::ImEnabled);
  check_undo_state();
}

void SourceEditor::on_change(const Change& change) {
  QElapsedTimer timer;
  timer.start();
  move_marks(change);
  if (preedit_line_ < lines_.size()) lines_[preedit_line_].layout.reset();
  preedit_line_ = SIZE_MAX;
  for (const Edit& e : change.edits) {
    cursor_ = transform(cursor_, e);
    anchor_ = transform(anchor_, e);
  }
  std::size_t restyled = 0;
  if (change.kind == ChangeKind::load) {
    rebuild_lines();
    cursor_ = snap(cursor_);
    anchor_ = snap(anchor_);
    restyle();
    restyled = lines_.size();
  } else {
    update_lines(merge_edits(change.edits));
    cursor_ = snap(cursor_);
    anchor_ = snap(anchor_);
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (const auto& [from, to] : touched_ranges(change)) spans.emplace_back(line_index(from), line_index(to));
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<std::size_t, std::size_t>> joined;
    for (const auto& s : spans) {
      if (!joined.empty() && s.first <= joined.back().second + 1) {
        joined.back().second = std::max(joined.back().second, s.second);
      } else {
        joined.push_back(s);
      }
    }
    for (const auto& [first, last] : joined) {
      restyle(first, last);
      restyled += last - first + 1;
    }
    if (touches_definition(change)) refresh_reference_images();
  }
  update_preedit_line();
  layout_dirty_ = true;
  viewport()->update();
  if (debug_on())
    log(LogLevel::debug, kComp, "change",
        {{"lines", num(lines_.size())}, {"restyled", num(restyled)},
         {"us", num(static_cast<std::size_t>(timer.nsecsElapsed() / 1000))}});
  if (change.kind != ChangeKind::load) emit textChanged();
  check_modified();
  check_undo_state();
  flush_image_requests();
}

void SourceEditor::move_marks(const Change& change) {
  if (marks_.empty()) return;
  if (change.kind == ChangeKind::load) {
    marks_.clear();
    return;
  }
  for (auto& [layer, m] : marks_) {
    std::vector<ByteRange> kept;
    kept.reserve(m.ranges.size());
    for (ByteRange r : m.ranges) {
      bool touched = false;
      for (const Edit& e : change.edits) {
        if (e.offset < r.end && r.start < e.offset + e.removed) touched = true;
        if (e.removed == 0 && r.start < e.offset && e.offset < r.end) touched = true;
        if (touched) break;
        r = {transform(r.start, e), transform(r.end, e)};
      }
      if (!touched) kept.push_back(r);
    }
    m.ranges = std::move(kept);
  }
}

void SourceEditor::relayout(const std::vector<ByteRange>& ranges) {
  for (const ByteRange& r : ranges) {
    if (lines_.empty()) return;
    for (std::size_t i = line_index(r.start), last = line_index(r.end); i <= last && i < lines_.size(); ++i) {
      lines_[i].layout.reset();
    }
  }
  layout_dirty_ = true;
}

void SourceEditor::set_marks(int layer, std::vector<ByteRange> ranges, const QTextCharFormat& format) {
  std::erase_if(ranges, [&](const ByteRange& r) { return r.end <= r.start || r.end > doc_.size(); });
  std::sort(ranges.begin(), ranges.end(), [](const ByteRange& a, const ByteRange& b) { return a.start < b.start; });
  std::vector<ByteRange> joined;
  for (const ByteRange& r : ranges) {
    if (!joined.empty() && r.start <= joined.back().end) {
      joined.back().end = std::max(joined.back().end, r.end);
    } else {
      joined.push_back(r);
    }
  }
  ranges = std::move(joined);
  if (const auto it = marks_.find(layer); it != marks_.end()) {
    relayout(it->second.ranges);
    if (ranges.empty()) marks_.erase(it);
  }
  if (!ranges.empty()) {
    relayout(ranges);
    marks_[layer] = MarkLayer{std::move(ranges), format};
  }
  if (debug_on()) log(LogLevel::debug, kComp, "marks", {{"layer", std::to_string(layer)}});
  viewport()->update();
}

std::vector<ByteRange> SourceEditor::marks(int layer) const {
  const auto it = marks_.find(layer);
  return it == marks_.end() ? std::vector<ByteRange>{} : it->second.ranges;
}

std::size_t SourceEditor::offset_at(QPoint point) const {
  return hit_test(QPointF(point) + QPointF(0, scroll_y()));
}

void SourceEditor::check_modified() {
  const bool m = doc_.modified();
  if (m == modified_) return;
  modified_ = m;
  emit modificationChanged(m);
}

void SourceEditor::rebuild_lines() {
  const Buffer& b = doc_.buffer();
  const std::size_t count = b.line_count();
  lines_.clear();
  lines_.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t start = b.line_start(i);
    const std::size_t next = i + 1 < count ? b.line_start(i + 1) : b.size();
    const std::string bytes = b.slice(start, next - start);
    Line line;
    line.span = split_lines(bytes, start).front();
    line.map = Utf16Map(std::string_view(bytes).substr(0, line.span.length));
    lines_.push_back(std::move(line));
  }
  layout_dirty_ = true;
}

void SourceEditor::update_lines(const TextRegion& region) {
  const std::size_t first = line_index(region.start);
  const std::size_t last = line_index(region.old_end);
  const std::size_t from = lines_[first].span.start;
  const std::size_t old_stop = lines_[last].span.next_start();
  const std::size_t new_stop = old_stop - region.old_end + region.new_end;
  const std::string t = doc_.buffer().slice(from, new_stop - from);
  std::vector<LineSpan> spans = split_lines(t, from);
  if (lines_[last].span.ending > 0) spans.pop_back();

  std::vector<Line> fresh;
  fresh.reserve(spans.size());
  for (const LineSpan& span : spans) {
    Line line;
    line.span = span;
    line.map = Utf16Map(std::string_view(t).substr(span.start - from, span.length));
    fresh.push_back(std::move(line));
  }
  const auto first_it = lines_.begin() + static_cast<std::ptrdiff_t>(first);
  const auto stop_it = lines_.begin() + static_cast<std::ptrdiff_t>(last + 1);
  const std::size_t after = first + fresh.size();
  lines_.erase(first_it, stop_it);
  lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(first), std::make_move_iterator(fresh.begin()),
                std::make_move_iterator(fresh.end()));
  for (std::size_t i = after; i < lines_.size(); ++i) {
    lines_[i].span.start = lines_[i].span.start - region.old_end + region.new_end;
  }
  layout_dirty_ = true;
}

void SourceEditor::restyle() {
  if (!lines_.empty()) restyle(0, lines_.size() - 1);
}

void SourceEditor::restyle(std::size_t first, std::size_t last) {
  const SyntaxTree& tree = doc_.tree();
  const auto nodes = tree.nodes();
  const std::size_t count = last - first + 1;
  const std::size_t lo = lines_[first].span.start;
  const std::size_t hi = lines_[last].span.next_start();
  std::vector<QList<QTextLayout::FormatRange>> formats(count);
  std::vector<char> background(count, 0);
  std::vector<std::vector<LineImage>> images(count);
  struct Frame {
    NodeId end;
    QTextCharFormat format;
  };
  std::vector<Frame> stack;
  NodeId begin = 1;
  if (nodes.size() > 1) {
    const auto it = std::upper_bound(nodes.begin() + 1, nodes.end(), lo,
                                     [](std::size_t pos, const Node& n) { return pos < n.start; });
    if (it != nodes.begin() + 1) {
      begin = static_cast<NodeId>(it - nodes.begin() - 1);
      while (nodes[begin].parent != 0) begin = nodes[begin].parent;
    }
  }
  for (NodeId id = 0; id < nodes.size();) {
    const Node& n = nodes[id];
    if (id == 0 && begin > 1) {
      const QTextCharFormat own = styles_.format(n);
      if (!own.isEmpty()) stack.push_back(Frame{n.subtree_end, own});
      id = begin;
      continue;
    }
    if (id > 0 && (n.end <= lo || n.start >= hi) && !(n.start == n.end && n.start == lo)) {
      id = n.subtree_end;
      continue;
    }
    while (!stack.empty() && stack.back().end <= id) stack.pop_back();
    if (n.kind == NodeKind::image) {
      const std::size_t end_line = line_index(n.end > n.start ? n.end - 1 : n.start);
      if (end_line >= first && end_line <= last) {
        if (const auto target = link_at(doc_, n.start); target && target->kind == md::UrlKind::image) {
          std::string alt;
          for (NodeId c : tree.children(id)) {
            if (nodes[c].kind == NodeKind::link_text) {
              alt = doc_.buffer().slice(nodes[c].start, nodes[c].end - nodes[c].start);
              break;
            }
          }
          LineImage li;
          li.url = target->url;
          li.start = n.start;
          li.end = n.end;
          li.alt = QString::fromUtf8(alt.data(), static_cast<int>(alt.size()));
          li.reference = n.variant != link_variant_inline;
          images[end_line - first].push_back(std::move(li));
          queue_image(target->url);
        }
      }
    }
    const bool line_bg = styles_.has_line_background(n);
    QTextCharFormat own = styles_.format(n);
    if (own.isEmpty() && !line_bg) {
      ++id;
      continue;
    }
    QTextCharFormat merged = own;
    if (!stack.empty()) {
      merged = stack.back().format;
      merged.merge(own);
    }
    if (!own.isEmpty()) stack.push_back(Frame{n.subtree_end, merged});
    if (n.end > n.start) {
      for (std::size_t i = std::max(first, line_index(n.start)); i <= last && lines_[i].span.start < n.end; ++i) {
        const Line& l = lines_[i];
        if (line_bg) background[i - first] = 1;
        if (own.isEmpty()) continue;
        const std::size_t s = std::max(n.start, l.span.start);
        const std::size_t e = std::min(n.end, l.span.text_end());
        if (e <= s) continue;
        const int a = to_utf16(l, s);
        const int b = to_utf16(l, e);
        if (b > a) formats[i - first].append(QTextLayout::FormatRange{a, b - a, merged});
      }
    }
    ++id;
  }
  auto same_images = [](const std::vector<LineImage>& a, const std::vector<LineImage>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i].url != b[i].url || a[i].start != b[i].start || a[i].end != b[i].end || a[i].alt != b[i].alt)
        return false;
    return true;
  };
  std::size_t changed = 0;
  for (std::size_t k = 0; k < count; ++k) {
    Line& l = lines_[first + k];
    const bool bg = background[k] != 0;
    const bool same_imgs = same_images(l.images, images[k]);
    if (l.formats == formats[k] && l.background == bg && same_imgs) continue;
    l.formats = std::move(formats[k]);
    l.background = bg;
    if (!same_imgs) l.images = std::move(images[k]);
    l.layout.reset();
    ++changed;
  }
  if (changed > 0) layout_dirty_ = true;
}

void SourceEditor::set_style_table(StyleTable table) {
  styles_ = std::move(table);
  custom_styles_ = true;
  for (Line& l : lines_) l.layout.reset();
  restyle();
  layout_dirty_ = true;
  viewport()->update();
}

void SourceEditor::use_default_styles() {
  styles_ = StyleTable::make_default(font(), palette());
  for (Line& l : lines_) l.layout.reset();
  restyle();
  layout_dirty_ = true;
  viewport()->update();
}

void SourceEditor::set_image_max_height(int px) {
  px = std::max(px, 0);
  if (px == image_max_height_) return;
  const bool turning_on = image_max_height_ == 0 && px > 0;
  image_max_height_ = px;
  for (Line& l : lines_) l.layout.reset();
  layout_dirty_ = true;
  if (turning_on) {
    for (const Line& l : lines_)
      for (const LineImage& img : l.images) queue_image(img.url);
    flush_image_requests();
  }
  viewport()->update();
}

void SourceEditor::queue_image(const std::string& url) {
  if (image_max_height_ <= 0) return;
  if (images_.find(url) != images_.end()) return;
  images_.emplace(url, ImageEntry{});
  pending_image_requests_.push_back(url);
}

void SourceEditor::reload_images() {
  images_.clear();
  pending_image_requests_.clear();
  for (Line& l : lines_) {
    if (l.images.empty()) continue;
    l.layout.reset();
    for (const LineImage& img : l.images) queue_image(img.url);
  }
  layout_dirty_ = true;
  flush_image_requests();
  viewport()->update();
}

bool SourceEditor::touches_definition(const Change& change) const {
  const SyntaxTree& tree = doc_.tree();
  for (const auto& [from, to] : touched_ranges(change)) {
    for (std::size_t i = line_index(from), last = line_index(to); i <= last && i < lines_.size(); ++i) {
      const LineSpan& span = lines_[i].span;
      for (const std::size_t at : {span.start, span.text_end() > span.start ? span.text_end() - 1 : span.start}) {
        for (NodeId id = tree.node_at(at); id != no_node; id = tree[id].parent) {
          if (tree[id].kind == NodeKind::link_reference_definition) return true;
        }
      }
    }
  }
  return false;
}

void SourceEditor::refresh_reference_images() {
  for (Line& l : lines_) {
    for (LineImage& img : l.images) {
      if (!img.reference) continue;
      const auto target = link_at(doc_, img.start);
      if (!target || target->url == img.url) continue;
      img.url = target->url;
      queue_image(img.url);
      l.layout.reset();
      layout_dirty_ = true;
    }
  }
}

void SourceEditor::flush_image_requests() {
  if (pending_image_requests_.empty()) return;
  std::vector<std::string> todo;
  todo.swap(pending_image_requests_);
  if (debug_on()) log(LogLevel::debug, kComp, "image_request", {{"count", num(todo.size())}});
  for (const std::string& url : todo) emit imageRequested(url);
}

void SourceEditor::set_image(const std::string& url, const QImage& image) {
  if (image.isNull()) {
    set_image_failed(url);
    return;
  }
  ImageEntry& e = images_[url];
  e.state = ImageState::ready;
  e.image = image;
  e.scaled = QPixmap();
  e.scaled_size = QSize();
  if (debug_on())
    log(LogLevel::debug, kComp, "image_set",
        {{"ok", "1"}, {"w", num(static_cast<std::size_t>(std::max(0, image.width())))},
         {"h", num(static_cast<std::size_t>(std::max(0, image.height())))}});
  relayout_image(url);
}

void SourceEditor::set_image_failed(const std::string& url) {
  ImageEntry& e = images_[url];
  e.state = ImageState::failed;
  e.image = QImage();
  e.scaled = QPixmap();
  e.scaled_size = QSize();
  if (debug_on()) log(LogLevel::debug, kComp, "image_set", {{"ok", "0"}, {"w", "0"}, {"h", "0"}});
  relayout_image(url);
}

void SourceEditor::relayout_image(const std::string& url) {
  for (Line& l : lines_) {
    for (const LineImage& img : l.images) {
      if (img.url == url) {
        l.layout.reset();
        break;
      }
    }
  }
  layout_dirty_ = true;
  viewport()->update();
}

int SourceEditor::image_gap() const { return QFontMetrics(font()).lineSpacing() / 4; }

QSize SourceEditor::image_box_size(const LineImage& img) const {
  const int width_limit = layout_width();
  const auto it = images_.find(img.url);
  if (it != images_.end() && it->second.state == ImageState::ready) {
    const QImage& image = it->second.image;
    const qreal dpr = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
    const qreal w = image.width() / dpr;
    const qreal h = image.height() / dpr;
    qreal scale = 1.0;
    if (w > 0) scale = std::min(scale, width_limit / w);
    if (h > 0) scale = std::min(scale, static_cast<qreal>(image_max_height_) / h);
    return QSize(std::max(1, static_cast<int>(std::lround(w * scale))),
                std::max(1, static_cast<int>(std::lround(h * scale))));
  }
  const QFontMetrics fm(font());
  const QString text = img.alt.isEmpty() ? QString::fromStdString(img.url) : img.alt;
  const int w = std::min(width_limit, fm.horizontalAdvance(text) + 8);
  return QSize(std::max(1, w), fm.lineSpacing() + 4);
}

std::vector<std::pair<QRect, const SourceEditor::LineImage*>> SourceEditor::image_boxes(const Line& l) const {
  std::vector<std::pair<QRect, const LineImage*>> out;
  if (image_max_height_ <= 0) return out;
  const int gap = image_gap();
  int y = l.text_height;
  for (const LineImage& img : l.images) {
    y += gap;
    const QSize size = image_box_size(img);
    out.emplace_back(QRect(0, y, size.width(), size.height()), &img);
    y += size.height() + gap;
  }
  return out;
}

std::optional<std::size_t> SourceEditor::image_at(QPointF doc_point) const {
  if (image_max_height_ <= 0) return std::nullopt;
  ensure_layout();
  if (doc_point.y() < 0) return std::nullopt;
  const Line& l = lines_[line_at_y(static_cast<int>(std::floor(doc_point.y())))];
  const qreal local_y = doc_point.y() - l.y;
  const qreal local_x = doc_point.x() - left_margin();
  for (const auto& [rect, img] : image_boxes(l)) {
    if (local_y >= rect.top() && local_y < rect.bottom() + 1 && local_x >= rect.left() && local_x < rect.right() + 1)
      return img->start;
  }
  return std::nullopt;
}

void SourceEditor::set_max_text_width(int chars) {
  max_text_width_ = std::max(chars, 0);
  ensure_layout();  
  viewport()->update();
}

void SourceEditor::set_page_centred(bool on) {
  page_centred_ = on;
  viewport()->update();
}

void SourceEditor::set_line_numbers(bool on) {
  line_numbers_ = on;
  ensure_layout();  
  viewport()->update();
}

int SourceEditor::gutter_width() const {
  if (!line_numbers_) return 0;
  int digits = 1;
  for (std::size_t n = lines_.size(); n >= 10; n /= 10) ++digits;
  const QFontMetricsF fm(font());
  return static_cast<int>(std::ceil(std::max(digits, 2) * fm.horizontalAdvance(QLatin1Char('0')) + 2 * average_char_width(fm)));
}

int SourceEditor::page_left() const {
  const int left = styles_.page_margin + gutter_width();
  if (!page_centred_) return left;
  const int page_w = layout_width() + 2 * styles_.page_padding;
  return std::max(left, (viewport()->width() - page_w) / 2);
}

qreal SourceEditor::left_margin() const { return page_left() + styles_.page_padding; }

int SourceEditor::top_space() const { return styles_.page_margin + styles_.page_padding; }

int SourceEditor::layout_width() const {
  int width = viewport()->width() - 2 * styles_.page_margin - gutter_width() - 2 * styles_.page_padding;
  if (max_text_width_ > 0)
    width = std::min(width, static_cast<int>(std::ceil(average_char_width(QFontMetricsF(font())) * max_text_width_)));
  return std::max(width, 20);
}

int SourceEditor::scroll_y() const { return verticalScrollBar()->value(); }

void SourceEditor::layout_line(std::size_t index) const {
  Line& l = lines_[index];
  auto layout = std::make_unique<QTextLayout>(l.map.text(), font());
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * 4);
  layout->setTextOption(option);
  layout->setCacheEnabled(true);
  const QList<QTextLayout::FormatRange> base = line_formats(index);
  if (index == preedit_line_ && !preedit_.isEmpty()) {
    const int at = to_utf16(l, cursor_);
    layout->setPreeditArea(at, preedit_);
    QList<QTextLayout::FormatRange> formats = shift_formats(base, at, static_cast<int>(preedit_.size()));
    for (QTextLayout::FormatRange r : preedit_formats_) {
      r.start += at;
      formats.append(r);
    }
    layout->setFormats(formats);
  } else {
    layout->setFormats(base);
  }
  const qreal width = layout_width();
  qreal height = 0;
  layout->beginLayout();
  while (true) {
    QTextLine tl = layout->createLine();
    if (!tl.isValid()) break;
    tl.setLineWidth(width);
    tl.setPosition(QPointF(0, height));
    height += tl.height();
  }
  layout->endLayout();
  l.text_height = std::max(static_cast<int>(std::ceil(height)), 1);
  l.height = l.text_height;
  if (image_max_height_ > 0 && !l.images.empty()) {
    const auto boxes = image_boxes(l);
    l.height = boxes.back().first.bottom() + 1 + image_gap();
  }
  l.layout = std::move(layout);
}

void SourceEditor::ensure_layout() const {
  const int width = layout_width();
  if (width != layout_width_) {
    for (Line& l : lines_) l.layout.reset();
    layout_width_ = width;
    layout_dirty_ = true;
  }
  if (!layout_dirty_) return;
  QElapsedTimer timer;
  timer.start();
  std::size_t count = 0;
  int y = top_space();
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    if (!lines_[i].layout) {
      layout_line(i);
      ++count;
    }
    lines_[i].y = y;
    y += lines_[i].height;
  }
  total_height_ = y + top_space();
  layout_dirty_ = false;
  update_scrollbar();
  if (debug_on())
    log(LogLevel::debug, kComp, "layout",
        {{"lines", num(lines_.size())}, {"relaid", num(count)}, {"width", num(static_cast<std::size_t>(width))},
         {"us", num(static_cast<std::size_t>(timer.nsecsElapsed() / 1000))}});
}

void SourceEditor::update_scrollbar() const {
  const int view = viewport()->height();
  QScrollBar* bar = verticalScrollBar();
  bar->setRange(0, std::max(0, total_height_ - view));
  bar->setPageStep(view);
  bar->setSingleStep(std::max(1, QFontMetrics(font()).lineSpacing()));
}

int SourceEditor::visual_line_count() const {
  ensure_layout();
  int count = 0;
  for (const Line& l : lines_) count += l.layout->lineCount();
  return count;
}

QString SourceEditor::line_text(std::size_t line) const { return lines_.at(line).map.text(); }

int SourceEditor::line_height(std::size_t line) const {
  ensure_layout();
  return lines_.at(line).height;
}

QList<QTextLayout::FormatRange> SourceEditor::line_formats(std::size_t line) const {
  const Line& l = lines_.at(line);
  QList<QTextLayout::FormatRange> out = l.formats;
  for (const auto& [layer, m] : marks_) {
    auto it = std::lower_bound(m.ranges.begin(), m.ranges.end(), l.span.start,
                               [](const ByteRange& r, std::size_t v) { return r.end <= v; });
    for (; it != m.ranges.end() && it->start < l.span.text_end(); ++it) {
      const int a = to_utf16(l, std::max(it->start, l.span.start));
      const int b = to_utf16(l, std::min(it->end, l.span.text_end()));
      if (b > a) out.append(QTextLayout::FormatRange{a, b - a, m.format});
    }
  }
  return out;
}

std::size_t SourceEditor::line_index(std::size_t pos) const {
  const auto it = std::upper_bound(lines_.begin(), lines_.end(), pos,
                                   [](std::size_t p, const Line& l) { return p < l.span.start; });
  return it == lines_.begin() ? 0 : static_cast<std::size_t>(it - lines_.begin()) - 1;
}

std::size_t SourceEditor::line_at_y(int y) const {
  const auto it =
      std::upper_bound(lines_.begin(), lines_.end(), y, [](int v, const Line& l) { return v < l.y; });
  return it == lines_.begin() ? 0 : static_cast<std::size_t>(it - lines_.begin()) - 1;
}

int SourceEditor::to_utf16(const Line& line, std::size_t pos) const {
  pos = std::clamp(pos, line.span.start, line.span.text_end());
  return line.map.to_utf16(pos - line.span.start);
}

std::size_t SourceEditor::snap(std::size_t pos) const {
  pos = std::min(pos, doc_.size());
  const Line& l = lines_[line_index(pos)];
  if (pos >= l.span.text_end()) return l.span.text_end();
  return l.span.start + l.map.to_utf8(l.map.to_utf16(pos - l.span.start));
}

std::size_t SourceEditor::hit_test(QPointF doc_point) const {
  ensure_layout();
  const int y = std::max(0, static_cast<int>(std::floor(doc_point.y())));
  const Line& l = lines_[line_at_y(y)];
  const qreal local_y = doc_point.y() - l.y;
  if (image_max_height_ > 0 && local_y >= l.text_height) {
    const auto boxes = image_boxes(l);
    if (!boxes.empty()) {
      const int gap = image_gap();
      const LineImage* chosen = boxes.back().second;
      for (const auto& [rect, img] : boxes) {
        if (local_y < rect.bottom() + 1 + gap) {
          chosen = img;
          break;
        }
      }
      return snap(chosen->start);
    }
  }
  QTextLine tl = l.layout->lineAt(0);
  for (int j = 0; j < l.layout->lineCount(); ++j) {
    tl = l.layout->lineAt(j);
    if (local_y < tl.y() + tl.height()) break;
  }
  const int pos = tl.xToCursor(doc_point.x() - left_margin());
  return snap(l.span.start + l.map.to_utf8(pos));
}

std::optional<std::size_t> SourceEditor::char_at(QPointF doc_point) const {
  ensure_layout();
  if (doc_point.y() < 0) return std::nullopt;
  const Line& l = lines_[line_at_y(static_cast<int>(std::floor(doc_point.y())))];
  const qreal local_y = doc_point.y() - l.y;
  for (int j = 0; j < l.layout->lineCount(); ++j) {
    const QTextLine tl = l.layout->lineAt(j);
    if (local_y < tl.y() || local_y >= tl.y() + tl.height()) continue;
    const qreal x = doc_point.x() - left_margin();
    if (tl.textLength() == 0 || x < tl.x() || x >= tl.x() + tl.naturalTextWidth()) return std::nullopt;
    const int pos = tl.xToCursor(x, QTextLine::CursorOnCharacter);
    if (pos < tl.textStart() || pos >= tl.textStart() + tl.textLength()) return std::nullopt;
    return l.span.start + l.map.to_utf8(pos);
  }
  return std::nullopt;
}

QRectF SourceEditor::caret_doc_rect() const {
  ensure_layout();
  const Line& l = lines_[line_index(cursor_)];
  int pos = to_utf16(l, cursor_);
  if (line_index(cursor_) == preedit_line_) pos += preedit_cursor_;
  QTextLine tl = l.layout->lineForTextPosition(pos);
  if (!tl.isValid()) tl = l.layout->lineAt(l.layout->lineCount() - 1);
  const qreal x = tl.cursorToX(pos);
  return QRectF(left_margin() + x, l.y + tl.y(), 1, tl.height());
}

QRect SourceEditor::cursor_rect() const {
  return caret_doc_rect().translated(0, -scroll_y()).toAlignedRect();
}

std::size_t SourceEditor::move_target(Move move, qreal* x_out) {
  ensure_layout();
  const std::size_t i = line_index(cursor_);
  const Line& l = lines_[i];
  const int p = to_utf16(l, cursor_);
  const std::size_t n = lines_.size();
  auto at = [](const Line& line, int pos) { return line.span.start + line.map.to_utf8(pos); };
  switch (move) {
    case Move::left:
    case Move::word_left:
      if (p > 0)
        return at(l, l.layout->previousCursorPosition(
                         p, move == Move::left ? QTextLayout::SkipCharacters : QTextLayout::SkipWords));
      return i > 0 ? lines_[i - 1].span.text_end() : cursor_;
    case Move::right:
    case Move::word_right:
      if (p < l.map.size())
        return at(l, l.layout->nextCursorPosition(
                         p, move == Move::right ? QTextLayout::SkipCharacters : QTextLayout::SkipWords));
      return i + 1 < n ? lines_[i + 1].span.start : cursor_;
    case Move::line_start:
    case Move::line_end: {
      QTextLine tl = l.layout->lineForTextPosition(p);
      if (!tl.isValid()) tl = l.layout->lineAt(0);
      if (move == Move::line_start) return at(l, tl.textStart());
      int end = tl.textStart() + tl.textLength();
      if (tl.lineNumber() + 1 < l.layout->lineCount()) end = l.layout->previousCursorPosition(end);
      return at(l, end);
    }
    case Move::doc_start: return 0;
    case Move::doc_end: return doc_.size();
    case Move::up:
    case Move::down: {
      QTextLine tl = l.layout->lineForTextPosition(p);
      if (!tl.isValid()) tl = l.layout->lineAt(0);
      const qreal x = desired_x_ >= 0 ? desired_x_ : tl.cursorToX(p);
      if (x_out) *x_out = x;
      const int vn = tl.lineNumber();
      if (move == Move::down) {
        if (vn + 1 < l.layout->lineCount()) return at(l, l.layout->lineAt(vn + 1).xToCursor(x));
        if (i + 1 < n) return at(lines_[i + 1], lines_[i + 1].layout->lineAt(0).xToCursor(x));
        return doc_.size();
      }
      if (vn > 0) return at(l, l.layout->lineAt(vn - 1).xToCursor(x));
      if (i > 0) {
        const Line& prev = lines_[i - 1];
        return at(prev, prev.layout->lineAt(prev.layout->lineCount() - 1).xToCursor(x));
      }
      return 0;
    }
    case Move::page_up:
    case Move::page_down: {
      const QRectF r = caret_doc_rect();
      const qreal x = desired_x_ >= 0 ? desired_x_ : r.x() - left_margin();
      if (x_out) *x_out = x;
      const int page = viewport()->height();
      const qreal y = r.center().y() + (move == Move::page_down ? page : -page);
      QScrollBar* bar = verticalScrollBar();
      bar->setValue(bar->value() + (move == Move::page_down ? page : -page));
      if (y < 0) return 0;
      if (y >= total_height_) return doc_.size();
      return hit_test(QPointF(x + left_margin(), y));
    }
  }
  return cursor_;
}

std::string SourceEditor::selected_text() const {
  const std::size_t lo = std::min(cursor_, anchor_);
  const std::size_t hi = std::max(cursor_, anchor_);
  return doc_.buffer().slice(lo, hi - lo);
}

void SourceEditor::move_cursor(std::size_t pos, bool keep_anchor) {
  close_group();
  cursor_ = snap(pos);
  if (!keep_anchor) anchor_ = cursor_;
  desired_x_ = -1;
  ensure_cursor_visible();
  restart_blink();
  viewport()->update();
  if (hasFocus()) QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
  emit cursorPositionChanged();
}

void SourceEditor::set_cursor_position(std::size_t pos, bool keep_anchor) { move_cursor(pos, keep_anchor); }

void SourceEditor::select(std::size_t anchor, std::size_t cursor) {
  move_cursor(anchor, false);
  move_cursor(cursor, true);
}

void SourceEditor::selectAll() {
  select(0, doc_.size());
  set_primary_selection();
}

void SourceEditor::navigate(Move move, bool keep_anchor) {
  if (!keep_anchor && has_selection() && (move == Move::left || move == Move::right)) {
    move_cursor(move == Move::left ? std::min(cursor_, anchor_) : std::max(cursor_, anchor_), false);
    return;
  }
  qreal x = -1;
  const std::size_t target = move_target(move, &x);
  move_cursor(target, keep_anchor);
  if (x >= 0) desired_x_ = x;
  if (keep_anchor) set_primary_selection();
}

void SourceEditor::ensure_cursor_visible() {
  QRectF r = caret_doc_rect();
  if (r.top() <= lines_.front().y) r.setTop(0);
  if (r.bottom() >= lines_.back().y + lines_.back().height) r.setBottom(total_height_);
  QScrollBar* bar = verticalScrollBar();
  const int top = bar->value();
  const int view = viewport()->height();
  if (r.top() < top) {
    bar->setValue(static_cast<int>(std::floor(r.top())));
  } else if (r.bottom() > top + view) {
    bar->setValue(static_cast<int>(std::ceil(r.bottom())) - view);
  }
}

void SourceEditor::select_word_at(std::size_t pos) {
  const Line& l = lines_[line_index(pos)];
  const int p = to_utf16(l, pos);
  QTextBoundaryFinder finder(QTextBoundaryFinder::Word, l.map.text());
  finder.setPosition(p);
  qsizetype start = p;
  if (!(finder.isAtBoundary() && (finder.boundaryReasons() & QTextBoundaryFinder::StartOfItem)))
    start = std::max<qsizetype>(finder.toPreviousBoundary(), 0);
  finder.setPosition(start);
  qsizetype end = finder.toNextBoundary();
  if (end < 0) end = l.map.size();
  select(l.span.start + l.map.to_utf8(static_cast<int>(start)), l.span.start + l.map.to_utf8(static_cast<int>(end)));
}

void SourceEditor::begin_group(Group group) {
  doc_.begin();
  group_ = group;
  group_version_ = doc_.version();
}

void SourceEditor::close_group() {
  if (group_ == Group::none) return;
  group_ = Group::none;
  doc_.commit();
  check_undo_state();
}

void SourceEditor::replace(std::size_t from, std::size_t to, std::string_view text) {
  const ListShape before = list_shape(doc_, from, to);
  const std::size_t end = from + text.size();
  Selection sel{end, end};
  doc_.begin();
  try {
    doc_.apply(Edit{from, to - from, std::string(text)});
  } catch (const std::exception& ex) {
    doc_.commit();
    log(LogLevel::debug, kComp, "edit_failed",
        {{"offset", num(from)}, {"removed", num(to - from)}, {"reason", ex.what()}});
    return;
  }
  try {
    sel = renumber(before, from, end, sel);
  } catch (const std::exception& ex) {
    log(LogLevel::debug, kComp, "renumber_failed", {{"offset", num(from)}, {"reason", ex.what()}});
  }
  doc_.commit();
  cursor_ = snap(sel.cursor);
  anchor_ = cursor_;
  desired_x_ = -1;
}

Selection SourceEditor::renumber(const ListShape& before, std::size_t from, std::size_t to, Selection sel) {
  const auto r = renumber_lists(doc_, before, from, to, sel);
  if (!r) return sel;
  for (const Edit& e : r->edits) doc_.apply(e);
  return r->selection;
}

void SourceEditor::type_text(std::string text) {
  if (text.empty()) return;
  const bool space = is_space_text(text);
  const bool sel = has_selection();
  if (group_ != Group::typing || sel || cursor_ != group_end_ || (group_last_space_ && !space)) {
    close_group();
    begin_group(Group::typing);
  }
  replace(std::min(cursor_, anchor_), std::max(cursor_, anchor_), text);
  group_end_ = cursor_;
  group_last_space_ = space;
  ensure_cursor_visible();
  restart_blink();
  emit cursorPositionChanged();
}

void SourceEditor::delete_range(std::size_t from, std::size_t to, Group group) {
  if (from >= to) return;
  if (group == Group::none) {
    close_group();
  } else if (group_ != group || (from != group_end_ && to != group_end_)) {
    close_group();
    begin_group(group);
  }
  replace(from, to, {});
  group_end_ = cursor_;
  ensure_cursor_visible();
  restart_blink();
  emit cursorPositionChanged();
}

bool SourceEditor::apply_command(const std::optional<CommandResult>& result) {
  if (!result || read_only_) return false;
  if (!preedit_.isEmpty()) {
    QGuiApplication::inputMethod()->reset();
    clear_preedit();
  }
  close_group();
  Selection sel = result->selection;
  if (!result->edits.empty()) {
    const TextRegion region = merge_edits(result->edits);
    const ListShape before = list_shape(doc_, region.start, region.old_end);
    std::vector<Edit> edits = result->edits;
    if (edits.size() > 1) {
      std::string text = doc_.buffer().slice(region.start, region.old_end - region.start);
      for (const Edit& e : edits) text.replace(e.offset - region.start, e.removed, e.inserted);
      edits = {Edit{region.start, region.old_end - region.start, std::move(text)}};
    }
    doc_.begin();
    bool failed = false;
    for (const Edit& e : edits) {
      try {
        doc_.apply(e);
      } catch (const std::exception& ex) {
        log(LogLevel::debug, kComp, "edit_failed",
            {{"offset", num(e.offset)}, {"removed", num(e.removed)}, {"reason", ex.what()}});
        failed = true;
        break;
      }
    }
    if (!failed) {
      try {
        sel = renumber(before, region.start, region.new_end, sel);
      } catch (const std::exception& ex) {
        log(LogLevel::debug, kComp, "renumber_failed", {{"offset", num(region.start)}, {"reason", ex.what()}});
        failed = true;
      }
    }
    doc_.commit();
    if (failed) {
      if (doc_.can_undo()) doc_.undo();
      check_undo_state();
      return true;
    }
    check_undo_state();
  }
  anchor_ = snap(sel.anchor);
  cursor_ = snap(sel.cursor);
  desired_x_ = -1;
  ensure_cursor_visible();
  restart_blink();
  viewport()->update();
  emit cursorPositionChanged();
  return true;
}

void SourceEditor::insert_text(std::string_view utf8) {
  close_group();
  replace(std::min(cursor_, anchor_), std::max(cursor_, anchor_), utf8);
  ensure_cursor_visible();
  emit cursorPositionChanged();
}

std::string SourceEditor::normalize_newlines(std::string text) const {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
      out += newline_;
    } else if (c == '\n') {
      out += newline_;
    } else {
      out += c;
    }
  }
  return out;
}

void SourceEditor::undo() {
  if (read_only_) return;
  close_group();
  clear_preedit();
  std::vector<Edit> edits;
  try {
    edits = doc_.undo();
  } catch (const std::exception& ex) {
    log(LogLevel::debug, kComp, "undo_failed", {{"reason", ex.what()}});
    return;
  }
  if (debug_on()) log(LogLevel::debug, kComp, "undo", {{"edits", num(edits.size())}});
  if (edits.empty()) return;
  move_cursor(edits.back().offset + edits.back().inserted.size(), false);
}

void SourceEditor::redo() {
  if (read_only_) return;
  close_group();
  clear_preedit();
  std::vector<Edit> edits;
  try {
    edits = doc_.redo();
  } catch (const std::exception& ex) {
    log(LogLevel::debug, kComp, "redo_failed", {{"reason", ex.what()}});
    return;
  }
  if (debug_on()) log(LogLevel::debug, kComp, "redo", {{"edits", num(edits.size())}});
  if (edits.empty()) return;
  move_cursor(edits.back().offset + edits.back().inserted.size(), false);
}

void SourceEditor::copy() {
  if (!has_selection()) return;
  const std::string s = selected_text();
  QGuiApplication::clipboard()->setText(QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())));
}

void SourceEditor::cut() {
  if (read_only_ || !has_selection()) return;
  copy();
  delete_range(std::min(cursor_, anchor_), std::max(cursor_, anchor_), Group::none);
}

void SourceEditor::paste() {
  if (read_only_) return;
  insert_clipboard_text(QGuiApplication::clipboard()->text(), "paste");
}

void SourceEditor::insert_clipboard_text(const QString& clip, const char* event) {
  if (clip.isEmpty()) return;
  const QByteArray utf8 = clip.toUtf8();
  std::string text = normalize_newlines(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
  if (debug_on())
    log(LogLevel::debug, kComp, event, {{"bytes", num(text.size())}, {"newline", newline_ == "\n" ? "lf" : "crlf"}});
  insert_text(text);
}

std::optional<QClipboard::Mode> SourceEditor::primary_mode() const {
  if (clipboard_as_primary_) return QClipboard::Clipboard;
  if (QGuiApplication::clipboard()->supportsSelection()) return QClipboard::Selection;
  return std::nullopt;
}

void SourceEditor::set_primary_selection() {
  const auto mode = primary_mode();
  if (!mode || !has_selection()) return;
  const std::string s = selected_text();
  QGuiApplication::clipboard()->setText(QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())), *mode);
}

void SourceEditor::paste_primary(std::size_t pos) {
  const auto mode = primary_mode();
  if (!mode || read_only_) return;
  move_cursor(pos, false);
  insert_clipboard_text(QGuiApplication::clipboard()->text(*mode), "paste_primary");
}

bool SourceEditor::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride) {
    auto* ke = static_cast<QKeyEvent*>(e);
    const bool ctrl = ke->modifiers() & Qt::ControlModifier;
    const bool alt = ke->modifiers() & Qt::AltModifier;
    bool take = false;
    switch (ke->key()) {
      case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
      case Qt::Key_Home: case Qt::Key_End:
        take = !alt;
        break;
      case Qt::Key_PageUp: case Qt::Key_PageDown:
        take = !ctrl && !alt;
        break;
      case Qt::Key_Tab: case Qt::Key_Backtab: case Qt::Key_Backspace: case Qt::Key_Delete: case Qt::Key_Return: case Qt::Key_Enter:
        take = !ctrl && !alt && !read_only_;
        break;
      default:
        take = !read_only_ && printable(ke->text()) && ctrl == alt;
        break;
    }
    if (take) {
      ke->accept();
      return true;
    }
  }
  return QAbstractScrollArea::event(e);
}

bool SourceEditor::focusNextPrevChild(bool next) {
  return read_only_ && QAbstractScrollArea::focusNextPrevChild(next);
}

void SourceEditor::keyPressEvent(QKeyEvent* e) {
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  const bool ctrl = e->modifiers() & Qt::ControlModifier;
  const bool alt = e->modifiers() & Qt::AltModifier;
  e->accept();
  if (e->matches(QKeySequence::Undo)) return undo();
  if (e->matches(QKeySequence::Redo) || (ctrl && !shift && e->key() == Qt::Key_Y)) return redo();
  if (e->matches(QKeySequence::Copy)) return copy();
  if (e->matches(QKeySequence::Cut)) return cut();
  if (e->matches(QKeySequence::Paste)) return paste();
  if (e->matches(QKeySequence::SelectAll)) return selectAll();

  if (read_only_) {
    switch (e->key()) {
      case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
      case Qt::Key_Home: case Qt::Key_End: case Qt::Key_PageUp: case Qt::Key_PageDown:
        break;
      default:
        e->ignore();
        return;
    }
  }
  switch (e->key()) {
    case Qt::Key_Left: return navigate(ctrl ? Move::word_left : Move::left, shift);
    case Qt::Key_Right: return navigate(ctrl ? Move::word_right : Move::right, shift);
    case Qt::Key_Up: return navigate(Move::up, shift);
    case Qt::Key_Down: return navigate(Move::down, shift);
    case Qt::Key_Home: return navigate(ctrl ? Move::doc_start : Move::line_start, shift);
    case Qt::Key_End: return navigate(ctrl ? Move::doc_end : Move::line_end, shift);
    case Qt::Key_PageUp: return navigate(Move::page_up, shift);
    case Qt::Key_PageDown: return navigate(Move::page_down, shift);
    case Qt::Key_Backspace:
      if (!ctrl && apply_command(delete_markup_backward(doc_, {anchor_, cursor_}))) return;
      if (has_selection()) return delete_range(std::min(cursor_, anchor_), std::max(cursor_, anchor_), Group::none);
      return delete_range(move_target(ctrl ? Move::word_left : Move::left), cursor_, Group::deleting);
    case Qt::Key_Delete:
      if (has_selection()) return delete_range(std::min(cursor_, anchor_), std::max(cursor_, anchor_), Group::none);
      return delete_range(cursor_, move_target(ctrl ? Move::word_right : Move::right), Group::deleting);
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (shift) {
        if (apply_command(previous_row(doc_, {anchor_, cursor_}))) return;
        if (apply_command(insert_line_break(doc_, {anchor_, cursor_}, newline_))) return;
      } else {
        if (apply_command(next_row(doc_, {anchor_, cursor_}, newline_))) return;
        if (apply_command(insert_newline(doc_, {anchor_, cursor_}, newline_))) return;
      }
      return type_text(newline_);
    case Qt::Key_Tab:
      if (apply_command(next_cell(doc_, {anchor_, cursor_}, newline_))) return;
      if (apply_command(indent_list(doc_, {anchor_, cursor_}))) return;
      return type_text("\t");
    case Qt::Key_Backtab:
      if (apply_command(previous_cell(doc_, {anchor_, cursor_}))) return;
      apply_command(outdent_list(doc_, {anchor_, cursor_}));
      return;
    default: break;
  }
  const QString text = e->text();
  if (printable(text) && (!ctrl || alt)) {
    const QByteArray utf8 = text.toUtf8();
    return type_text(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
  }
  e->ignore();
}

void SourceEditor::inputMethodEvent(QInputMethodEvent* e) {
  if (read_only_) {
    e->ignore();
    return;
  }
  if (!e->commitString().isEmpty() || e->replacementLength() > 0) {
    const QByteArray utf8 = e->commitString().toUtf8();
    std::string commit(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    if (e->replacementLength() > 0 || e->replacementStart() != 0) {
      const Line& l = lines_[line_index(cursor_)];
      const int c = to_utf16(l, cursor_);
      const int a = std::clamp(c + e->replacementStart(), 0, l.map.size());
      const int b = std::clamp(a + e->replacementLength(), 0, l.map.size());
      if (debug_on()) log(LogLevel::debug, kComp, "ime_replace", {{"start", num(static_cast<std::size_t>(a))}});
      close_group();
      replace(l.span.start + l.map.to_utf8(a), l.span.start + l.map.to_utf8(b), commit);
      ensure_cursor_visible();
      emit cursorPositionChanged();
    } else {
      type_text(std::move(commit));
    }
  }
  preedit_ = e->preeditString();
  preedit_formats_.clear();
  preedit_cursor_ = static_cast<int>(preedit_.size());
  preedit_cursor_visible_ = true;
  for (const QInputMethodEvent::Attribute& a : e->attributes()) {
    if (a.type == QInputMethodEvent::TextFormat) {
      const QTextCharFormat f = qvariant_cast<QTextFormat>(a.value).toCharFormat();
      if (f.isValid()) preedit_formats_.append(QTextLayout::FormatRange{a.start, a.length, f});
    } else if (a.type == QInputMethodEvent::Cursor) {
      preedit_cursor_ = a.start;
      preedit_cursor_visible_ = a.length != 0;
    } else if (a.type == QInputMethodEvent::Selection && preedit_.isEmpty()) {
      const Line& l = lines_[line_index(cursor_)];
      select(l.span.start + l.map.to_utf8(a.start), l.span.start + l.map.to_utf8(a.start + a.length));
    }
  }
  if (preedit_formats_.isEmpty() && !preedit_.isEmpty()) {
    QTextCharFormat f;
    f.setFontUnderline(true);
    preedit_formats_.append(QTextLayout::FormatRange{0, static_cast<int>(preedit_.size()), f});
  }
  update_preedit_line();
  viewport()->update();
  e->accept();
}

void SourceEditor::update_preedit_line() {
  if (preedit_line_ < lines_.size()) lines_[preedit_line_].layout.reset();
  preedit_line_ = preedit_.isEmpty() ? SIZE_MAX : line_index(cursor_);
  if (preedit_line_ < lines_.size()) lines_[preedit_line_].layout.reset();
  layout_dirty_ = true;
}

void SourceEditor::clear_preedit() {
  if (preedit_.isEmpty()) return;
  preedit_.clear();
  preedit_formats_.clear();
  preedit_cursor_ = 0;
  update_preedit_line();
  viewport()->update();
}

QVariant SourceEditor::inputMethodQuery(Qt::InputMethodQuery query) const {
  const Line& l = lines_[line_index(cursor_)];
  switch (query) {
    case Qt::ImEnabled: return true;
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle: return QRectF(cursor_rect().translated(viewport()->pos()));
    case Qt::ImFont: return font();
    case Qt::ImCursorPosition: return to_utf16(l, cursor_);
    case Qt::ImAnchorPosition:
      if (anchor_ < l.span.start) return 0;
      if (anchor_ > l.span.text_end()) return l.map.size();
      return to_utf16(l, anchor_);
    case Qt::ImSurroundingText: return l.map.text();
    case Qt::ImCurrentSelection: {
      const std::string s = selected_text();
      return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
    }
    case Qt::ImHints: return static_cast<int>(Qt::ImhMultiLine);
    default: return QAbstractScrollArea::inputMethodQuery(query);
  }
}

void SourceEditor::mousePressEvent(QMouseEvent* e) {
  const bool middle = e->button() == Qt::MiddleButton && !read_only_ && primary_mode();
  if (e->button() != Qt::LeftButton && !middle) return QAbstractScrollArea::mousePressEvent(e);
  if (!preedit_.isEmpty()) {
    QGuiApplication::inputMethod()->reset();
    clear_preedit();
  }
  if (middle) return paste_primary(hit_test(e->position() + QPointF(0, scroll_y())));
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  if ((e->modifiers() & Qt::ControlModifier) && !shift) {
    const QPointF doc_point = e->position() + QPointF(0, scroll_y());
    if (const auto img = image_at(doc_point)) {
      if (auto target = link_at(doc_, *img)) {
        if (debug_on()) log(LogLevel::debug, kComp, "link_activated", {{"start", std::to_string(target->start)}});
        emit linkActivated(*target);
      }
      return;
    }
    if (const auto at = char_at(doc_point)) {
      if (auto target = link_at(doc_, *at)) {
        if (debug_on()) log(LogLevel::debug, kComp, "link_activated", {{"start", std::to_string(target->start)}});
        emit linkActivated(*target);
        return;
      }
    }
  }
  const std::size_t pos = hit_test(e->position() + QPointF(0, scroll_y()));
  const bool triple = double_click_ms_ >= 0 && !shift &&
                      e->timestamp() - static_cast<quint64>(double_click_ms_) <
                          static_cast<quint64>(QGuiApplication::styleHints()->mouseDoubleClickInterval()) &&
                      (e->position() - double_click_at_).manhattanLength() < QGuiApplication::styleHints()->startDragDistance();
  double_click_ms_ = -1;
  if (triple) {
    const LineSpan& span = lines_[line_index(pos)].span;
    select(span.start, span.next_start());
    selecting_ = false;
    set_primary_selection();
    return;
  }
  if (!shift && acceptDrops() && has_selection() && pos >= std::min(anchor_, cursor_) &&
      pos < std::max(anchor_, cursor_)) {
    drag_candidate_ = true;
    drag_press_pos_ = e->position();
    drag_press_doc_pos_ = pos;
    return;
  }
  move_cursor(pos, shift);
  selecting_ = true;
}

void SourceEditor::mouseMoveEvent(QMouseEvent* e) {
  if (drag_candidate_) {
    if ((e->position() - drag_press_pos_).manhattanLength() < QGuiApplication::styleHints()->startDragDistance())
      return;
    drag_candidate_ = false;
    start_drag();
    return;
  }
  if (!selecting_ || !(e->buttons() & Qt::LeftButton)) return;
  move_cursor(hit_test(e->position() + QPointF(0, scroll_y())), true);
  update_autoscroll(e->position());
}

void SourceEditor::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const bool was_selecting = selecting_;
  selecting_ = false;
  autoscroll_.stop();
  if (drag_candidate_) {
    drag_candidate_ = false;
    move_cursor(drag_press_doc_pos_, false);
  }
  if (was_selecting) set_primary_selection();
}

void SourceEditor::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  select_word_at(hit_test(e->position() + QPointF(0, scroll_y())));
  selecting_ = false;
  set_primary_selection();
  double_click_ms_ = static_cast<qint64>(e->timestamp());
  double_click_at_ = e->position();
}

void SourceEditor::focusInEvent(QFocusEvent* e) {
  restart_blink();
  viewport()->update();
  QAbstractScrollArea::focusInEvent(e);
}

void SourceEditor::focusOutEvent(QFocusEvent* e) {
  close_group();
  blink_.stop();
  autoscroll_.stop();
  caret_on_ = false;
  viewport()->update();
  QAbstractScrollArea::focusOutEvent(e);
}

void SourceEditor::restart_blink() {
  caret_on_ = true;
  const int flash = QGuiApplication::styleHints()->cursorFlashTime();
  if (flash > 0 && hasFocus()) {
    blink_.start(flash / 2, this);
  } else {
    blink_.stop();
  }
  viewport()->update();
}

void SourceEditor::timerEvent(QTimerEvent* e) {
  if (e->timerId() == blink_.timerId()) {
    caret_on_ = !caret_on_;
    viewport()->update(cursor_rect().adjusted(-2, -2, 2, 2));
    return;
  }
  if (e->timerId() == autoscroll_.timerId()) {
    const int h = std::max(1, viewport()->height());
    const qreal y = autoscroll_pos_.y();
    const qreal dist = y < 0 ? -y : y - h;
    const int step = std::clamp(static_cast<int>(dist / 2), 1, h);
    QScrollBar* bar = verticalScrollBar();
    bar->setValue(bar->value() + (y < 0 ? -step : step));
    const QPointF clamped(autoscroll_pos_.x(), std::clamp<qreal>(y, 0, h - 1));
    move_cursor(hit_test(clamped + QPointF(0, scroll_y())), true);
    return;
  }
  QAbstractScrollArea::timerEvent(e);
}

void SourceEditor::update_autoscroll(QPointF viewport_pos) {
  const int h = viewport()->height();
  if (viewport_pos.y() < 0 || viewport_pos.y() >= h) {
    autoscroll_pos_ = viewport_pos;
    if (!autoscroll_.isActive()) autoscroll_.start(30, this);
  } else {
    autoscroll_.stop();
  }
}

void SourceEditor::start_drag() {
  const std::size_t lo = std::min(anchor_, cursor_);
  const std::size_t hi = std::max(anchor_, cursor_);
  const std::string text = selected_text();
  auto* mime = new QMimeData;
  mime->setText(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
  QDrag drag(this);
  drag.setMimeData(mime);
  drag_source_ = ByteRange{lo, hi};
  log(LogLevel::debug, kComp, "drag_start", {{"bytes", num(hi - lo)}});
  drag.exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
  drag_source_.reset();
}

std::optional<std::string> SourceEditor::drop_mime_text(const QMimeData& mime) const {
  QString text;
  if (mime.hasUrls()) {
    QStringList parts;
    for (const QUrl& url : mime.urls()) parts << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    text = parts.join(QString::fromStdString(newline_));
  } else if (mime.hasFormat(QStringLiteral("text/plain"))) {
    text = mime.text();
  } else {
    return std::nullopt;
  }
  const QByteArray utf8 = text.toUtf8();
  return normalize_newlines(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

void SourceEditor::dragEnterEvent(QDragEnterEvent* e) {
  const bool text = drop_mime_text(*e->mimeData()).has_value();
  if (debug_on())
    log(LogLevel::debug, kComp, "drag_enter",
        {{"internal", drag_source_ ? "1" : "0"}, {"text", text ? "1" : "0"},
         {"formats", num(static_cast<std::size_t>(e->mimeData()->formats().size()))}});
  if (!acceptDrops() || read_only_ || !text) {
    e->ignore();
    return;
  }
  e->acceptProposedAction();
  drop_pos_ = hit_test(e->position() + QPointF(0, scroll_y()));
  viewport()->update();
}

void SourceEditor::dragMoveEvent(QDragMoveEvent* e) {
  if (!acceptDrops() || read_only_ || !drop_mime_text(*e->mimeData())) {
    e->ignore();
    return;
  }
  e->acceptProposedAction();
  drop_pos_ = hit_test(e->position() + QPointF(0, scroll_y()));
  viewport()->update();
}

void SourceEditor::dragLeaveEvent(QDragLeaveEvent*) {
  drop_pos_ = SIZE_MAX;
  viewport()->update();
}

void SourceEditor::dropEvent(QDropEvent* e) {
  drop_pos_ = SIZE_MAX;
  viewport()->update();
  if (!acceptDrops() || read_only_) {
    e->ignore();
    return;
  }
  const std::size_t pos = hit_test(e->position() + QPointF(0, scroll_y()));
  if (drag_source_) {
    const bool copy = e->modifiers() & Qt::ControlModifier;
    const std::size_t bytes = drag_source_->end - drag_source_->start;
    if (!apply_command(drop_text(doc_, *drag_source_, pos, !copy))) {
      e->ignore();
      return;
    }
    e->acceptProposedAction();
    log(LogLevel::debug, kComp, "drop", {{"kind", copy ? "copy" : "move"}, {"bytes", num(bytes)}});
    return;
  }
  const auto text = drop_mime_text(*e->mimeData());
  if (!text) {
    e->ignore();
    return;
  }
  const CommandResult r{{Edit{pos, 0, *text}}, {pos, pos + text->size()}};
  if (!apply_command(r)) {
    e->ignore();
    return;
  }
  e->acceptProposedAction();
  log(LogLevel::debug, kComp, "drop", {{"kind", "external"}, {"bytes", num(text->size())}});
}

void SourceEditor::scrollContentsBy(int, int) { viewport()->update(); }

void SourceEditor::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  ensure_layout();
  update_scrollbar();
}

void SourceEditor::changeEvent(QEvent* e) {
  QAbstractScrollArea::changeEvent(e);
  if (e->type() == QEvent::FontChange || e->type() == QEvent::PaletteChange) {
    if (debug_on())
      log(LogLevel::debug, kComp, e->type() == QEvent::FontChange ? "font_change" : "palette_change",
          {{"custom_styles", custom_styles_ ? "1" : "0"}});
    if (!custom_styles_) {
      use_default_styles();
    } else if (e->type() == QEvent::FontChange) {
      for (Line& l : lines_) l.layout.reset();
      layout_dirty_ = true;
      viewport()->update();
    }
  }
}

void SourceEditor::paintEvent(QPaintEvent*) {
  ensure_layout();
  QPainter p(viewport());
  const int top = scroll_y();
  const int view_h = viewport()->height();
  p.fillRect(viewport()->rect(), palette().base());
  p.fillRect(viewport()->rect(), styles_.outside_background);
  const int page_x = page_left();
  const int text_x = page_x + styles_.page_padding;
  const int text_w = layout_width();
  const int page_w = text_w + 2 * styles_.page_padding;
  const int page_top = styles_.page_margin - top;
  const int page_bottom = std::max(total_height_ - styles_.page_margin - top, view_h);
  QBrush page = styles_.page_background;
  page.setTransform(QTransform::fromTranslate(page_x, -top));  
  p.fillRect(QRect(page_x, page_top, page_w, page_bottom - page_top), page);
  const int gutter_w = gutter_width();
  if (gutter_w > 0)
    p.fillRect(QRect(page_x - gutter_w, page_top, gutter_w, page_bottom - page_top), styles_.gutter_background);
  p.setFont(font());
  const QFontMetricsF number_metrics(font());
  const qreal number_pad = average_char_width(number_metrics);

  const std::size_t lo = std::min(cursor_, anchor_);
  const std::size_t hi = std::max(cursor_, anchor_);
  QTextCharFormat sel_format;
  sel_format.setBackground(palette().highlight());
  sel_format.setForeground(palette().highlightedText());
  const qreal newline_w = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
  const bool caret = caret_on_ && hasFocus();
  const std::size_t caret_line = line_index(cursor_);

  for (std::size_t i = line_at_y(top); i < lines_.size(); ++i) {
    const Line& l = lines_[i];
    if (l.y - top > view_h) break;
    const QPointF origin(text_x, l.y - top);
    if (l.background)
      p.fillRect(QRectF(origin.x(), origin.y(), text_w, l.text_height), styles_.code_line_background);
    if (gutter_w > 0) {
      const QTextLine first = l.layout->lineAt(0);
      const QString number = QString::number(i + 1);
      const QPen text_pen = p.pen();
      p.setPen(styles_.gutter_text);
      p.drawText(QPointF(page_x - number_pad - number_metrics.horizontalAdvance(number),
                         origin.y() + first.y() + first.ascent()),
                 number);
      p.setPen(text_pen);
    }

    QList<QTextLayout::FormatRange> selections;
    if (lo < hi && lo <= l.span.text_end() && hi > l.span.start) {
      const int a = to_utf16(l, lo);
      const int b = to_utf16(l, hi);
      if (b > a) selections.append(QTextLayout::FormatRange{a, b - a, sel_format});
      if (hi > l.span.text_end() && l.span.ending > 0) {
        const QTextLine last = l.layout->lineAt(l.layout->lineCount() - 1);
        p.fillRect(QRectF(origin.x() + last.x() + last.naturalTextWidth(), origin.y() + last.y(), newline_w,
                          last.height()),
                   palette().highlight());
      }
    }
    l.layout->draw(&p, origin, selections);
    for (const auto& [rect, img] : image_boxes(l)) {
      const QRect box(static_cast<int>(origin.x()) + rect.x(), static_cast<int>(origin.y()) + rect.y(),
                      rect.width(), rect.height());
      const auto it = images_.find(img->url);
      if (it != images_.end() && it->second.state == ImageState::ready) {
        ImageEntry& e = it->second;
        const qreal dpr = devicePixelRatioF();
        if (e.scaled_size != box.size() || e.scaled.isNull() || e.scaled.devicePixelRatio() != dpr) {
          QImage scaled_image = e.image.scaled(box.size() * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          scaled_image.setDevicePixelRatio(dpr);
          e.scaled = QPixmap::fromImage(scaled_image);
          e.scaled_size = box.size();
        }
        p.drawPixmap(box.topLeft(), e.scaled);
      } else {
        const bool failed = it != images_.end() && it->second.state == ImageState::failed;
        const QTextCharFormat& fmt = failed ? styles_.image_failed : styles_.image_pending;
        const QColor fg = fmt.foreground().color();
        p.setPen(fg);
        p.drawRect(box.adjusted(0, 0, -1, -1));
        QFont box_font = font();
        box_font.setStrikeOut(fmt.fontStrikeOut());
        p.setFont(box_font);
        const QString label = img->alt.isEmpty() ? QString::fromStdString(img->url) : img->alt;
        p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        p.setFont(font());
      }
      if (lo <= img->start && img->end <= hi) {
        QColor hl = palette().highlight().color();
        hl.setAlpha(100);
        p.fillRect(box, hl);
      }
    }
    if (caret && i == caret_line) {
      int pos = to_utf16(l, cursor_);
      if (i == preedit_line_) {
        if (!preedit_cursor_visible_) continue;
        pos += preedit_cursor_;
      }
      l.layout->drawCursor(&p, origin, pos, 1);
    }
    if (drop_pos_ != SIZE_MAX && i == line_index(drop_pos_)) {
      l.layout->drawCursor(&p, origin, to_utf16(l, drop_pos_), 1);
    }
  }
}

}  
