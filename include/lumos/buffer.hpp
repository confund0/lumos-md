#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/text_source.hpp"
#include "lumos/types.hpp"

namespace lumos {

class Buffer final : public TextSource {
 public:
  static constexpr std::size_t default_max_undo = 10000;

  explicit Buffer(std::string text = {});

  std::size_t size() const override;
  std::string text() const;
  std::string slice(std::size_t offset, std::size_t length) const override;
  char at(std::size_t offset) const;
  std::string_view chunk_at(std::size_t offset) const override;

  Edit apply(const Edit& e);

  void begin();
  void commit();
  bool in_transaction() const;

  std::vector<Edit> undo();
  std::vector<Edit> redo();
  bool can_undo() const;
  bool can_redo() const;
  std::size_t undo_steps() const { return undo_.size(); }

  void set_max_undo(std::size_t steps);
  std::size_t max_undo() const { return max_undo_; }

  void wipe();

  std::uint64_t version() const;

  std::size_t line_count() const override;
  std::size_t line_start(std::size_t line) const override;
  std::size_t line_of(std::size_t offset) const override;

  std::size_t store_bytes() const { return stores_[0].bytes.size() + stores_[1].bytes.size(); }
  std::size_t piece_count() const;

 private:
  struct Store {
    std::string bytes;
    std::vector<std::size_t> rank{0};  

    void append(std::string_view s);
    void rebuild();
    std::size_t newlines_before(std::size_t pos) const;
    std::size_t find_newline(std::size_t pos, std::size_t k) const;
  };

  struct Piece {
    std::uint8_t store = 0;  
    std::size_t start = 0;
    std::size_t length = 0;
    std::size_t newlines = 0;
  };

  using Index = std::uint32_t;
  static constexpr Index nil = UINT32_MAX;

  struct TreeNode {
    Piece piece;
    Index left = nil;
    Index right = nil;
    std::uint32_t priority = 0;
    std::size_t bytes = 0;  
    std::size_t newlines = 0;
  };

  struct Step {
    std::vector<Edit> forward;  
    std::vector<Edit> inverse;  
  };

  Index make_node(const Piece& p);
  void free_tree(Index t);
  void update(Index t);
  std::size_t bytes_of(Index t) const { return t == nil ? 0 : nodes_[t].bytes; }
  std::size_t newlines_of(Index t) const { return t == nil ? 0 : nodes_[t].newlines; }
  Piece make_piece(std::uint8_t store, std::size_t start, std::size_t length) const;
  void split(Index t, std::size_t offset, Index& left, Index& right);
  Index merge(Index left, Index right);
  bool extend_last(Index t, std::size_t length, std::size_t newlines);
  void build(std::size_t length);
  template <typename Fn>
  void for_each_from(std::size_t offset, Fn fn) const;

  std::string_view piece_view(const Piece& p) const;
  void validate(const Edit& e) const;
  Edit apply_raw(const Edit& e);
  void push_step(Step step);
  void maybe_compact();
  void bump_version();

  Store stores_[2];
  std::vector<TreeNode> nodes_;
  std::vector<Index> free_;
  Index root_ = nil;
  std::uint32_t seed_ = 0x9E3779B9U;
  std::size_t size_ = 0;

  std::deque<Step> undo_;
  std::vector<Step> redo_;
  Step open_;  
  int depth_ = 0;
  std::size_t max_undo_ = default_max_undo;
  std::uint64_t version_ = 0;
};

}  
