#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "lumos/syntax.hpp"

namespace lumos::md {

using StateId = std::uint32_t;
inline constexpr StateId no_state = UINT32_MAX;
inline constexpr StateId document_state = 0;

enum class OpenKind : std::uint8_t {
  document,
  block_quote,
  list,
  list_item,
  paragraph,      
  fenced_code,
  indented_code,
  html_block,
  table,          
  front_matter,   
};

enum ItemFlag : std::uint8_t {
  item_empty = 1,
};

enum ListFlag : std::uint8_t {
  list_blank = 1,
};

struct OpenBlock {
  OpenKind kind = OpenKind::document;
  char marker = 0;
  std::uint8_t small = 0;
  std::uint32_t width = 0;
  StateId parent = no_state;

  std::uint32_t depth = 0;

  bool same_identity(const OpenBlock& o) const noexcept {
    return kind == o.kind && marker == o.marker && small == o.small && width == o.width && parent == o.parent;
  }
};

class StateArena {
 public:
  StateArena();

  StateId intern(const OpenBlock& block);
  const OpenBlock& get(StateId id) const { return entries_[id]; }

  StateId push(StateId parent, OpenBlock block);         
  StateId pop(StateId id) const { return get(id).parent; }
  StateId with_flags(StateId id, std::uint8_t small);    

  bool has_paragraph(StateId id) const { return get(id).kind == OpenKind::paragraph; }

  std::vector<StateId> compact(std::span<const StateId> live);

  std::size_t size() const noexcept { return entries_.size(); }

 private:
  struct Hash {
    std::size_t operator()(const OpenBlock& b) const noexcept;
  };
  struct Same {
    bool operator()(const OpenBlock& a, const OpenBlock& b) const noexcept { return a.same_identity(b); }
  };
  std::vector<OpenBlock> entries_;
  std::unordered_map<OpenBlock, StateId, Hash, Same> index_;
};

class LineTable {
 public:
  std::size_t size() const noexcept { return starts_.size(); }
  std::size_t start(std::size_t line) const { return starts_[line]; }
  StateId state(std::size_t line) const { return states_[line]; }

  std::size_t line_of(std::size_t offset) const noexcept;

  void clear() noexcept;
  void push(std::size_t start, StateId state);

  void replace(std::size_t first, std::size_t last, std::span<const std::size_t> starts,
               std::span<const StateId> states, std::ptrdiff_t delta);

  std::span<const StateId> states() const noexcept { return states_; }
  void remap_states(std::span<const StateId> old_to_new);

 private:
  std::vector<std::size_t> starts_;
  std::vector<StateId> states_;
};

}  
