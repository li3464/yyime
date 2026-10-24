#include "converter.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

#include "util.h"

namespace yy {

const ShuangpinScheme* Dictionaries::Shuangpin(Scheme s) const {
  switch (s) {
    case Scheme::Xiaohe: return &xiaohe;
    case Scheme::Ziranma: return &ziranma;
    case Scheme::Mspy: return &mspy;
    default: return nullptr;
  }
}

namespace {

constexpr size_t kMaxDepth = 24;     // 一个词最多多少个音节
constexpr int kTopPerNode = 24;      // 每个词典节点最多取几个词进候选
constexpr int kRawCost = 25000;      // 认不出的字母，组句时按一个很生僻的字算
constexpr size_t kMaxCands = 400;

struct Path {
  std::vector<uint16_t> syls;
  std::vector<int> edges;
};

struct BestWord {
  int cost = INT_MAX;
  std::wstring_view text;
  std::vector<uint16_t> syls;
  std::vector<int> edges;
};

struct StartMatch {
  int end;
  int penalty;
  const TrieView* trie;
  const TrieNode* node;
  std::vector<uint16_t> syls;
};

// 从 pos 出发沿音节图往前走，同时在前缀树里往下走，每走到一个有词的节点就回调。
template <typename F>
void Walk(const TrieView& trie, const Lattice& lat, int pos, const TrieNode& node, int penalty, Path& path,
          F& onMatch) {
  if (path.syls.size() >= kMaxDepth) return;
  for (int ei : lat.out[size_t(pos)]) {
    const LatticeEdge& e = lat.edges[size_t(ei)];
    if (e.kind == EdgeKind::Sep) {
      // 隔音符只管音节怎么切，词可以跨过去（xi'an 还是能出“西安”）
      if (!path.syls.empty()) Walk(trie, lat, e.to, node, penalty, path, onMatch);
      continue;
    }
    const int pen = penalty + e.penalty;
    auto visit = [&](const TrieNode& c) {
      path.syls.push_back(c.syl);
      path.edges.push_back(ei);
      if (c.entryCount) onMatch(e.to, c, pen, path);
      Walk(trie, lat, e.to, c, pen, path, onMatch);
      path.syls.pop_back();
      path.edges.pop_back();
    };
    if (e.kind == EdgeKind::Full && e.syls.count() <= 4) {
      e.syls.forEach([&](int id) {
        if (const TrieNode* c = trie.child(node, uint16_t(id))) visit(*c);
      });
    } else {
      auto [b, en] = trie.children(node);
      for (const TrieNode* c = b; c && c != en; ++c)
        if (e.syls.test(c->syl)) visit(*c);
    }
  }
}

bool IsPinyinInput(std::string_view s, bool semicolon) {
  for (char c : s)
    if (!IsAsciiLower(c) && c != '\'' && !(semicolon && c == ';')) return false;
  return true;
}

}  // namespace

void AddEnglish(const Dictionaries& d, std::string_view input, bool clean, Conversion* out) {
  if (!d.english.loaded() || input.size() < 2 || input.size() > 30) return;
  std::string lower;
  for (char c : input) {
    if (!IsAsciiAlpha(c)) return;
    lower += AsciiLower(c);
  }
  auto [b, e] = d.english.PrefixRange(lower);
  std::vector<Candidate> found;
  for (auto p = b; p != e && found.size() < 2; ++p) {
    if (p->textLen == lower.size()) {
      Candidate c;
      c.text = std::wstring(d.english.textOf(*p));
      c.consumed = int(input.size());
      c.kind = CandKind::English;
      found.push_back(std::move(c));
    }
  }
  if (!clean && input.size() >= 3) {
    std::vector<const EnglishEntry*> longer;
    for (auto p = b; p != e && longer.size() < 2000; ++p)
      if (p->textLen > lower.size()) longer.push_back(p);
    std::stable_sort(longer.begin(), longer.end(),
                     [](const EnglishEntry* x, const EnglishEntry* y) { return x->textLen < y->textLen; });
    for (size_t i = 0; i < longer.size() && i < 2; ++i) {
      Candidate c;
      c.text = std::wstring(d.english.textOf(*longer[i]));
      c.consumed = int(input.size());
      c.kind = CandKind::English;
      found.push_back(std::move(c));
    }
  }
  if (found.empty()) return;
  // 去掉拼音候选里一模一样的（比如字母本身）
  for (auto& f : found) {
    out->cands.erase(std::remove_if(out->cands.begin(), out->cands.end(),
                                    [&](const Candidate& c) { return c.text == f.text; }),
                     out->cands.end());
  }
  size_t at = clean ? std::min<size_t>(1, out->cands.size()) : 0;
  out->cands.insert(out->cands.begin() + long(at), found.begin(), found.end());
}

void ConvertPinyin(const Dictionaries& d, std::string_view input, const ConvertOptions& opt, Conversion* out) {
  out->cands.clear();
  out->display.clear();
  out->clean = false;
  const int n = int(input.size());
  if (n == 0) return;
  const bool shuangpin = IsShuangpin(opt.scheme);
  if (!d.pinyin.loaded() || !IsPinyinInput(input, opt.scheme == Scheme::Mspy)) {
    // 带了大写字母、数字之类的：只给原样和英文单词
    out->display = std::string(input);
    Candidate raw;
    raw.text = Utf8ToWide(input);
    raw.consumed = n;
    raw.kind = CandKind::Raw;
    out->cands.push_back(std::move(raw));
    if (opt.englishMixed) AddEnglish(d, input, false, out);
    return;
  }

  Lattice lat;
  static const FuzzyRules kNoFuzzy;
  if (shuangpin) BuildShuangpinLattice(input, *d.Shuangpin(opt.scheme), d.table, &lat);
  else BuildPinyinLattice(input, d.table, opt.fuzzy ? *opt.fuzzy : kNoFuzzy, &lat);

  TrieView sys = d.pinyin.trie();
  TrieView usr = opt.user ? opt.user->view() : TrieView{};
  const TrieView* tries[2] = {&sys, &usr};

  std::vector<std::unordered_map<int, BestWord>> best(size_t(n) + 1);
  std::vector<StartMatch> starts;
  for (int s = 0; s < n; ++s) {
    if (lat.out[size_t(s)].empty()) continue;
    for (const TrieView* t : tries) {
      if (t->empty()) continue;
      Path path;
      auto onMatch = [&](int end, const TrieNode& node, int pen, const Path& p) {
        auto [eb, ee] = t->entriesOf(node);
        if (!eb) return;
        int c = int(eb->cost) + pen;
        BestWord& bw = best[size_t(s)][end];
        if (c < bw.cost) {
          bw.cost = c;
          bw.text = t->textOf(*eb);
          bw.syls = p.syls;
          bw.edges = p.edges;
        }
        if (s == 0) starts.push_back(StartMatch{end, pen, t, &node, p.syls});
      };
      Walk(*t, lat, s, *t->root(), 0, path, onMatch);
    }
  }

  // ---- 组整句：从头到尾代价最小的走法
  std::vector<int> dist(size_t(n) + 1, INT_MAX);
  struct Back {
    int from = -1;
    int kind = -1;  // 0 隔音符 1 词 2 认不出的字母
  };
  std::vector<Back> back(size_t(n) + 1);
  dist[0] = 0;
  for (int p = 0; p < n; ++p) {
    if (dist[size_t(p)] == INT_MAX) continue;
    auto relax = [&](int to, int cost, int kind) {
      if (cost < dist[size_t(to)]) {
        dist[size_t(to)] = cost;
        back[size_t(to)] = Back{p, kind};
      }
    };
    for (int ei : lat.out[size_t(p)])
      if (lat.edges[size_t(ei)].kind == EdgeKind::Sep) relax(lat.edges[size_t(ei)].to, dist[size_t(p)], 0);
    for (auto& [end, bw] : best[size_t(p)]) relax(end, dist[size_t(p)] + bw.cost, 1);
    relax(p + 1, dist[size_t(p)] + kRawCost, 2);
  }
  std::vector<std::pair<int, int>> steps;  // (起点, 终点)
  for (int p = n; p > 0; p = back[size_t(p)].from) steps.emplace_back(back[size_t(p)].from, p);
  std::reverse(steps.begin(), steps.end());

  Candidate sentence;
  sentence.kind = CandKind::Sentence;
  sentence.consumed = n;
  sentence.cost = dist[size_t(n)];
  bool learnable = true, clean = true;
  int prevKind = -1;
  for (auto [from, to] : steps) {
    int kind = back[size_t(to)].kind;
    if (kind == 0) {
      if (!out->display.empty() && out->display.back() != '\'') out->display += '\'';
    } else if (kind == 1) {
      const BestWord& bw = best[size_t(from)].at(to);
      sentence.text += bw.text;
      sentence.syls.insert(sentence.syls.end(), bw.syls.begin(), bw.syls.end());
      for (size_t k = 0; k < bw.edges.size(); ++k) {
        const LatticeEdge& e = lat.edges[size_t(bw.edges[k])];
        if (!out->display.empty() && out->display.back() != '\'') out->display += '\'';
        if (shuangpin && e.kind == EdgeKind::Full) out->display += d.table.name(bw.syls[k]);
        else out->display += e.spelling;
        if (e.kind == EdgeKind::Abbr && e.to != n) clean = false;
      }
    } else {
      sentence.text += wchar_t((unsigned char)input[size_t(from)]);
      learnable = false;
      clean = false;
      if (prevKind != 2 && !out->display.empty() && out->display.back() != '\'') out->display += '\'';
      out->display += input[size_t(from)];
    }
    prevKind = kind;
  }
  if (!learnable) sentence.syls.clear();
  out->clean = clean;

  // ---- 从头开始的词：覆盖得越长越靠前，同样长的按代价
  std::vector<Candidate> words;
  std::unordered_map<std::wstring, size_t> seen;
  for (const StartMatch& m : starts) {
    auto [eb, ee] = m.trie->entriesOf(*m.node);
    int taken = 0;
    for (const TrieEntry* p = eb; p && p != ee && taken < kTopPerNode; ++p, ++taken) {
      std::wstring_view text = m.trie->textOf(*p);
      if (text.empty()) continue;
      int cost = int(p->cost) + m.penalty;
      std::wstring key(text);
      key += wchar_t(1);
      key += wchar_t(m.end + 1);
      auto it = seen.find(key);
      if (it != seen.end()) {
        if (cost < words[it->second].cost) {
          words[it->second].cost = cost;
          words[it->second].syls = m.syls;
        }
        continue;
      }
      seen.emplace(std::move(key), words.size());
      Candidate c;
      c.text = std::wstring(text);
      c.consumed = m.end;
      c.cost = cost;
      c.syls = m.syls;
      words.push_back(std::move(c));
    }
  }
  std::sort(words.begin(), words.end(), [](const Candidate& a, const Candidate& b) {
    if (a.consumed != b.consumed) return a.consumed > b.consumed;
    return a.cost < b.cost;
  });

  bool sentenceUseful = !sentence.text.empty();
  if (sentenceUseful && !words.empty() && words[0].consumed == n && words[0].text == sentence.text) {
    sentenceUseful = false;
  }
  if (sentenceUseful) {
    words.erase(std::remove_if(words.begin(), words.end(),
                               [&](const Candidate& c) { return c.consumed == n && c.text == sentence.text; }),
                words.end());
    out->cands.push_back(std::move(sentence));
  }
  for (auto& w : words) {
    if (out->cands.size() >= kMaxCands) break;
    out->cands.push_back(std::move(w));
  }
  if (opt.englishMixed && input.find('\'') == std::string_view::npos) AddEnglish(d, input, clean, out);
}

void ConvertWubi(const Dictionaries& d, std::string_view input, Conversion* out) {
  out->cands.clear();
  out->display = std::string(input);
  out->clean = true;
  if (!d.wubi.loaded() || input.empty() || input.size() > 4) return;
  auto [b, e] = d.wubi.PrefixRange(input);
  std::vector<const WubiEntry*> more;
  std::vector<std::wstring_view> seen;
  auto isNew = [&](std::wstring_view t) {
    if (t.empty() || std::find(seen.begin(), seen.end(), t) != seen.end()) return false;
    seen.push_back(t);
    return true;
  };
  for (auto p = b; p != e; ++p) {
    std::string_view code = WubiDict::codeOf(*p);
    if (code.size() == input.size()) {
      if (!isNew(d.wubi.textOf(*p))) continue;
      Candidate c;
      c.text = std::wstring(d.wubi.textOf(*p));
      c.consumed = int(input.size());
      c.cost = p->cost;
      c.kind = CandKind::Wubi;
      out->cands.push_back(std::move(c));
    } else {
      more.push_back(p);
    }
  }
  // 后面接几个“再打几码就能出”的字词，备注里写剩下的编码（同一个字只留最短的那个码）
  std::stable_sort(more.begin(), more.end(), [](const WubiEntry* x, const WubiEntry* y) {
    if (x->cost != y->cost) return x->cost < y->cost;
    return WubiDict::codeOf(*x).size() < WubiDict::codeOf(*y).size();
  });
  int added = 0;
  for (size_t i = 0; i < more.size() && added < 12; ++i) {
    if (!isNew(d.wubi.textOf(*more[i]))) continue;
    Candidate c;
    c.text = std::wstring(d.wubi.textOf(*more[i]));
    c.comment = Utf8ToWide(WubiDict::codeOf(*more[i]).substr(input.size()));
    c.consumed = int(input.size());
    c.cost = more[i]->cost;
    c.kind = CandKind::Wubi;
    out->cands.push_back(std::move(c));
    ++added;
  }
}

}  // namespace yy
