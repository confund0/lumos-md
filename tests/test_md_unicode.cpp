
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "md/entities.hpp"
#include "md/unicode.hpp"

namespace md = lumos::md;

namespace {

std::string fold(char32_t cp) {
  std::string s;
  md::append_case_fold(cp, s);
  return s;
}

std::string utf8(char32_t cp) {
  std::string s;
  md::append_utf8(cp, s);
  return s;
}

std::string entity(std::string_view s, std::size_t* len = nullptr) {
  std::string out;
  const std::size_t n = md::scan_entity(s, 0, &out);
  if (len) *len = n;
  return out;
}

}  

TEST_CASE("md unicode: decode_utf8") {
  using std::string_view;
  CHECK(md::decode_utf8("a", 0).cp == U'a');
  CHECK(md::decode_utf8("a", 0).len == 1);
  CHECK(md::decode_utf8("", 0).len == 0);
  const string_view s = "\xC3\xA4\xE6\x97\xA5\xF0\x9F\x98\x80";
  CHECK(md::decode_utf8(s, 0).cp == 0xE4);
  CHECK(md::decode_utf8(s, 0).len == 2);
  CHECK(md::decode_utf8(s, 2).cp == 0x65E5);
  CHECK(md::decode_utf8(s, 2).len == 3);
  CHECK(md::decode_utf8(s, 5).cp == 0x1F600);
  CHECK(md::decode_utf8(s, 5).len == 4);
  for (string_view bad : {string_view("\xC0\x80"), string_view("\xED\xA0\x80"), string_view("\xE6\x97"),
                          string_view("\xF5\x80\x80\x80"), string_view("\x80")}) {
    const md::Decoded d = md::decode_utf8(bad, 0);
    CHECK(d.cp == 0xFFFD);
    CHECK(d.len == 1);
  }
  CHECK(md::decode_utf8_before(s, 2).cp == 0xE4);
  CHECK(md::decode_utf8_before(s, 5).cp == 0x65E5);
  CHECK(md::decode_utf8_before(s, 9).cp == 0x1F600);
  CHECK(md::decode_utf8_before(s, 9).len == 4);
  CHECK(md::decode_utf8_before("a\x80", 2).cp == 0xFFFD);
  CHECK(md::decode_utf8_before("ab", 0).len == 0);
}

TEST_CASE("md unicode: append_utf8") {
  CHECK(utf8(U'a') == "a");
  CHECK(utf8(0xE4) == "\xC3\xA4");
  CHECK(utf8(0x1F600) == "\xF0\x9F\x98\x80");
  CHECK(utf8(0xD800) == "\xEF\xBF\xBD");
  CHECK(utf8(0x110000) == "\xEF\xBF\xBD");
}

TEST_CASE("md unicode: whitespace and punctuation") {
  for (char32_t c : {U' ', U'\t', U'\n', U'\f', U'\r', char32_t{0xA0}, char32_t{0x3000}, char32_t{0x2009}})
    CHECK(md::is_unicode_whitespace(c));
  for (char32_t c : {U'a', U'\v', char32_t{0x200B}, char32_t{0x85}}) CHECK_FALSE(md::is_unicode_whitespace(c));
  for (char32_t c : {U'!', U'$', U'~', char32_t{0xA2}, char32_t{0x2014}, char32_t{0x20AC}, char32_t{0x3001},
                     char32_t{0x1F600}})
    CHECK(md::is_unicode_punctuation(c));
  for (char32_t c : {U'a', U'0', U' ', char32_t{0xAA}, char32_t{0x65E5}}) CHECK_FALSE(md::is_unicode_punctuation(c));
  for (char32_t c : {U'!', U'$', U'~', char32_t{0xA1}, char32_t{0x2014}, char32_t{0x3001}})
    CHECK(md::is_autolink_punctuation(c));
  for (char32_t c : {U'a', U' ', char32_t{0xA2}, char32_t{0x20AC}, char32_t{0x1F600}, char32_t{0x65E5}})
    CHECK_FALSE(md::is_autolink_punctuation(c));
  CHECK(md::unicode_category_version() == "15.0.0");
  CHECK(md::unicode_case_fold_version() == "17.0.0");
}

TEST_CASE("md unicode: full case folding") {
  CHECK(fold(U'A') == "a");
  CHECK(fold(U'z') == "z");
  CHECK(fold(0x1E9E) == "ss");          
  CHECK(fold(0xDF) == "ss");            
  CHECK(fold(0x3A3) == "\xCF\x83");     
  CHECK(fold(0x130) == "i\xCC\x87");    
  CHECK(fold(0x65E5) == "\xE6\x97\xA5");  
}

TEST_CASE("md entities: named and numeric references") {
  md::EntityValue v;
  REQUIRE(md::lookup_entity("amp", v));
  CHECK(v.first == U'&');
  CHECK(v.second == 0);
  REQUIRE(md::lookup_entity("ngE", v));
  CHECK(v.first == 0x2267);
  CHECK(v.second == 0x338);
  CHECK(md::lookup_entity("CounterClockwiseContourIntegral", v));
  CHECK_FALSE(md::lookup_entity("nosuch", v));
  CHECK(md::lookup_entity("AMP", v));  
  CHECK_FALSE(md::lookup_entity("aMp", v));

  std::size_t n = 0;
  CHECK(entity("&amp;", &n) == "&");
  CHECK(n == 5);
  CHECK(entity("&copy;x") == "\xC2\xA9");
  CHECK(entity("&ngE;") == "\xE2\x89\xA7\xCC\xB8");
  CHECK(entity("&#35;") == "#");
  CHECK(entity("&#X22;") == "\"");
  CHECK(entity("&#x22;") == "\"");
  CHECK(entity("&#0;") == "\xEF\xBF\xBD");
  CHECK(entity("&#1234567;") == "\xEF\xBF\xBD");
  CHECK(entity("&#xD800;") == "\xEF\xBF\xBD");
  CHECK(entity("&#x110000;") == "\xEF\xBF\xBD");
  for (std::string_view no : {"&amp", "&#12345678;", "&#x1234567;", "&#;", "&#x;", "&nosuch;", "&;", "amp;"}) {
    CHECK(entity(no, &n).empty());
    CHECK(n == 0);
  }
}
