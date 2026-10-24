
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/md/html_renderer.hpp"
#include "lumos/parser.hpp"

namespace md = lumos::md;
using lumos::NodeId;
using lumos::NodeKind;

namespace {

std::string render(std::string_view text, const md::HtmlOptions& opt) {
  lumos::Dialect d;
  d.front_matter = false;
  auto p = md::make_markdown_parser(d);
  const lumos::StringSource s(text);
  p->parse(s);
  return md::render_html(p->tree(), s, opt);
}

bool has_task_marker(const lumos::SyntaxTree& tree, NodeId id) {
  for (NodeId c : tree.children(id))
    if (tree.node(c).kind == NodeKind::task_marker) return true;
  return false;
}

}  

TEST_CASE("html hooks: is_dangerous_url follows cmark") {
  CHECK(md::is_dangerous_url("javascript:x"));
  CHECK(md::is_dangerous_url("JavaScript:x"));
  CHECK(md::is_dangerous_url("VBSCRIPT:x"));
  CHECK(md::is_dangerous_url("file:///a"));
  CHECK(md::is_dangerous_url("data:text/html,x"));
  CHECK(md::is_dangerous_url("DATA:image/svg+xml,x"));
  CHECK_FALSE(md::is_dangerous_url("data:image/png,x"));
  CHECK_FALSE(md::is_dangerous_url("Data:Image/PNG,x"));
  CHECK_FALSE(md::is_dangerous_url("https://a"));
  CHECK_FALSE(md::is_dangerous_url("javascript"));
  CHECK_FALSE(md::is_dangerous_url(""));
}

TEST_CASE("html hooks: safe mode covers autolinks") {
  const md::HtmlOptions opt;
  CHECK(render("<javascript:x>\n", opt) == "<p><a href=\"\">javascript:x</a></p>\n");
  CHECK(render("<File:///a>\n", opt) == "<p><a href=\"\">File:///a</a></p>\n");
  CHECK(render("<http://a>\n", opt) == "<p><a href=\"http://a\">http://a</a></p>\n");
}

TEST_CASE("html hooks: url hook gets each URL with its kind") {
  std::vector<std::pair<md::UrlKind, std::string>> seen;
  md::HtmlOptions opt;
  opt.url_hook = [&](md::UrlKind kind, std::string_view url) -> std::optional<std::string> {
    seen.emplace_back(kind, std::string(url));
    return std::string(url);
  };
  const std::string out = render(
      "[a](x&amp;y) ![b](i.png) [c][r] <uu:v> www.w.x a@b.cd\n\n"
      "[r]: \\*ref\n",
      opt);
  const std::vector<std::pair<md::UrlKind, std::string>> want = {
      {md::UrlKind::link, "x&y"},  {md::UrlKind::image, "i.png"},          {md::UrlKind::link, "*ref"},
      {md::UrlKind::link, "uu:v"}, {md::UrlKind::link, "http://www.w.x"}, {md::UrlKind::link, "mailto:a@b.cd"},
  };
  CHECK(seen == want);
  CHECK(out ==
        "<p><a href=\"x&amp;y\">a</a> <img src=\"i.png\" alt=\"b\" /> <a href=\"*ref\">c</a> "
        "<a href=\"uu:v\">uu:v</a> <a href=\"http://www.w.x\">www.w.x</a> <a href=\"mailto:a@b.cd\">a@b.cd</a></p>\n");
}

TEST_CASE("html hooks: url hook rewrites, and safe mode does not check its result") {
  md::HtmlOptions opt;
  opt.url_hook = [](md::UrlKind kind, std::string_view url) -> std::optional<std::string> {
    if (kind == md::UrlKind::image) return "file:///notes/" + std::string(url);
    return std::string(url);
  };
  CHECK(render("![a b](<c d.png>)\n", opt).find("<img src=\"file:///notes/c%20d.png\" alt=\"a b\" />") !=
        std::string::npos);
  CHECK(render("[a](javascript:x)\n", opt) == "<p><a href=\"javascript:x\">a</a></p>\n");
}

TEST_CASE("html hooks: url hook drops links and images") {
  md::HtmlOptions opt;
  opt.url_hook = [](md::UrlKind, std::string_view url) -> std::optional<std::string> {
    if (md::is_dangerous_url(url) || url.substr(0, 4) == "http") return std::nullopt;
    return std::string(url);
  };
  CHECK(render("[a *b*](javascript:x \"t\")\n", opt) == "<p>a <em>b</em></p>\n");
  CHECK(render("![a *b*](http://x/i.png)\n", opt) == "<p>a <em>b</em></p>\n");
  CHECK(render("<http://a> www.b.c x@y.zz <rel:k>\n", opt) ==
        "<p>http://a www.b.c <a href=\"mailto:x@y.zz\">x@y.zz</a> <a href=\"rel:k\">rel:k</a></p>\n");
  opt.url_hook = [](md::UrlKind, std::string_view) -> std::optional<std::string> { return std::nullopt; };
  CHECK(render("x@y.zz and [n](u)\n", opt) == "<p>x@y.zz and n</p>\n");
}

TEST_CASE("html hooks: node hook replaces task item tags") {
  md::HtmlOptions opt;
  opt.node_hook = [](const lumos::SyntaxTree& tree, const lumos::TextSource&, NodeId id, bool entering,
                     std::string& out) {
    if (tree.node(id).kind != NodeKind::list_item || !has_task_marker(tree, id)) return md::NodeOutput::keep;
    if (entering) {
      if (!out.empty() && out.back() != '\n') out += '\n';
      out += "<li class=\"task\">";
    } else {
      out += "</li>\n";
    }
    return md::NodeOutput::replace_tags;
  };
  CHECK(render("- [x] *done*\n- plain\n", opt) ==
        "<ul>\n<li class=\"task\"><em>done</em></li>\n<li>plain</li>\n</ul>\n");
}

TEST_CASE("html hooks: node hook replaces whole nodes and keeps the rest") {
  int calls = 0;
  md::HtmlOptions opt;
  opt.node_hook = [&](const lumos::SyntaxTree& tree, const lumos::TextSource&, NodeId id, bool entering,
                      std::string& out) {
    ++calls;
    CHECK(entering);  
    switch (tree.node(id).kind) {
      case NodeKind::code_block:
        out += "<pre>[code]</pre>\n";
        return md::NodeOutput::replace_all;
      case NodeKind::strong:
        out += "[strong]";
        return md::NodeOutput::replace_all;
      default: return md::NodeOutput::keep;
    }
  };
  CHECK(render("a **b *c*** d\n\n```\nx\n```\n\n| h |\n|---|\n| **e** |\n", opt) ==
        "<p>a [strong] d</p>\n<pre>[code]</pre>\n<table>\n<thead>\n<tr>\n<th>h</th>\n</tr>\n</thead>\n"
        "<tbody>\n<tr>\n<td>[strong]</td>\n</tr>\n</tbody>\n</table>\n");
  CHECK(calls > 0);
}

TEST_CASE("html hooks: a keep-all node hook gives the default output") {
  const std::string doc =
      "# h\n\n> - [ ] a\n>   1. b **c** [d](e)\n\n| x | y |\n|:-|-:|\n| ![i](j) | ~k~ |\n\n<div>\nz\n</div>\n";
  md::HtmlOptions opt;
  const std::string plain = render(doc, opt);
  opt.node_hook = [](const lumos::SyntaxTree&, const lumos::TextSource&, NodeId, bool, std::string&) {
    return md::NodeOutput::keep;
  };
  CHECK(render(doc, opt) == plain);
}
