#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "lumos/syntax.hpp"
#include "lumos/types.hpp"
#include "md/ref_index.hpp"

namespace lumos_test {

inline std::string node_summary(const lumos::SyntaxTree& t, lumos::NodeId id) {
  if (id == lumos::no_node || id >= t.size()) return "none";
  const lumos::Node& n = t.node(id);
  return "id=" + std::to_string(id) + " kind=" + std::string(lumos::kind_name(n.kind)) +
         " start=" + std::to_string(n.start) + " end=" + std::to_string(n.end) +
         " parent=" + std::to_string(n.parent) + " depth=" + std::to_string(t.depth(id)) +
         " tight=" + (n.tight ? "1" : "0") + " llb=" + (n.last_line_blank ? "1" : "0");
}

inline std::string tree_signature(const lumos::SyntaxTree& a, const lumos::SyntaxTree& b) {
  if (a.size() != b.size()) return "size";
  for (lumos::NodeId i = 0; i < a.size(); ++i) {
    const lumos::Node& x = a.node(i);
    const lumos::Node& y = b.node(i);
    const char* field = x.kind != y.kind ? "kind" : x.start != y.start ? "start" : x.end != y.end ? "end"
                        : x.parent != y.parent ? "parent" : x.tight != y.tight ? "tight"
                        : x.last_line_blank != y.last_line_blank ? "llb" : nullptr;
    if (field != nullptr)
      return std::string("diff ") + field + " " + std::string(lumos::kind_name(y.kind)) + " in " +
             std::string(lumos::kind_name(b.node(y.parent).kind));
  }
  return "";
}

inline std::string diff_trees(const lumos::SyntaxTree& a, const lumos::SyntaxTree& b) {
  const std::size_t n = std::min(a.size(), b.size());
  lumos::NodeId i = 0;
  for (; i < n; ++i) {
    const lumos::Node& x = a.node(i);
    const lumos::Node& y = b.node(i);
    if (x.kind != y.kind || x.start != y.start || x.end != y.end || x.parent != y.parent ||
        x.tight != y.tight || x.last_line_blank != y.last_line_blank)
      break;
  }
  if (i == n) {
    if (a.size() == b.size()) return "";
    return "tree sizes differ: old " + std::to_string(a.size()) + " new " + std::to_string(b.size()) +
           " (first extra node id " + std::to_string(n) + ")";
  }
  std::string out = "first diff at node " + std::to_string(i) + "\n";
  out += "  old: " + node_summary(a, i) + "\n";
  out += "  new: " + node_summary(b, i) + "\n";
  out += "  old parent: " + node_summary(a, a.node(i).parent) + "\n";
  out += "  new parent: " + node_summary(b, b.node(i).parent) + "\n";
  for (lumos::NodeId back = 1; back <= 2 && back <= i; ++back)
    out += "  before: " + node_summary(a, i - back) + "\n";
  return out;
}

inline std::string diff_refs(const lumos::md::RefIndex& a, const lumos::md::RefIndex& b) {
  const auto ad = a.definitions();
  const auto bd = b.definitions();
  const std::size_t n = std::min(ad.size(), bd.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (ad[i].start != bd[i].start || a.label(ad[i].label) != b.label(bd[i].label)) {
      return "refs differ at definition " + std::to_string(i) + ": old start=" + std::to_string(ad[i].start) +
             " label=\"" + std::string(a.label(ad[i].label)) + "\", new start=" + std::to_string(bd[i].start) +
             " label=\"" + std::string(b.label(bd[i].label)) + "\"";
    }
  }
  if (ad.size() != bd.size())
    return "refs size differs: old " + std::to_string(ad.size()) + " new " + std::to_string(bd.size());

  const auto al = a.leaves();
  const auto bl = b.leaves();
  const std::size_t ln = std::min(al.size(), bl.size());
  auto label_texts = [](const lumos::md::RefIndex& idx, const std::vector<lumos::md::LabelId>& ids) {
    std::vector<std::string> texts;
    texts.reserve(ids.size());
    for (lumos::md::LabelId id : ids) texts.emplace_back(idx.label(id));
    std::sort(texts.begin(), texts.end());
    std::string out;
    for (const std::string& t : texts) out += t + ",";
    return out;
  };
  for (std::size_t i = 0; i < ln; ++i) {
    const auto& x = al[i];
    const auto& y = bl[i];
    const std::string xl = label_texts(a, x.labels);
    const std::string yl = label_texts(b, y.labels);
    const bool same_segments = x.segments.size() == y.segments.size() &&
                                std::equal(x.segments.begin(), x.segments.end(), y.segments.begin(),
                                           [](const lumos::md::Segment& p, const lumos::md::Segment& q) {
                                             return p.start == q.start && p.end == q.end;
                                           });
    if (x.start != y.start || xl != yl || !same_segments) {
      return "refs differ at leaf " + std::to_string(i) + ": old start=" + std::to_string(x.start) + " labels=\"" +
             xl + "\", new start=" + std::to_string(y.start) + " labels=\"" + yl + "\"";
    }
  }
  if (al.size() != bl.size())
    return "refs leaves size differs: old " + std::to_string(al.size()) + " new " + std::to_string(bl.size());
  return "";
}

inline std::string source_window(std::string_view src, std::size_t offset, std::size_t radius = 100) {
  const std::size_t from = offset > radius ? offset - radius : 0;
  const std::size_t to = std::min(src.size(), offset + radius);
  return "offset " + std::to_string(offset) + ", bytes [" + std::to_string(from) + ".." + std::to_string(to) +
         "): \"" + std::string(src.substr(from, to - from)) + "\"";
}

using NodeKey = std::tuple<lumos::NodeKind, std::size_t, std::size_t, std::size_t>;  

inline std::multiset<NodeKey> nodes_outside(const lumos::SyntaxTree& t,
                                             std::span<const std::pair<std::size_t, std::size_t>> ranges,
                                             std::ptrdiff_t shift_after, std::size_t shift_from,
                                             const std::set<std::size_t>& ancestor_starts) {
  std::multiset<NodeKey> out;
  for (lumos::NodeId i = 1; i < t.size(); ++i) {
    const lumos::Node& n = t.node(i);
    if (ancestor_starts.count(n.start)) continue;
    bool outside = true;
    for (const auto& [from, to] : ranges) {
      const bool before = n.end <= from && !(n.start == n.end && n.start == from);
      const bool after = n.start > to;
      if (!before && !after) {
        outside = false;
        break;
      }
    }
    if (!outside) continue;
    const std::ptrdiff_t shift = n.start >= shift_from ? shift_after : 0;
    out.emplace(n.kind, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.start) + shift),
                static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.end) + shift), t.depth(i));
  }
  return out;
}

inline std::string check_locality(const lumos::SyntaxTree& old_tree, const lumos::SyntaxTree& new_tree,
                                   std::span<const lumos::TextRegion> regions) {
  if (regions.empty()) return "";
  std::vector<lumos::TextRegion> sorted(regions.begin(), regions.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const lumos::TextRegion& a, const lumos::TextRegion& b) { return a.start < b.start; });

  const auto main_it = std::find_if(sorted.begin(), sorted.end(),
                                     [](const lumos::TextRegion& r) { return r.new_end != r.old_end; });
  const bool has_main = main_it != sorted.end();
  const std::ptrdiff_t main_delta =
      has_main ? static_cast<std::ptrdiff_t>(main_it->new_end) - static_cast<std::ptrdiff_t>(main_it->old_end) : 0;
  const std::size_t main_start = has_main ? main_it->start : 0;
  const std::size_t main_old_end = has_main ? main_it->old_end : SIZE_MAX;

  std::vector<std::pair<std::size_t, std::size_t>> old_ranges, new_ranges;
  old_ranges.reserve(sorted.size());
  new_ranges.reserve(sorted.size());
  for (const lumos::TextRegion& r : sorted) {
    new_ranges.emplace_back(r.start, r.new_end);
    if (has_main && &r == &*main_it) {
      old_ranges.emplace_back(r.start, r.old_end);
    } else {
      const std::ptrdiff_t shift = has_main && r.start > main_start ? main_delta : 0;
      old_ranges.emplace_back(static_cast<std::size_t>(static_cast<std::ptrdiff_t>(r.start) - shift),
                               static_cast<std::size_t>(static_cast<std::ptrdiff_t>(r.old_end) - shift));
    }
  }

  std::set<std::size_t> ancestor_starts;
  if (has_main) {
    const lumos::TextRegion& r = *main_it;
    if (r.start > 0) {
      for (lumos::NodeId cur = old_tree.node_at(r.start - 1); cur != lumos::no_node; cur = old_tree.node(cur).parent)
        ancestor_starts.insert(old_tree.node(cur).start);
    }
    lumos::NodeId last_closed_before_r = lumos::no_node;
    for (lumos::NodeId i = 1; i < old_tree.size(); ++i) {
      if (old_tree.node(i).end <= r.start) last_closed_before_r = i;
    }
    for (lumos::NodeId cur = last_closed_before_r; cur != lumos::no_node; cur = old_tree.node(cur).parent)
      ancestor_starts.insert(old_tree.node(cur).start);
  }

  const auto o1 = nodes_outside(old_tree, old_ranges, main_delta, main_old_end, ancestor_starts);
  const auto o2 = nodes_outside(new_tree, new_ranges, 0, SIZE_MAX, ancestor_starts);
  if (o1 == o2) return "";
  std::string out;
  for (const lumos::TextRegion& r : sorted)
    out += "region start=" + std::to_string(r.start) + " old_end=" + std::to_string(r.old_end) +
           " new_end=" + std::to_string(r.new_end) + "\n";
  for (auto& k : o1)
    if (!o2.count(k))
      out += "only in old: kind=" + std::string(lumos::kind_name(std::get<0>(k))) +
             " start=" + std::to_string(std::get<1>(k)) + " end=" + std::to_string(std::get<2>(k)) +
             " depth=" + std::to_string(std::get<3>(k)) + "\n";
  for (auto& k : o2)
    if (!o1.count(k))
      out += "only in new: kind=" + std::string(lumos::kind_name(std::get<0>(k))) +
             " start=" + std::to_string(std::get<1>(k)) + " end=" + std::to_string(std::get<2>(k)) +
             " depth=" + std::to_string(std::get<3>(k)) + "\n";
  return out;
}

inline void dump_start(std::string_view text) {
  if (const char* dump = std::getenv("LUMOS_DUMP_EDITS")) {
    std::ofstream out(dump, std::ios::app);
    out << "START ";
    for (char c : text) out << std::hex << static_cast<int>(static_cast<unsigned char>(c)) << ",";
    out << std::dec << "\n";
  }
}

inline void dump_edit(const lumos::Edit& e) {
  if (const char* dump = std::getenv("LUMOS_DUMP_EDITS")) {
    std::ofstream out(dump, std::ios::app);
    out << e.offset << " " << e.removed << " ";
    for (char c : e.inserted) out << std::hex << static_cast<int>(static_cast<unsigned char>(c)) << ",";
    out << std::dec << "\n";
  }
}

struct EditBlock {
  std::string start_text;
  std::vector<lumos::Edit> edits;
};

inline std::string decode_hex_bytes(const std::string& field) {
  std::string out;
  std::istringstream ss(field);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    if (tok.empty()) continue;
    out.push_back(static_cast<char>(std::stoi(tok, nullptr, 16)));
  }
  return out;
}

inline std::vector<EditBlock> read_dump_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<EditBlock> blocks;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line.rfind("START ", 0) == 0) {
      blocks.push_back({decode_hex_bytes(line.substr(6)), {}});
      continue;
    }
    if (blocks.empty()) throw std::runtime_error("edit line before any START: " + line);
    std::istringstream ss(line);
    lumos::Edit e;
    if (!(ss >> e.offset >> e.removed)) throw std::runtime_error("bad edit line: " + line);
    std::string inserted_hex;
    ss >> inserted_hex;  
    e.inserted = decode_hex_bytes(inserted_hex);
    blocks.back().edits.push_back(std::move(e));
  }
  return blocks;
}

}  
