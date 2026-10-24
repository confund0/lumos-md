#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "md/content.hpp"

namespace lumos::md {

std::string normalize_label(std::string_view label);

using LabelId = std::uint32_t;

class RefIndex {
 public:
  LabelId intern(std::string_view normalized);
  bool find(std::string_view normalized, LabelId& out) const;
  std::string_view label(LabelId id) const;

  bool is_defined(LabelId id) const noexcept { return id < counts_.size() && counts_[id] > 0; }

  struct Definition {
    std::size_t start = 0;
    LabelId label = 0;
  };
  void replace_definitions(std::size_t start, std::size_t old_end, std::size_t new_end,
                           std::span<const Definition> fresh, std::vector<LabelId>& flipped);

  std::span<const Definition> definitions() const noexcept { return definitions_; }

  void definitions_in(std::size_t start, std::size_t end, std::ptrdiff_t shift,
                      std::vector<Definition>& out) const;

  struct LeafQueries {
    std::size_t start = 0;          
    std::vector<LabelId> labels;    
    std::vector<Segment> segments;  
  };

  void replace_leaves(std::size_t start, std::size_t old_end, std::size_t new_end, std::vector<LeafQueries> fresh);

  void leaves_in(std::size_t start, std::size_t end, std::ptrdiff_t shift, std::vector<LeafQueries>& out) const;

  std::span<const LeafQueries> leaves() const noexcept { return leaves_; }  

  void dependents(std::span<const LabelId> flipped, std::size_t skip_start, std::size_t skip_end,
                  std::vector<std::size_t>& out) const;

  void set_leaf_labels(std::size_t start, std::vector<LabelId> labels);

  std::size_t label_count() const noexcept { return labels_.size(); }

  bool maybe_compact();

  void clear();
  void wipe();

 private:
  bool is_in_use(LabelId id) const noexcept { return counts_[id] > 0 || leaf_refs_[id] > 0; }
  void inc_leaf_ref(LabelId id);
  void dec_leaf_ref(LabelId id);

  std::unordered_map<std::string, LabelId> ids_;
  std::vector<std::string> labels_;      
  std::vector<std::uint32_t> counts_;    
  std::vector<std::uint32_t> leaf_refs_; 
  std::size_t in_use_count_ = 0;         
  std::vector<Definition> definitions_;
  std::vector<LeafQueries> leaves_;
};

class LabelLookup {
 public:
  explicit LabelLookup(RefIndex& index) noexcept : index_(&index) {}

  bool is_defined(std::string_view label);

  std::vector<LabelId> take();

 private:
  RefIndex* index_;
  std::vector<LabelId> asked_;
  std::string scratch_;  
};

}  
