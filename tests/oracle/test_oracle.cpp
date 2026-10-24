
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/md/html_renderer.hpp"
#include "lumos/parser.hpp"

#if defined(LUMOS_ORACLE_GFM)
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#else
#include <cmark.h>
#endif

namespace {

#if defined(LUMOS_ORACLE_GFM)
bool g_task_off_first_line = false;

bool task_ws(unsigned char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }

bool task_box(const unsigned char* p) {
  return p[0] == '[' && (p[1] == ' ' || p[1] == 'x' || p[1] == 'X') && p[2] == ']';
}

bool task_line(const unsigned char* s, int len) {
  int i = 0;
  while (i < len && task_ws(s[i])) ++i;
  if (i < len && (s[i] == '-' || s[i] == '+' || s[i] == '*')) {
    ++i;
  } else if (i < len && s[i] >= '0' && s[i] <= '9') {
    while (i < len && s[i] >= '0' && s[i] <= '9') ++i;
    ++i;
  } else {
    return false;
  }
  if (i >= len || !task_ws(s[i])) return false;
  while (i < len && task_ws(s[i])) ++i;
  return i + 3 < len && task_box(s + i) && task_ws(s[i + 3]);
}

cmark_node* task_probe(cmark_syntax_extension*, int, cmark_parser* parser, cmark_node* parent, unsigned char* input,
                       int len) {
  if (cmark_node_get_type(parent) != CMARK_NODE_ITEM || !task_line(input, len)) return nullptr;
  const int off = cmark_parser_get_offset(parser);
  const bool first_line = cmark_node_first_child(parent) == nullptr &&
                          cmark_node_get_start_line(parent) == cmark_parser_get_line_number(parser);
  if (!first_line || off + 3 > len || !task_box(input + off)) g_task_off_first_line = true;
  return nullptr;
}
#endif

bool g_safe = false;

std::string reference_html(const std::string& doc, bool extensions = true) {
  const int options = g_safe ? CMARK_OPT_DEFAULT : CMARK_OPT_UNSAFE;
#if defined(LUMOS_ORACLE_GFM)
  cmark_gfm_core_extensions_ensure_registered();
  cmark_parser* parser = cmark_parser_new(options);
  g_task_off_first_line = false;
  if (extensions) {
    static cmark_syntax_extension* const probe = [] {
      cmark_syntax_extension* e = cmark_syntax_extension_new("lumos_task_probe");
      cmark_syntax_extension_set_open_block_func(e, task_probe);
      return e;
    }();
    for (const char* name : {"table", "strikethrough", "autolink", "tasklist", "tagfilter"}) {
      if (std::string_view(name) == "tasklist") cmark_parser_attach_syntax_extension(parser, probe);
      if (cmark_syntax_extension* ext = cmark_find_syntax_extension(name))
        cmark_parser_attach_syntax_extension(parser, ext);
    }
  }
  cmark_parser_feed(parser, doc.data(), doc.size());
  cmark_node* root = cmark_parser_finish(parser);
#else
  (void)extensions;
  cmark_node* root = cmark_parse_document(doc.data(), doc.size(), options);
#endif
#if defined(LUMOS_ORACLE_GFM)
  char* out = cmark_render_html(root, options, cmark_parser_get_syntax_extensions(parser));
#else
  char* out = cmark_render_html(root, options);
#endif
  std::string s(out);
  std::free(out);
  cmark_node_free(root);
#if defined(LUMOS_ORACLE_GFM)
  cmark_parser_free(parser);
#endif
  return s;
}

struct Ours {
  std::string html;
  std::string problem;  
};

Ours our_html(const std::string& doc, bool gfm) {
  lumos::Dialect d = lumos::Dialect::commonmark();
  if (gfm) {
    d = lumos::Dialect{};
    d.front_matter = false;
  }
  auto parser = lumos::md::make_markdown_parser(d);
  const lumos::StringSource src(doc);
  parser->parse(src);
  Ours r;
  lumos::md::HtmlOptions opt;
  opt.unsafe = !g_safe;
  opt.tagfilter = gfm;
  r.html = lumos::md::render_html(parser->tree(), src, opt);
  if (auto e = parser->tree().validate(doc.size())) r.problem = *e;
  if (auto e = parser->tree().validate_coverage(doc)) r.problem = *e;
  return r;
}

class Generator {
 public:
  Generator(std::uint32_t seed, bool gfm) : rng_(seed), gfm_(gfm) {}

  std::string document() {
    html_flavor_ = coin(2);
    const int ending_style = pick(10);
    std::string doc;
    if (coin(50)) doc += "\xEF\xBB\xBF";  
    const int lines = 1 + pick(14);
    for (int i = 0; i < lines; ++i) {
      doc += line();
      if (i + 1 == lines && coin(5)) break;  
      if (ending_style == 0) {
        doc += "\r\n";
      } else if (ending_style == 1) {
        static const char* endings[] = {"\n", "\r\n", "\r"};
        doc += endings[pick(3)];
      } else {
        doc += "\n";
      }
    }
    return doc;
  }

 private:
  int pick(int n) { return static_cast<int>(rng_() % static_cast<std::uint32_t>(n)); }
  bool coin(int n) { return pick(n) == 0; }
  template <std::size_t N>
  const char* one(const char* const (&a)[N]) {
    return a[pick(static_cast<int>(N))];
  }

  std::string words() {
    static const char* const w[] = {"foo", "bar", "baz", "qux", "x",  "hello", "a1", "\xC3\xA4\xC3\xB6", "\xE6\x97\xA5",
                                    "9",   "1.",  "2)",  "-",   "#",  "=",     "+",  "~",  "0.",  "123456789.",
                                    "1234567890.", "\v", "\f", "x\vy", "\xFF", "\x80"};
    if (coin(200)) return std::string("a\0b", 3);
    auto word = [&] {
      for (;;) {
        std::string_view x = one(w);
        if (!gfm_ || (x.find('~') == std::string_view::npos && x != "\v" && x != "\f")) return std::string(x);
      }
    };
    std::string s = word();
    const int n = pick(4);
    for (int i = 0; i < n; ++i) {
      s += coin(6) ? "\t" : " ";
      s += word();
    }
    return s;
  }

  std::string prefix() {
    static const char* const p[] = {">", "> ", ">  ", "- ", "* ", "+ ", "1. ", "2) ", "10. ", "-\t", ">\t", "1.\t",
                                    "-\v", "  -  ", "-      ", "1)  ", " >", "   > ", "\t> ",
                                    " ",  "  ", "   ", "    ", "\t", " \t", "-    "};
    std::string s;
    const int n = pick(4);
    for (int i = 0; i < n; ++i) {
      std::string_view x = one(p);
      if (gfm_ && x.find('\v') != std::string_view::npos) x = "- ";
      s += x;
    }
    return s;
  }

  std::string inline_tokens() {
    static const char* const toks[] = {
        "*", "**", "***", "****", "_", "__", "___", "____", "~", "~~", "~~~",
        "*a*", "_a_", "~a~", "~~a~~", "**a**", "***a***", "*a**b**c*", "a*\"b\"*", "**a*", "*(*a*)*",
        "`a`", "``a``", "`a``b`", "``a`b``", "a`b", "`  `", "` a `",
        "\\*", "\\_", "\\`", "\\~", "\\\\", "\\a", "\\!", "\\(", "\\)", "\\.",
        "&amp;", "&#35;", "&#x41;", "&nbsp;", "&foo;", "&AMP", "&#0;", "&#1114112;", "&#xD800;",
        "<http://x>", "<a@b.c>", "<HTTP://X>", "<a1+.-:x>", "<a:>", "<a1+.-:x y>", "<mailto:a@b.c>", "<http://x y>",
        "<javascript:x>", "<file:///a>", "<data:text/html,x>", "<data:image/png,x>", "<JAVASCRIPT:x>",
        "<!-- c -->", "<!-->", "<!--->", "<!-- a ---> ", "<?x?>", "<![CDATA[x]]>", "<!DOCTYPE x>",
        "<a b=\"c\">", "</a>", "<a b=c>", "<a_b>", "<a *b*>", "</a >", "</a b>",
        "\xE2\x82\xAC",  
        "\xC2\xA0",      
        "\xE3\x80\x82",  
        "<script>", "</style >", "<TITLE/>", "<xmp\t>", "<scripts>", "<plaintext",
    };
    static const char* const brackets[] = {"[", "]", "![", "[]", "[a", "a]"};
    if (gfm_ && coin(5)) return autolink_tokens();
    if (coin(6)) return one(brackets);
    return one(toks);
  }

  std::string autolink_tokens() {
    static const char* const leads[] = {"", " ", "*", "_", "~", "(", ".", "x", "[", "![", "\v"};
    static const char* const www_bodies[] = {
        "www.a.b", "www.a", "www.", "WWW.a.b", "www.a_b.c", "www.a.b_c",
        "www.\xE2\x82\xAC.a_b", "www.a.b.c.d.e.f.g.h.i.j.k_l.m",
    };
    static const char* const url_bodies[] = {
        "http://a.b", "https://a", "ftp://x.y/z", "HTTP://A", "xhttp://a.b",
        "http://", "http://\xC2\xA1", "http://a\\_b.c",
    };
    static const char* const email_bodies[] = {
        "a@b.c", "a.b+c@d-e.f", "mailto:a@b.c", "xmpp:a@b.c/d", "a@b", "a@b.c@d",
        "a@b1+c@d.e", "@", "a\\_b@c.d", "&#97;@b.c", "a&#64;b.c",
    };
    static const char* const tails[] = {
        "", "/p(q)", ")", "))", ".", "?!", "&amp;", "&x;", ";", "<b", "'", "\"", "_", "*", "~", "]", ".x",
    };
    std::string s = one(leads);
    switch (pick(3)) {
      case 0: s += one(www_bodies); break;
      case 1: s += one(url_bodies); break;
      default: s += one(email_bodies); break;
    }
    s += one(tails);
    return s;
  }

  std::string label_word() {
    static const char* const pool[] = {"foo", "bar", "baz", "qux lo"};
    std::string s = one(pool);
    switch (pick(4)) {
      case 0:
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        break;
      case 1: s = " " + s + "  "; break;
      case 2: {
        std::string t;
        for (char c : s) {
          t += c;
          if (c == ' ') t += "\t ";
        }
        s = t;
        break;
      }
      default: break;
    }
    return s;
  }

  std::string link_dest() {
    static const char* const plain[] = {"url",          "a/b/c",     "x",          "123",
                                        "a(b)c",        "p.q.r",     "",           "a\\(b\\)",
                                        "a&#32;b",      "javascript:x", "file:///a", "vbscript:a",
                                        "data:image/gif;a", "data:x"};
    static const char* const pointy[] = {"url", "a b", "a.b", "", "x y z", "a\\<b", "javascript:a b"};
    if (coin(5)) return "<" + std::string(one(pointy)) + ">";
    return std::string(one(plain));
  }

  std::string title_words() {
    std::string t = words();
    if (gfm_) std::erase_if(t, [](char c) { return c == '\xFF' || c == '\x80'; });
    return t;
  }

  std::string link_title() {
    switch (pick(4)) {
      case 0: return "";
      case 1: return " \"" + title_words() + "\"";
      case 2: return " '" + title_words() + "'";
      default: return " (" + title_words() + ")";
    }
  }

  std::string link_tokens() {
    const std::string bang = coin(3) ? "!" : "";
    const std::string text = coin(2) ? words() : inline_tokens();
    switch (pick(5)) {
      case 0: {
        std::string s = bang + "[" + text + "](" + link_dest();
        if (coin(2)) s += link_title();
        if (coin(6)) return s;  
        return s + ")";
      }
      case 1: return bang + "[" + text + "][" + label_word() + "]";  
      case 2: return bang + "[" + label_word() + "][]";              
      case 3: return bang + "[" + label_word() + "]";                
      default: {
        std::string s = bang + "[" + text + "](\n" + link_dest();
        if (coin(2)) s += "\n" + link_title();
        return s + ")";
      }
    }
  }

  std::string table_row_piece() {
    std::string s;
    if (coin(3)) s += "|";
    const int cells = 1 + pick(4);
    for (int i = 0; i < cells; ++i) {
      if (i) s += "|";
      switch (pick(5)) {
        case 0: s += words(); break;
        case 1: s += "a\\|b"; break;
        case 2: s += "a\\\\|b"; break;
        case 3: s += "`a|b`"; break;
        default: s += link_tokens(); break;
      }
    }
    if (coin(3)) s += "|";
    return s;
  }

  std::string table_delimiter_piece() {
    switch (pick(5)) {
      case 0: return "-|";
      case 1: return "- -|-";
      case 2: return ":";
      default: break;
    }
    std::string s;
    if (coin(3)) s += "|";
    const int cells = 1 + pick(4);
    for (int i = 0; i < cells; ++i) {
      if (i) s += "|";
      static const char* const d[] = {"-", "--", ":-", "-:", ":-:", " - ", "\t-\t"};
      s += one(d);
    }
    if (coin(3)) s += "|";
    if (coin(5)) s += "x";  
    return s;
  }

  std::string body() {
    switch (pick(19)) {
      case 17: return table_row_piece();
      case 18: return table_delimiter_piece();
      case 0: return "#" + std::string(static_cast<std::size_t>(pick(7)), '#') + (coin(3) ? "" : " " + words());
      case 1: {
        static const char* const t[] = {"---", "***", "___", "- - -", " * * *", "-----", "--", "=", "===", "==="};
        return one(t);
      }
      case 2:
        if (coin(8)) return std::string(coin(2) ? 300 : 256 + static_cast<std::size_t>(pick(8)), '~');
        return std::string(3 + static_cast<std::size_t>(pick(2)), '~') + (coin(2) ? "" : " " + words());
      case 3: return "    " + words();
      case 4: return "";
      case 5:
        if (html_flavor_) {
          static const char* const h[] = {"<div>", "</div>", "<pre>", "</pre>", "<script>", "</script>",
                                          "<style>", "</style>", "<textarea>", "<!-- c", "-->", "<?x", "?>",
                                          "<!DOCTYPE html>", "<![CDATA[", "]]>", "<table>", "<p>", "<div",
                                          "<hr/>", "<del>", "</ins>", "<a b=\"c\">", "<DIV>", "<PRE", "<x-y z='1'>",
                                          "<!a b>", "<?", "</div >", "<div/>", "<td\t>", "<a\vb=c>", "<textarea>",
                                          "</textarea>", "<style", "<ScRiPt>", "<!---->", "<!-->", "<b/>",
                                          "<script>", "</style >", "<TITLE/>", "<xmp\t", "<scripts>", "<plaintext"};
          std::string s = one(h);
          if (coin(3)) s += " " + words();
          return s;
        }
        return words() + (coin(2) ? "  " : "\\");
      case 6: {
        std::string s = coin(3) ? link_tokens() : inline_tokens();
        const int n = pick(4);
        for (int i = 0; i < n; ++i) {
          s += coin(3) ? "" : (coin(2) ? " " : "\t");
          s += pick(3) == 0 ? link_tokens() : (coin(2) ? inline_tokens() : words());
        }
        if (coin(4)) s += one({" ", "  ", "\t", "\\"});
        return s;
      }
      default: {
        std::string s = words();
        if (coin(4)) s += one({" ", "  ", "\t", "   "});
        return s;
      }
    }
  }

  std::string definition_line() {
    const std::string label = coin(3) ? label_word() : "d" + std::to_string(++def_count_);
    const std::string pfx = prefix();
    const bool pointy = coin(3);
    static const char* const plain_dest[] = {"url", "a/b/c", "x", "123", "a(b)c", "p.q.r", "javascript:x", "data:image/webp,x"};
    static const char* const pointy_dest[] = {"url", "a b", "a.b", "", "x y z"};
    const std::string dest = pointy ? (std::string("<") + one(pointy_dest) + ">") : std::string(one(plain_dest));

    const int title_kind = pick(4);  
    std::string title;
    if (title_kind == 1) title = "\"" + title_words() + "\"";
    else if (title_kind == 2) title = "'" + title_words() + "'";
    else if (title_kind == 3) title = "(" + title_words() + ")";

    const int shape = pick(8);
    const bool bad_label = shape == 0;
    const bool no_colon = shape == 1;
    const bool trailing = shape == 2;
    const bool multiline = coin(3) && !no_colon;

    std::string s = pfx + "[" + (bad_label ? " " : label) + "]" + (no_colon ? " " : ":");
    if (multiline) {
      s += "\n" + pfx + dest;
      if (!title.empty()) s += "\n" + pfx + title;
    } else {
      s += " " + dest;
      if (!title.empty()) s += " " + title;
    }
    if (trailing) s += " " + words();
    if (coin(5)) {
      static const char* const underline[] = {"===", "---", "--"};
      s += "\n" + pfx + one(underline);
    }
    return s;
  }

  std::string task_item_start() {
    static const char* const bullets[] = {"-", "*", "1.", "2)"};
    static const char* const gaps[] = {" ", "\t", "\v", "\f", "  "};
    static const char* const boxes[] = {"[ ]", "[x]", "[X]", "[\t]"};
    std::string s = one(bullets);
    s += one(gaps);
    s += one(boxes);
    if (!coin(5)) s += one(gaps);
    return s;
  }

  std::string line() {
    if (coin(4)) {
      static const char* const b[] = {"", "", "  ", ">", "> ", "\t", "    ", ">>"};
      return one(b);
    }
    if (coin(10)) return definition_line();
    if (gfm_ && coin(10)) return prefix() + task_item_start() + (coin(2) ? body() : "");
    return prefix() + body();
  }

  std::mt19937 rng_;
  bool gfm_ = false;
  bool html_flavor_ = false;
  int def_count_ = 0;
};

std::string visible(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      case '\n': out += "\\n\n"; break;
      case '\\': out += "\\\\"; break;
      case '\v': out += "\\v"; break;
      case '\f': out += "\\f"; break;
      case '\0': out += "\\0"; break;
      default: out += c;
    }
  }
  return out;
}

#if defined(LUMOS_ORACLE_GFM)
constexpr bool gfm_mode = true;
#else
constexpr bool gfm_mode = false;
#endif

bool core_differs(const std::string& doc) {
  if (!gfm_mode) return false;
#if defined(LUMOS_ORACLE_GFM)
  reference_html(doc);
  if (g_task_off_first_line) return true;
#endif
  if (our_html(doc, false).html != reference_html(doc, false)) return true;
  std::string no_tilde;
  for (char c : doc)
    if (c != '~') no_tilde += c;
  if (no_tilde.size() != doc.size() && our_html(no_tilde, false).html != reference_html(no_tilde, false)) return true;
  std::string no_task = doc;
  for (std::size_t i = 0; i + 3 <= no_task.size(); ++i)
    if (no_task[i] == '[' && (no_task[i + 1] == ' ' || no_task[i + 1] == 'x' || no_task[i + 1] == 'X') &&
        no_task[i + 2] == ']')
      no_task.replace(i, 3, "   ");
  if (no_task != doc && our_html(no_task, false).html != reference_html(no_task, false)) return true;
  std::string no_rows;
  bool had_pipe = false;
  std::size_t line = 0;
  for (std::size_t i = 0; i <= doc.size(); ++i) {
    if (i < doc.size() && doc[i] != '\n' && doc[i] != '\r') continue;
    const std::string_view text(doc.data() + line, i - line);
    const bool delimiter_like =
        text.find('-') != std::string_view::npos && text.find_first_not_of("|:- \t\v\f") == std::string_view::npos;
    if (text.find('|') != std::string_view::npos || delimiter_like) {
      had_pipe = true;
    } else {
      no_rows += text;
    }
    if (i < doc.size()) no_rows += doc[i];
    line = i + 1;
  }
  if (had_pipe && our_html(no_rows, false).html != reference_html(no_rows, false)) return true;
  std::vector<std::string_view> lines;
  std::vector<char> endings;
  line = 0;
  for (std::size_t i = 0; i <= doc.size(); ++i) {
    if (i < doc.size() && doc[i] != '\n' && doc[i] != '\r') continue;
    lines.emplace_back(doc.data() + line, i - line);
    endings.push_back(i < doc.size() ? doc[i] : '\0');
    line = i + 1;
  }
  std::vector<bool> empty(lines.size(), false);
  for (std::size_t k = 0; k < lines.size(); ++k) {
    const std::string_view text = lines[k];
    if (text.find('-') != std::string_view::npos && text.find_first_not_of("|:- \t\v\f") == std::string_view::npos) {
      empty[k] = true;
      if (k > 0) empty[k - 1] = true;
    }
  }
  std::string no_head;
  for (std::size_t k = 0; k < lines.size(); ++k) {
    if (!empty[k]) no_head += lines[k];
    if (endings[k] != '\0') no_head += endings[k];
  }
  return no_head != doc && our_html(no_head, false).html != reference_html(no_head, false);
}

bool differs(const std::string& doc) {
  const Ours o = our_html(doc, gfm_mode);
  return !o.problem.empty() || o.html != reference_html(doc);
}

std::string shrink_lines(std::string doc) {
  for (bool again = true; again;) {
    again = false;
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < doc.size(); ++i)
      if (doc[i] == '\n' || (doc[i] == '\r' && (i + 1 >= doc.size() || doc[i + 1] != '\n'))) starts.push_back(i + 1);
    for (std::size_t k = starts.size(); k-- > 0;) {
      const std::size_t a = starts[k];
      const std::size_t b = k + 1 < starts.size() ? starts[k + 1] : doc.size();
      if (a >= b) continue;
      std::string cut = doc.substr(0, a) + doc.substr(b);
      if (!core_differs(cut) && differs(cut)) {
        doc = cut;
        again = true;
        break;
      }
    }
  }
  return doc;
}

std::string shrink_chars(std::string doc) {
  for (bool again = true; again;) {
    again = false;
    for (std::size_t len = 4; len != 0; --len) {
      for (std::size_t i = doc.size(); i-- > 0;) {
        if (i + len > doc.size()) continue;
        if (len > 1) {
          bool same = true;
          for (std::size_t j = 1; j < len; ++j)
            if (doc[i + j] != doc[i]) same = false;
          if (!same) continue;
        }
        std::string cut = doc.substr(0, i) + doc.substr(i + len);
        if (!core_differs(cut) && differs(cut)) {
          doc = cut;
          again = true;
          break;
        }
      }
      if (again) break;
    }
  }
  return doc;
}

std::string shrink(const std::string& doc) { return shrink_chars(shrink_lines(doc)); }

void report(const std::string& doc, int n) {
  const std::string small = shrink(doc);
  const Ours o = our_html(small, gfm_mode);
  std::printf("=== mismatch %d\ninput (shrunk):\n%s\n--- cmark:\n%s--- lumos:\n%s", n, visible(small).c_str(),
              reference_html(small).c_str(), o.html.c_str());
  if (!o.problem.empty()) std::printf("--- invalid tree: %s\n", o.problem.c_str());
}

}  

int main(int argc, char** argv) {
  int count = 50000;
  std::uint32_t seed = 1;
  int show = 5;
  std::string doc_file;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--count" && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (a == "--show" && i + 1 < argc) {
      show = std::atoi(argv[++i]);
    } else if (a == "--doc" && i + 1 < argc) {
      doc_file = argv[++i];
    } else if (a == "--safe") {
      g_safe = true;
    } else {
      std::fprintf(stderr, "usage: lumos_oracle [--count N] [--seed S] [--show N] [--doc FILE] [--safe]\n");
      return 2;
    }
  }
  if (!doc_file.empty()) {
    std::ifstream in(doc_file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string doc = ss.str();
    const Ours o = our_html(doc, gfm_mode);
    const std::string ref = reference_html(doc);
    std::printf("--- cmark:\n%s--- lumos:\n%s", ref.c_str(), o.html.c_str());
    if (!o.problem.empty()) std::printf("--- invalid tree: %s\n", o.problem.c_str());
    return o.html == ref && o.problem.empty() ? 0 : 1;
  }
  Generator gen(seed, gfm_mode);
  int mismatches = 0;
  int skipped = 0;
  for (int i = 0; i < count; ++i) {
    const std::string doc = gen.document();
    if (core_differs(doc)) {
      ++skipped;
      continue;
    }
    if (!differs(doc)) continue;
    ++mismatches;
    if (mismatches <= show) report(doc, mismatches);
  }
  if (gfm_mode) std::printf("%d documents skipped: cmark-gfm core differs, or a lazy task line\n", skipped);
  std::printf("%d documents, %d mismatches (seed %u%s)\n", count, mismatches, seed, g_safe ? ", safe mode" : "");
  return mismatches == 0 ? 0 : 1;
}
