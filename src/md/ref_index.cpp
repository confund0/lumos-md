#include "md/ref_index.hpp"

#include <algorithm>

#include "md/unicode.hpp"
#include "wipe.hpp"

namespace lumos::md {

std::string normalize_label(std::string_view label) {
  std::string folded;
  folded.reserve(label.size());
  std::size_t i = 0;
  while (i < label.size()) {
    const auto c = static_cast<unsigned char>(label[i]);
    if (c != 0 && c < 0x80) {
      folded += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
      ++i;
      continue;
    }
    const Decoded d = decode_utf8(label, i);
    append_case_fold(d.cp == 0 ? 0xFFFD : d.cp, folded);
    i += d.len != 0 ? d.len : 1;
  }

  auto is_space = [](char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
  };
  std::size_t b = 0, e = folded.size();
  while (b < e && is_space(folded[b])) ++b;
  while (e > b && is_space(folded[e - 1])) --e;

  std::string out;
  out.reserve(e - b);
  bool last_space = false;
  for (std::size_t k = b; k < e; ++k) {
    if (is_space(folded[k])) {
      if (!last_space) out += ' ';
      last_space = true;
    } else {
      out += folded[k];
      last_space = false;
    }
  }
  return out;
}

LabelId RefIndex::intern(std::string_view normalized) {
  if (const auto it = ids_.find(std::string(normalized)); it != ids_.end()) return it->second;
  const auto id = static_cast<LabelId>(labels_.size());
  labels_.emplace_back(normalized);
  counts_.push_back(0);
  leaf_refs_.push_back(0);
  ids_.emplace(labels_.back(), id);
  return id;
}

bool RefIndex::find(std::string_view normalized, LabelId& out) const {
  const auto it = ids_.find(std::string(normalized));
  if (it == ids_.end()) return false;
  out = it->second;
  return true;
}

std::string_view RefIndex::label(LabelId id) const {
  return id < labels_.size() ? std::string_view(labels_[id]) : std::string_view();
}

void RefIndex::replace_definitions(std::size_t start, std::size_t old_end, std::size_t new_end,
                                   std::span<const Definition> fresh, std::vector<LabelId>& flipped) {
  auto by_start = [](const Definition& d, std::size_t v) noexcept { return d.start < v; };
  const auto first = std::lower_bound(definitions_.begin(), definitions_.end(), start, by_start);
  const auto last = std::lower_bound(definitions_.begin(), definitions_.end(), old_end, by_start);
  const std::size_t base = flipped.size();
  for (auto it = first; it != last; ++it) {
    if (it->label < counts_.size() && counts_[it->label] > 0) {
      if (--counts_[it->label] == 0) {
        flipped.push_back(it->label);
        if (leaf_refs_[it->label] == 0) --in_use_count_;
      }
    }
  }
  const auto delta = static_cast<std::ptrdiff_t>(new_end) - static_cast<std::ptrdiff_t>(old_end);
  for (auto it = last; it != definitions_.end(); ++it) {
    it->start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(it->start) + delta);
  }
  const auto removed_begin = flipped.begin() + static_cast<std::ptrdiff_t>(base);
  std::sort(removed_begin, flipped.end());
  const std::size_t removed_end = flipped.size();
  std::vector<LabelId> back;  
  for (const Definition& d : fresh) {
    if (d.label >= counts_.size()) continue;
    const std::uint32_t before = counts_[d.label]++;
    if (before != 0) continue;
    if (leaf_refs_[d.label] == 0) ++in_use_count_;
    const auto rb = flipped.begin() + static_cast<std::ptrdiff_t>(base);
    const auto re = flipped.begin() + static_cast<std::ptrdiff_t>(removed_end);
    if (std::binary_search(rb, re, d.label)) {
      back.push_back(d.label);
    } else {
      flipped.push_back(d.label);
    }
  }
  if (!back.empty()) {
    std::sort(back.begin(), back.end());
    const auto rb = flipped.begin() + static_cast<std::ptrdiff_t>(base);
    const auto re = flipped.begin() + static_cast<std::ptrdiff_t>(removed_end);
    const auto kept = std::remove_if(rb, re, [&](LabelId id) { return std::binary_search(back.begin(), back.end(), id); });
    flipped.erase(kept, re);
  }
  const auto gap = definitions_.erase(first, last);
  definitions_.insert(gap, fresh.begin(), fresh.end());
}

void RefIndex::definitions_in(std::size_t start, std::size_t end, std::ptrdiff_t shift,
                              std::vector<Definition>& out) const {
  auto by_start = [](const Definition& d, std::size_t v) noexcept { return d.start < v; };
  const auto first = std::lower_bound(definitions_.begin(), definitions_.end(), start, by_start);
  const auto last = std::lower_bound(definitions_.begin(), definitions_.end(), end, by_start);
  for (auto it = first; it != last; ++it) {
    out.push_back({static_cast<std::size_t>(static_cast<std::ptrdiff_t>(it->start) + shift), it->label});
  }
}

void RefIndex::inc_leaf_ref(LabelId id) {
  const bool before = is_in_use(id);
  ++leaf_refs_[id];
  if (!before) ++in_use_count_;
}

void RefIndex::dec_leaf_ref(LabelId id) {
  --leaf_refs_[id];
  if (!is_in_use(id)) --in_use_count_;
}

void RefIndex::replace_leaves(std::size_t start, std::size_t old_end, std::size_t new_end,
                              std::vector<LeafQueries> fresh) {
  auto by_start = [](const LeafQueries& q, std::size_t v) noexcept { return q.start < v; };
  const auto first = std::lower_bound(leaves_.begin(), leaves_.end(), start, by_start);
  const auto last = std::lower_bound(leaves_.begin(), leaves_.end(), old_end, by_start);
  for (auto it = first; it != last; ++it)
    for (LabelId id : it->labels) dec_leaf_ref(id);
  const auto delta = static_cast<std::ptrdiff_t>(new_end) - static_cast<std::ptrdiff_t>(old_end);
  for (auto it = last; it != leaves_.end(); ++it) {
    it->start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(it->start) + delta);
  }
  for (const LeafQueries& q : fresh)
    for (LabelId id : q.labels) inc_leaf_ref(id);
  const auto gap = leaves_.erase(first, last);
  leaves_.insert(gap, std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
}

void RefIndex::leaves_in(std::size_t start, std::size_t end, std::ptrdiff_t shift,
                         std::vector<LeafQueries>& out) const {
  auto by_start = [](const LeafQueries& q, std::size_t v) noexcept { return q.start < v; };
  const auto first = std::lower_bound(leaves_.begin(), leaves_.end(), start, by_start);
  const auto last = std::lower_bound(leaves_.begin(), leaves_.end(), end, by_start);
  for (auto it = first; it != last; ++it) {
    LeafQueries q = *it;
    q.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(q.start) + shift);
    out.push_back(std::move(q));
  }
}

void RefIndex::dependents(std::span<const LabelId> flipped, std::size_t skip_start, std::size_t skip_end,
                          std::vector<std::size_t>& out) const {
  for (const LeafQueries& q : leaves_) {
    if (q.start >= skip_start && q.start < skip_end) continue;
    for (LabelId id : q.labels) {
      if (std::binary_search(flipped.begin(), flipped.end(), id)) {
        out.push_back(q.start);
        break;
      }
    }
  }
}

void RefIndex::set_leaf_labels(std::size_t start, std::vector<LabelId> labels) {
  auto by_start = [](const LeafQueries& q, std::size_t v) noexcept { return q.start < v; };
  const auto it = std::lower_bound(leaves_.begin(), leaves_.end(), start, by_start);
  if (it == leaves_.end() || it->start != start) return;
  for (LabelId id : it->labels) dec_leaf_ref(id);
  if (labels.empty()) {
    leaves_.erase(it);
    return;
  }
  for (LabelId id : labels) inc_leaf_ref(id);
  it->labels = std::move(labels);
}

bool RefIndex::maybe_compact() {
  if (labels_.size() <= 2 * in_use_count_ + 64) return false;
  std::vector<LabelId> remap(labels_.size(), 0);
  std::vector<std::string> new_labels;
  std::vector<std::uint32_t> new_counts;
  std::vector<std::uint32_t> new_leaf_refs;
  new_labels.reserve(in_use_count_);
  new_counts.reserve(in_use_count_);
  new_leaf_refs.reserve(in_use_count_);
  for (LabelId id = 0; id < labels_.size(); ++id) {
    if (counts_[id] == 0 && leaf_refs_[id] == 0) continue;
    remap[id] = static_cast<LabelId>(new_labels.size());
    new_labels.push_back(std::move(labels_[id]));
    new_counts.push_back(counts_[id]);
    new_leaf_refs.push_back(leaf_refs_[id]);
  }
  for (Definition& d : definitions_) d.label = remap[d.label];
  for (LeafQueries& q : leaves_) {
    for (LabelId& id : q.labels) id = remap[id];
    std::sort(q.labels.begin(), q.labels.end());
  }
  labels_ = std::move(new_labels);
  counts_ = std::move(new_counts);
  leaf_refs_ = std::move(new_leaf_refs);
  ids_.clear();
  for (LabelId id = 0; id < labels_.size(); ++id) ids_.emplace(labels_[id], id);
  in_use_count_ = labels_.size();
  return true;
}

bool LabelLookup::is_defined(std::string_view label) {
  scratch_ = normalize_label(label);
  const LabelId id = index_->intern(scratch_);
  asked_.push_back(id);
  return index_->is_defined(id);
}

std::vector<LabelId> LabelLookup::take() {
  std::sort(asked_.begin(), asked_.end());
  asked_.erase(std::unique(asked_.begin(), asked_.end()), asked_.end());
  return std::move(asked_);
}

void RefIndex::clear() {
  ids_.clear();
  labels_.clear();
  counts_.clear();
  leaf_refs_.clear();
  in_use_count_ = 0;
  definitions_.clear();
  leaves_.clear();
}

void RefIndex::wipe() {
  for (std::string& label : labels_) wipe_string(label);
  while (!ids_.empty()) {
    auto node = ids_.extract(ids_.begin());
    wipe_string(node.key());
  }
  clear();
}

}  
