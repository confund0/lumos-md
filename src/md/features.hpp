#pragma once

#include "lumos/parser.hpp"

namespace lumos::md {

struct Features {
  bool tables = true;              
  bool strikethrough = true;       
  bool task_lists = true;          
  bool extended_autolinks = true;  
  bool front_matter = true;        
  bool math = false;               

  static constexpr Features commonmark() noexcept { return {false, false, false, false, false, false}; }
  static constexpr Features gfm() noexcept { return {true, true, true, true, false, false}; }

  static Features from(const Dialect& d) noexcept {
    Features f;
    f.tables = d.tables;
    f.strikethrough = d.strikethrough;
    f.task_lists = d.task_lists;
    f.extended_autolinks = d.extended_autolinks;
    f.front_matter = d.front_matter;
    f.math = d.math;
    return f;
  }

  bool operator==(const Features&) const = default;
};

}  
