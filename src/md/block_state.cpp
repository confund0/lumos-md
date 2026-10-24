#include "md/block_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace lumos::md {

namespace {

std::size_t mix(std::size_t h, std::size_t v) noexcept {
  return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

}  

std::size_t StateArena::Hash::operator()(const OpenBlock& b) const noexcept {
  std::size_t h = static_cast<std::size_t>(b.kind);
  h = mix(h, static_cast<unsigned char>(b.marker));
  h = mix(h, b.small);
  h = mix(h, b.width);
  h = mix(h, b.parent);
  return h;
}

StateArena::StateArena() {
  OpenBlock doc;
  doc.kind = OpenKind::document;
  doc.parent = no_state;
  entries_.push_back(doc);
  index_.emplace(doc, document_state);
}

StateId StateArena::intern(const OpenBlock& block) {
  auto it = index_.find(block);
  if (it != index_.end()) return it->second;
  if (block.parent >= entries_.size()) throw std::invalid_argument("StateArena::intern: bad parent");
  OpenBlock e = block;
  const OpenBlock& p = entries_[block.parent];
  e.depth = p.depth + 1;
  const auto id = static_cast<StateId>(entries_.size());
  entries_.push_back(e);
  index_.emplace(e, id);
  return id;
}

StateId StateArena::push(StateId parent, OpenBlock block) {
  block.parent = parent;
  return intern(block);
}

StateId StateArena::with_flags(StateId id, std::uint8_t small) {
  OpenBlock b = entries_.at(id);
  b.small = small;
  return intern(b);
}

std::vector<StateId> StateArena::compact(std::span<const StateId> live) {
  const std::size_t n = entries_.size();
  std::vector<char> keep(n, 0);
  keep[document_state] = 1;
  for (StateId s : live) {
    for (StateId e = s; e != no_state && !keep[e]; e = entries_[e].parent) keep[e] = 1;
  }
  std::vector<StateId> map(n, no_state);
  std::vector<OpenBlock> kept;
  for (std::size_t i = 0; i < n; ++i) {
    if (!keep[i]) continue;
    OpenBlock e = entries_[i];
    if (e.parent != no_state) e.parent = map[e.parent];
    map[i] = static_cast<StateId>(kept.size());
    kept.push_back(e);
  }
  entries_ = std::move(kept);
  index_.clear();
  for (std::size_t i = 0; i < entries_.size(); ++i) index_.emplace(entries_[i], static_cast<StateId>(i));
  return map;
}

std::size_t LineTable::line_of(std::size_t offset) const noexcept {
  auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
  if (it == starts_.begin()) return 0;
  return static_cast<std::size_t>(it - starts_.begin()) - 1;
}

void LineTable::clear() noexcept {
  starts_.clear();
  states_.clear();
}

void LineTable::push(std::size_t start, StateId state) {
  starts_.push_back(start);
  states_.push_back(state);
}

void LineTable::replace(std::size_t first, std::size_t last, std::span<const std::size_t> starts,
                        std::span<const StateId> states, std::ptrdiff_t delta) {
  if (first > last || last > starts_.size() || starts.size() != states.size())
    throw std::invalid_argument("LineTable::replace: bad range");
  for (std::size_t i = last; i < starts_.size(); ++i)
    starts_[i] = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(starts_[i]) + delta);
  const auto f = static_cast<std::ptrdiff_t>(first);
  const auto l = static_cast<std::ptrdiff_t>(last);
  starts_.erase(starts_.begin() + f, starts_.begin() + l);
  states_.erase(states_.begin() + f, states_.begin() + l);
  starts_.insert(starts_.begin() + f, starts.begin(), starts.end());
  states_.insert(states_.begin() + f, states.begin(), states.end());
}

void LineTable::remap_states(std::span<const StateId> old_to_new) {
  for (StateId& s : states_) s = old_to_new[s];
}

}  
