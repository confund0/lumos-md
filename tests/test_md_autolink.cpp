
#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "md/autolink.hpp"

namespace md = lumos::md;

TEST_CASE("md autolink: host char") {
  struct Row {
    std::string_view s;
    std::size_t i;
    bool expected;
  };
  const Row rows[] = {
      {"a", 0, true},
      {" ", 0, false},
      {"\t", 0, false},
      {"\v", 0, true},    
      {"!", 0, false},    
      {"-", 0, false},    
      {"5", 0, true},
      {"\xE2\x82\xAC", 0, true},   
      {"\xC2\xA1", 0, false},      
      {"\xE2\x82\xAC", 1, false},  
      {"\x80", 0, false},          
      {"", 0, false},
      {"a", 1, false},
  };
  for (const Row& r : rows) {
    INFO(r.s << " @ " << r.i);
    CHECK(md::is_autolink_host_char(r.s, r.i) == r.expected);
  }
}

TEST_CASE("md autolink: domain") {
  struct Row {
    std::string_view rest;
    bool need_dot;
    std::size_t expected;
  };
  const Row rows[] = {
      {"www.a.b ", true, 7},
      {"www.a_b.c ", true, 0},           
      {"www.a.b_c ", true, 0},           
      {"www.a.b.c.d.e.f.g.h.i.j.k_l ", true, 27},  
      {"a ", false, 1},                  
      {"!bc.d ", true, 5},               
      {"a.b\\.c ", true, 6},             
      {"a.b\\_c ", true, 0},             
      {"abc ", false, 3},                
      {"abc ", true, 0},                 
  };
  for (const Row& r : rows) {
    INFO(r.rest);
    CHECK(md::autolink_domain(r.rest, r.need_dot) == r.expected);
  }
}

TEST_CASE("md autolink: trim") {
  struct Row {
    std::string_view link;
    std::size_t expected;
  };
  const Row rows[] = {
      {"abc", 3},
      {"abc.", 3},
      {"abc?!", 3},
      {"a(b)", 4},    
      {"a(b))", 4},   
      {"a(b", 3},     
      {"&amp;", 0},   
      {"x&amp;", 1},
      {"x&x;", 1},    
      {"x&;", 2},     
      {"x&1;", 3},    
      {"ab<cd", 2},   
      {"(a<b)", 2},   
      {"", 0},
      {";", 0},       
  };
  for (const Row& r : rows) {
    INFO(r.link);
    CHECK(md::autolink_trim(r.link) == r.expected);
  }
}

TEST_CASE("md autolink: www") {
  struct Row {
    std::string_view s;
    std::size_t i;
    std::size_t expected;
  };
  const Row rows[] = {
      {"www.a.b", 0, 7},
      {"a*www.a.b", 2, 7},
      {"a_www.a.b", 2, 7},
      {"a~www.a.b", 2, 7},
      {"a(www.a.b", 2, 7},
      {"a.www.a.b", 2, 0},     
      {"a\vwww.a.b", 2, 0},    
      {"a@www.b.c", 2, 0},     
      {" www.a.b", 1, 7},      
      {"x www.", 2, 0},        
      {"www.a.b<x", 0, 7},     
      {"www.\xE2\x82\xAC.a_b", 0, 11},    
      {"www.a_\xE2\x82\xAC.b", 0, 0},     
      {"www.a_b.c", 0, 0},
      {"[a](www.b.c", 4, 7},   
  };
  for (const Row& r : rows) {
    INFO(r.s << " @ " << r.i);
    CHECK(md::match_www(r.s, r.i) == r.expected);
  }
}

TEST_CASE("md autolink: url") {
  struct Row {
    std::string_view s;
    std::size_t colon;
    std::size_t start;
    std::size_t end;
  };
  const Row rows[] = {
      {"http://a.b", 4, 0, 10},
      {"xhttp://a.b", 5, 0, 0},            
      {"a.http://x.y", 6, 2, 12},
      {"HTTPS://a.b", 5, 0, 11},           
      {"http://a", 4, 0, 8},               
      {"http://\xE2\x82\xAC", 4, 0, 10},   
      {"http://\xC2\xA1", 4, 0, 0},        
      {"http://a.b_c", 4, 0, 0},
      {"xhttp://", 5, 0, 0},
      {"ftp://a.b", 3, 0, 9},
      {"http://a.b&amp;", 4, 0, 10},       
      {"http://a.b(x))", 4, 0, 13},        
      {"http://a.b<x", 4, 0, 10},          
  };
  for (const Row& r : rows) {
    INFO(r.s << " : " << r.colon);
    const md::AutolinkRange got = md::match_url(r.s, r.colon);
    CHECK(got.start == r.start);
    CHECK(got.end == r.end);
  }
}

TEST_CASE("md autolink: email") {
  struct Row {
    std::string_view run;
    std::size_t from;
    std::size_t start;
    std::size_t end;
    bool mailto;
  };
  const Row rows[] = {
      {"a@b.c", 0, 0, 5, true},
      {"a@b1+c@d.e", 0, 4, 10, true},        
      {"a@b.c@d.e", 0, 2, 9, true},          
      {".mailto:a@b.c", 0, 0, 13, false},    
      {"xmailto:a@b.c", 0, 8, 13, true},     
      {"xmpp:a@b.c/d", 0, 0, 12, false},
      {"mailto:@b.c", 0, 0, 11, false},      
      {"a@b.c1", 0, 6, 6, true},             
      {"a@b.c-", 0, 6, 6, true},
      {"a@b.c)", 0, 0, 5, true},             
      {"x(a@b.c)", 0, 2, 7, true},           
      {"a@b.c d@e.f", 0, 0, 5, true},        
      {"a@b.c d@e.f", 5, 6, 11, true},       
      {"a@b.\xFF" "c", 0, 6, 6, true},        
      {"a@b.c@d", 0, 2, 7, true},
      {"a@b.c@d1", 0, 8, 8, true},
      {"mailto:a@b@c.d", 0, 9, 14, false},
      {"xmpp:a@b@c.d/e", 0, 7, 14, false},
  };
  for (const Row& r : rows) {
    INFO(r.run << " from " << r.from);
    const md::EmailMatch got = md::find_email(r.run, r.from);
    CHECK(got.start == r.start);
    CHECK(got.end == r.end);
    CHECK(got.mailto == r.mailto);
  }
}
