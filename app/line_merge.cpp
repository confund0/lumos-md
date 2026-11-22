
#include "line_merge.hpp"

#include <algorithm>
#include <unordered_map>

namespace {

void assign_ids(const std::vector<std::string_view>& lines, std::unordered_map<std::string_view, int>* ids,
                 std::vector<int>* out) {
  out->reserve(lines.size());
  for (std::string_view line : lines) {
    auto [it, added] = ids->try_emplace(line, static_cast<int>(ids->size()));
    out->push_back(it->second);
  }
}

int get_v(const std::vector<int>& v, int k, int offset) {
  const int idx = k + offset;
  if (idx < 0 || idx >= static_cast<int>(v.size())) return -1;
  return v[static_cast<std::size_t>(idx)];
}

struct Move {
  int prev_x = 0, prev_y = 0, x = 0, y = 0;
};

bool myers_moves(const std::vector<int>& a, const std::vector<int>& b, int cap, std::vector<Move>* moves) {
  const int n = static_cast<int>(a.size());
  const int m = static_cast<int>(b.size());
  const int max_d = std::min(cap, n + m);
  const int offset = max_d + 1;
  const int width = 2 * max_d + 3;
  std::vector<int> v(static_cast<std::size_t>(width), -1);
  v[static_cast<std::size_t>(1 + offset)] = 0;  
  std::vector<std::vector<int>> trace;
  int found_d = -1;
  for (int d = 0; d <= max_d; ++d) {
    trace.push_back(v);
    for (int k = -d; k <= d; k += 2) {
      int x = 0;
      if (k == -d || (k != d && get_v(v, k - 1, offset) < get_v(v, k + 1, offset))) {
        x = get_v(v, k + 1, offset);
      } else {
        x = get_v(v, k - 1, offset) + 1;
      }
      int y = x - k;
      while (x < n && y < m && a[static_cast<std::size_t>(x)] == b[static_cast<std::size_t>(y)]) {
        ++x;
        ++y;
      }
      v[static_cast<std::size_t>(k + offset)] = x;
      if (x >= n && y >= m) {
        found_d = d;
        break;
      }
    }
    if (found_d >= 0) break;
  }
  if (found_d < 0) return false;

  int x = n;
  int y = m;
  for (int d = found_d; d >= 1; --d) {
    const std::vector<int>& prior = trace[static_cast<std::size_t>(d)];
    const int k = x - y;
    int prev_k = 0;
    if (k == -d || (k != d && get_v(prior, k - 1, offset) < get_v(prior, k + 1, offset))) {
      prev_k = k + 1;
    } else {
      prev_k = k - 1;
    }
    const int prev_x = get_v(prior, prev_k, offset);
    const int prev_y = prev_x - prev_k;
    while (x > prev_x && y > prev_y) {
      moves->push_back(Move{x - 1, y - 1, x, y});
      --x;
      --y;
    }
    moves->push_back(Move{prev_x, prev_y, x, y});
    x = prev_x;
    y = prev_y;
  }
  std::reverse(moves->begin(), moves->end());
  return true;
}

}  

std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  if (text.empty()) return lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      lines.push_back(text.substr(start, i - start + 1));
      start = i + 1;
    }
  }
  if (start < text.size()) lines.push_back(text.substr(start));
  return lines;
}

bool diff_lines(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b, int cap,
                std::vector<LineHunk>* out) {
  out->clear();
  const int n = static_cast<int>(a.size());
  const int m = static_cast<int>(b.size());

  int prefix = 0;
  const int most_prefix = std::min(n, m);
  while (prefix < most_prefix && a[static_cast<std::size_t>(prefix)] == b[static_cast<std::size_t>(prefix)]) ++prefix;
  int suffix = 0;
  const int most_suffix = most_prefix - prefix;
  while (suffix < most_suffix &&
         a[static_cast<std::size_t>(n - 1 - suffix)] == b[static_cast<std::size_t>(m - 1 - suffix)]) {
    ++suffix;
  }

  std::unordered_map<std::string_view, int> ids;
  std::vector<int> ids_a, ids_b;
  assign_ids(a, &ids, &ids_a);
  assign_ids(b, &ids, &ids_b);
  const std::vector<int> mid_a(ids_a.begin() + prefix, ids_a.begin() + (n - suffix));
  const std::vector<int> mid_b(ids_b.begin() + prefix, ids_b.begin() + (m - suffix));

  std::vector<Move> moves;
  if (!myers_moves(mid_a, mid_b, cap, &moves)) return false;

  bool open = false;
  LineHunk cur;
  for (const Move& move : moves) {
    const bool kept = move.x == move.prev_x + 1 && move.y == move.prev_y + 1;
    if (kept) {
      if (open) {
        out->push_back(cur);
        open = false;
      }
      continue;
    }
    if (!open) {
      cur = LineHunk{prefix + move.prev_x, prefix + move.prev_x, prefix + move.prev_y, prefix + move.prev_y};
      open = true;
    }
    cur.a_end = prefix + move.x;
    cur.b_end = prefix + move.y;
  }
  if (open) out->push_back(cur);
  return true;
}

namespace {

struct Group {
  int base_begin = 0, base_end = 0;
  std::vector<LineHunk> yours, other;
};

std::vector<Group> group_hunks(const std::vector<LineHunk>& yours_hunks, const std::vector<LineHunk>& other_hunks) {
  std::vector<Group> groups;
  std::size_t i = 0, j = 0;
  Group* current = nullptr;
  auto absorb = [&](const LineHunk& hunk, bool is_yours) {
    if (current != nullptr && hunk.a_begin <= current->base_end) {
      current->base_end = std::max(current->base_end, hunk.a_end);
    } else {
      groups.push_back(Group{hunk.a_begin, hunk.a_end, {}, {}});
      current = &groups.back();
    }
    if (is_yours) {
      current->yours.push_back(hunk);
    } else {
      current->other.push_back(hunk);
    }
  };
  while (i < yours_hunks.size() || j < other_hunks.size()) {
    if (j >= other_hunks.size() || (i < yours_hunks.size() && yours_hunks[i].a_begin <= other_hunks[j].a_begin)) {
      absorb(yours_hunks[i], true);
      ++i;
    } else {
      absorb(other_hunks[j], false);
      ++j;
    }
  }
  return groups;
}

}  

int LineMerge::conflicts() const {
  int count = 0;
  for (const MergeChunk& chunk : chunks) {
    if (chunk.kind == ChunkKind::conflict) ++count;
  }
  return count;
}

LineMerge merge_lines(std::string_view base, std::string_view yours, std::string_view other) {
  LineMerge merge;
  merge.base = split_lines(base);
  merge.yours = split_lines(yours);
  merge.other = split_lines(other);

  std::vector<LineHunk> yours_hunks, other_hunks;
  const bool ok_yours = diff_lines(merge.base, merge.yours, kMergeCap, &yours_hunks);
  const bool ok_other = diff_lines(merge.base, merge.other, kMergeCap, &other_hunks);
  if (!ok_yours || !ok_other) {
    merge.too_many = true;
    return merge;
  }

  const std::vector<Group> groups = group_hunks(yours_hunks, other_hunks);
  const int base_size = static_cast<int>(merge.base.size());

  int pos = 0;
  int offset_yours = 0, offset_other = 0;
  for (const Group& group : groups) {
    if (group.base_begin > pos) {
      merge.chunks.push_back(MergeChunk{ChunkKind::same, pos, group.base_begin, pos + offset_yours,
                                         group.base_begin + offset_yours, pos + offset_other,
                                         group.base_begin + offset_other});
    }

    int yours_begin, yours_end, other_begin, other_end;
    if (!group.yours.empty()) {
      const LineHunk& first = group.yours.front();
      const LineHunk& last = group.yours.back();
      yours_begin = first.b_begin - (first.a_begin - group.base_begin);
      yours_end = last.b_end + (group.base_end - last.a_end);
      offset_yours = last.b_end - last.a_end;
    } else {
      yours_begin = group.base_begin + offset_yours;
      yours_end = group.base_end + offset_yours;
    }
    if (!group.other.empty()) {
      const LineHunk& first = group.other.front();
      const LineHunk& last = group.other.back();
      other_begin = first.b_begin - (first.a_begin - group.base_begin);
      other_end = last.b_end + (group.base_end - last.a_end);
      offset_other = last.b_end - last.a_end;
    } else {
      other_begin = group.base_begin + offset_other;
      other_end = group.base_end + offset_other;
    }

    ChunkKind kind;
    if (!group.yours.empty() && group.other.empty()) {
      kind = ChunkKind::yours;
    } else if (group.yours.empty() && !group.other.empty()) {
      kind = ChunkKind::other;
    } else {
      bool same_bytes = (yours_end - yours_begin) == (other_end - other_begin);
      for (int i = 0; same_bytes && i < yours_end - yours_begin; ++i) {
        if (merge.yours[static_cast<std::size_t>(yours_begin + i)] !=
            merge.other[static_cast<std::size_t>(other_begin + i)]) {
          same_bytes = false;
        }
      }
      kind = same_bytes ? ChunkKind::both_same : ChunkKind::conflict;
    }

    merge.chunks.push_back(
        MergeChunk{kind, group.base_begin, group.base_end, yours_begin, yours_end, other_begin, other_end});
    pos = group.base_end;
  }
  if (pos < base_size) {
    merge.chunks.push_back(MergeChunk{ChunkKind::same, pos, base_size, pos + offset_yours, base_size + offset_yours,
                                       pos + offset_other, base_size + offset_other});
  }
  return merge;
}

std::string merge_result(const LineMerge& merge, const std::vector<Pick>& picks) {
  std::string result;
  auto append = [&](const std::vector<std::string_view>& lines, int begin, int end) {
    for (int i = begin; i < end; ++i) result.append(lines[static_cast<std::size_t>(i)]);
  };
  for (std::size_t i = 0; i < merge.chunks.size(); ++i) {
    const MergeChunk& chunk = merge.chunks[i];
    if (chunk.kind == ChunkKind::same || chunk.kind == ChunkKind::both_same) {
      append(merge.yours, chunk.yours_begin, chunk.yours_end);
      continue;
    }
    const Pick pick = picks.at(i);
    if (pick == Pick::yours || pick == Pick::both) append(merge.yours, chunk.yours_begin, chunk.yours_end);
    if (pick == Pick::other || pick == Pick::both) append(merge.other, chunk.other_begin, chunk.other_end);
  }
  return result;
}

std::vector<Pick> default_picks(const LineMerge& merge) {
  std::vector<Pick> picks;
  picks.reserve(merge.chunks.size());
  for (const MergeChunk& chunk : merge.chunks) {
    switch (chunk.kind) {
      case ChunkKind::yours:
        picks.push_back(Pick::yours);
        break;
      case ChunkKind::other:
        picks.push_back(Pick::other);
        break;
      case ChunkKind::conflict:
        picks.push_back(Pick::open);
        break;
      case ChunkKind::same:
      case ChunkKind::both_same:
        picks.push_back(Pick::yours);
        break;
    }
  }
  return picks;
}
