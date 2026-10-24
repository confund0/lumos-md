#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "lumos/buffer.hpp"
#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"
#include "lumos/types.hpp"

namespace lumos {

enum class ChangeKind : std::uint8_t { load, edit, undo, redo };

struct Change {
  ChangeKind kind = ChangeKind::edit;
  std::span<const Edit> edits;
  std::span<const TextRegion> tree_changes;
  std::uint64_t version = 0;
};

class Document {
 public:
  using Listener = std::function<void(const Change&)>;
  using ListenerId = std::uint64_t;

  explicit Document(const Dialect& dialect = {});
  explicit Document(std::string text, const Dialect& dialect = {});
  ~Document();
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  void load(std::string text);

  void wipe();

  std::string text() const { return buffer_.text(); }
  std::size_t size() const { return buffer_.size(); }
  const Buffer& buffer() const { return buffer_; }
  const SyntaxTree& tree() const { return parser_->tree(); }
  const Dialect& dialect() const { return parser_->dialect(); }

  Edit apply(const Edit& e);

  void begin();
  void commit();
  bool in_transaction() const { return buffer_.in_transaction(); }

  std::vector<Edit> undo();
  std::vector<Edit> redo();
  bool can_undo() const { return buffer_.can_undo(); }
  bool can_redo() const { return buffer_.can_redo(); }
  void set_max_undo(std::size_t steps);

  std::uint64_t version() const { return version_; }

  void mark_saved();
  bool modified() const;

  ListenerId add_listener(Listener listener);
  void remove_listener(ListenerId id);

 private:
  void follow(std::span<const Edit> edits);
  void push_id(std::uint64_t id);
  void notify(ChangeKind kind, std::span<const Edit> edits);
  std::uint64_t state() const { return undo_ids_.empty() ? base_id_ : undo_ids_.back(); }

  Buffer buffer_;
  std::unique_ptr<Parser> parser_;
  std::uint64_t version_ = 0;

  std::deque<std::uint64_t> undo_ids_;
  std::uint64_t base_id_ = 0;
  std::vector<std::uint64_t> redo_ids_;
  std::uint64_t next_id_ = 1;
  std::uint64_t saved_id_ = 0;
  bool open_edits_ = false;  

  std::vector<std::pair<ListenerId, std::shared_ptr<Listener>>> listeners_;
  ListenerId next_listener_ = 1;
};

}  
