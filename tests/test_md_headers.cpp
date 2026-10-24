
#include <catch2/catch_test_macros.hpp>

#include "md/content.hpp"
#include "md/inline_parser.hpp"
#include "md/link_syntax.hpp"
#include "md/ref_index.hpp"

namespace md = lumos::md;

TEST_CASE("md headers: drafts compile") {
  md::Segment s;
  s.start = 2;
  s.end = 5;
  CHECK(s.size() == 3);
  CHECK(md::max_label_bytes == 1000);
}
