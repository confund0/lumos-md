#include "lumos/document.hpp"

#include <algorithm>
#include <stdexcept>

#include "lumos/log.hpp"

namespace lumos {
namespace {

constexpr std::string_view kComp = "document";

std::string num(std::uint64_t v) { return std::to_string(v); }

bool debug_on() { return log_enabled(LogLevel::debug); }

std::string_view kind_text(ChangeKind kind) {
  switch (kind) {
    case ChangeKind::load: return "load";
    case ChangeKind::edit: return "edit";
    case ChangeKind::undo: return "undo";
    case ChangeKind::redo: return "redo";
  }
  return "unknown";
}

}  

Document::Document(const Dialect& dialect) : Document(std::string{}, dialect) {}

Document::Document(std::string text, const Dialect& dialect)
    : buffer_(std::move(text)), parser_(make_parser(dialect)) {
  parser_->parse(buffer_);
  if (debug_on()) log(LogLevel::debug, kComp, "create", {{"bytes", num(buffer_.size())}});
}

Document::~Document() = default;

void Document::load(std::string text) {
  if (buffer_.in_transaction()) {
    log(LogLevel::debug, kComp, "load_in_transaction");
    throw std::logic_error("Document::load: transaction is open");
  }
  const std::size_t max_undo = buffer_.max_undo();
  buffer_ = Buffer(std::move(text));
  buffer_.set_max_undo(max_undo);
  parser_->parse(buffer_);
  undo_ids_.clear();
  redo_ids_.clear();
  base_id_ = 0;
  saved_id_ = 0;
  open_edits_ = false;
  ++version_;
  if (debug_on())
    log(LogLevel::debug, kComp, "load", {{"bytes", num(buffer_.size())}, {"nodes", num(tree().size())}});
  notify(ChangeKind::load, {});
}

void Document::wipe() {
  if (buffer_.in_transaction()) {
    log(LogLevel::debug, kComp, "wipe_in_transaction");
    throw std::logic_error("Document::wipe: transaction is open");
  }
  buffer_.wipe();
  parser_->wipe();
  undo_ids_.clear();
  redo_ids_.clear();
  base_id_ = 0;
  saved_id_ = 0;
  open_edits_ = false;
  ++version_;
  log(LogLevel::debug, kComp, "wipe");
  notify(ChangeKind::load, {});
}

void Document::follow(std::span<const Edit> edits) {
  if (edits.size() > 1 && debug_on())
    log(LogLevel::debug, kComp, "edits_merged", {{"edits", num(edits.size())}});
  parser_->apply_edit(merge_edits(edits), buffer_);
}

void Document::push_id(std::uint64_t id) {
  undo_ids_.push_back(id);
  while (undo_ids_.size() > buffer_.undo_steps()) {
    base_id_ = undo_ids_.front();
    undo_ids_.pop_front();
  }
}

void Document::set_max_undo(std::size_t steps) {
  buffer_.set_max_undo(steps);
  while (undo_ids_.size() > buffer_.undo_steps()) {
    base_id_ = undo_ids_.front();
    undo_ids_.pop_front();
  }
}

Edit Document::apply(const Edit& e) {
  const std::uint64_t before = buffer_.version();
  Edit inverse = buffer_.apply(e);
  if (buffer_.version() == before) return inverse;  
  follow(std::span<const Edit>(&e, 1));
  redo_ids_.clear();
  if (buffer_.in_transaction()) {
    open_edits_ = true;
  } else {
    push_id(next_id_++);
  }
  ++version_;
  notify(ChangeKind::edit, std::span<const Edit>(&e, 1));
  return inverse;
}

void Document::begin() { buffer_.begin(); }

void Document::commit() {
  buffer_.commit();
  if (buffer_.in_transaction() || !open_edits_) return;
  push_id(next_id_++);
  open_edits_ = false;
}

std::vector<Edit> Document::undo() {
  std::vector<Edit> edits = buffer_.undo();
  if (edits.empty()) {
    if (debug_on()) log(LogLevel::debug, kComp, "undo_empty");
    return edits;
  }
  follow(edits);
  redo_ids_.push_back(undo_ids_.back());
  undo_ids_.pop_back();
  ++version_;
  notify(ChangeKind::undo, edits);
  return edits;
}

std::vector<Edit> Document::redo() {
  std::vector<Edit> edits = buffer_.redo();
  if (edits.empty()) {
    if (debug_on()) log(LogLevel::debug, kComp, "redo_empty");
    return edits;
  }
  follow(edits);
  const std::uint64_t id = redo_ids_.back();
  redo_ids_.pop_back();
  push_id(id);
  ++version_;
  notify(ChangeKind::redo, edits);
  return edits;
}

void Document::mark_saved() {
  if (buffer_.in_transaction()) {
    log(LogLevel::debug, kComp, "mark_saved_in_transaction");
    throw std::logic_error("Document::mark_saved: transaction is open");
  }
  saved_id_ = state();
  if (debug_on()) log(LogLevel::debug, kComp, "mark_saved", {{"version", num(version_)}});
}

bool Document::modified() const { return open_edits_ || state() != saved_id_; }

Document::ListenerId Document::add_listener(Listener listener) {
  const ListenerId id = next_listener_++;
  listeners_.emplace_back(id, std::make_shared<Listener>(std::move(listener)));
  return id;
}

void Document::remove_listener(ListenerId id) {
  std::erase_if(listeners_, [id](const auto& entry) { return entry.first == id; });
}

void Document::notify(ChangeKind kind, std::span<const Edit> edits) {
  if (debug_on())
    log(LogLevel::debug, kComp, "change",
        {{"kind", kind_text(kind)}, {"edits", num(edits.size())}, {"version", num(version_)},
         {"bytes", num(buffer_.size())}, {"tree_regions", num(parser_->changes().size())}});
  if (listeners_.empty()) return;
  const auto current = listeners_;
  const Change change{kind, edits, parser_->changes(), version_};
  for (const auto& entry : current) {
    if (*entry.second) (*entry.second)(change);
  }
}

}  
