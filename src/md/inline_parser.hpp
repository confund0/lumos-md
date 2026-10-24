#pragma once

#include <memory>
#include <vector>

#include "md/content.hpp"
#include "md/features.hpp"
#include "md/ref_index.hpp"
#include "lumos/syntax.hpp"

namespace lumos::md {

struct InlineNode {
  Node node;  
};

class InlineParser {
 public:
  explicit InlineParser(const Features& features);
  ~InlineParser();
  InlineParser(const InlineParser&) = delete;
  InlineParser& operator=(const InlineParser&) = delete;

  void parse(const ContentBuffer& content, LabelLookup& lookup, std::vector<InlineNode>& out);

 private:
  struct Buffers;
  Features features_;
  std::unique_ptr<Buffers> buffers_;
};

}  
