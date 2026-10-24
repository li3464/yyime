#include "converter.h"

#include <algorithm>
#include <climits>
#include <unordered_map>
#include <unordered_set>

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
constexpr int kTopPerNode = 24;      // 简拼、模糊音、补全的节点最多取几个词进候选
constexpr int kRawCost = 25000;      // 认不出的字母，组句时按一个很生僻的字算
constexpr size_t kMaxCands = 400;  // 候选到这么多就不再加简拼、模糊音这些猜的；拼完整的字词不受限

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

  const bool grammar = opt.useGrammar && d.gram.loaded();
  std::vector<std::unordered_map<int, std::vector<BestWord>>> best(size_t(n) + 1);
  std::vector<StartMatch> starts;
  for (int s = 0; s < n; ++s) {
    if (lat.out[size_t(s)].empty()) continue;
    for (const TrieView* t : tries) {
      if (t->empty()) continue;
      Path path;
      auto onMatch = [&](int end, const TrieNode& node, int pen, const Path& p) {
        auto [eb, ee] = t->entriesOf(node);
        if (!eb) return;
        auto& choices = best[size_t(s)][end];
        int taken = 0;
        for (auto entry = eb; entry != ee && taken < (grammar ? 8 : 1); ++entry, ++taken) {
          BestWord bw;
          bw.cost = int(entry->cost) + pen;
          bw.text = t->textOf(*entry);
          if (bw.text.empty()) continue;
          bw.syls = p.syls;
          bw.edges = p.edges;
          auto same = std::find_if(choices.begin(), choices.end(), [&](const BestWord& old) {
            return old.text == bw.text;
          });
          if (same != choices.end()) {
            if (same->cost <= bw.cost) continue;
            *same = std::move(bw);
          } else choices.push_back(std::move(bw));
          std::sort(choices.begin(), choices.end(), [](const BestWord& x, const BestWord& y) {
            if (x.cost != y.cost) return x.cost < y.cost;
            return x.text < y.text;
          });
          if (choices.size() > size_t(grammar ? 8 : 1)) choices.resize(grammar ? 8 : 1);
        }
        if (s == 0) starts.push_back(StartMatch{end, pen, t, &node, p.syls});
      };
      Walk(*t, lat, s, *t->root(), 0, path, onMatch);
    }
  }

  // 有界束搜索：保留多条同音路径，搭配分与词频一起决定整句。
  struct Hypothesis {
    double cost = 0;
    std::wstring text;
    std::u32string context;
    std::vector<uint16_t> syls;
    std::string display;
    bool clean = true, learnable = true;
    int lastKind = -1;
  };
  const size_t width = grammar ? 10 : 1;
  std::vector<std::vector<Hypothesis>> beam(size_t(n) + 1);
  Hypothesis initial;
  initial.context = ToCodePoints(opt.context);
  if (initial.context.size() > 3) initial.context.erase(0, initial.context.size() - 3);
  beam[0].push_back(initial);
  auto push = [&](int end, Hypothesis h) {
    auto& dest = beam[size_t(end)];
    auto same = std::find_if(dest.begin(), dest.end(), [&](const Hypothesis& old) { return old.text == h.text; });
    if (same != dest.end()) {
      if (same->cost <= h.cost) return;
      *same = std::move(h);
    } else dest.push_back(std::move(h));
    std::sort(dest.begin(), dest.end(), [](const Hypothesis& x, const Hypothesis& y) {
      if (x.cost != y.cost) return x.cost < y.cost;
      return x.text < y.text;
    });
    if (dest.size() > width) dest.resize(width);
  };
  for (int pos = 0; pos < n; ++pos) {
    for (const auto& h : beam[size_t(pos)]) {
      for (int ei : lat.out[size_t(pos)]) {
        const auto& edge = lat.edges[size_t(ei)];
        if (edge.kind != EdgeKind::Sep) continue;
        Hypothesis next = h;
        if (!next.display.empty() && next.display.back() != '\'') next.display += '\'';
        next.lastKind = 0;
        push(edge.to, std::move(next));
      }
      for (const auto& [end, choices] : best[size_t(pos)]) {
        for (const auto& bw : choices) {
          Hypothesis next = h;
          auto codepoints = ToCodePoints(bw.text);
          next.cost += bw.cost;
          // 搭配分作为转移代价。不能给每次切词固定奖励，否则会偏向把“中国”拆成“中过”。
          if (grammar && !h.context.empty()) next.cost -= 650.0 * d.gram.Query(h.context, codepoints, end == n);
          next.text += bw.text;
          next.context += codepoints;
          if (next.context.size() > 3) next.context.erase(0, next.context.size() - 3);
          next.syls.insert(next.syls.end(), bw.syls.begin(), bw.syls.end());
          for (size_t k = 0; k < bw.edges.size(); ++k) {
            const auto& edge = lat.edges[size_t(bw.edges[k])];
            if (!next.display.empty() && next.display.back() != '\'') next.display += '\'';
            if (shuangpin && edge.kind == EdgeKind::Full) next.display += d.table.name(bw.syls[k]);
            else next.display += edge.spelling;
            if (edge.kind == EdgeKind::Abbr && edge.to != n) next.clean = false;
          }
          next.lastKind = 1;
          push(end, std::move(next));
        }
      }
      Hypothesis raw = h;
      raw.cost += kRawCost;
      raw.text += wchar_t((unsigned char)input[size_t(pos)]);
      if (raw.lastKind != 2 && !raw.display.empty() && raw.display.back() != '\'') raw.display += '\'';
      raw.display += input[size_t(pos)];
      raw.clean = raw.learnable = false;
      raw.lastKind = 2;
      raw.context.clear();
      push(pos + 1, std::move(raw));
    }
  }
  Candidate sentence;
  sentence.kind = CandKind::Sentence;
  sentence.consumed = n;
  const auto& winning = beam[size_t(n)].front();
  sentence.text = winning.text;
  sentence.cost = int(winning.cost);
  if (winning.learnable) sentence.syls = winning.syls;
  out->display = winning.display;
  const bool clean = winning.clean;
  out->clean = clean;

  // ---- 从头开始的词：覆盖得越长越靠前，同样长的按代价
  std::vector<Candidate> words;
  std::unordered_map<std::wstring, size_t> seen;
  std::unordered_set<std::wstring> exact;  // 拼完整拼对的（没走简拼、模糊音、补全）
  auto keyOf = [](std::wstring_view text, int end) {
    std::wstring key(text);
    key += wchar_t(1);
    key += wchar_t(end + 1);
    return key;
  };
  for (const StartMatch& m : starts) {
    auto [eb, ee] = m.trie->entriesOf(*m.node);
    // 完整拼对的音节（没走简拼、模糊音、补全）全列出来：shi 有七十多个字，只取前几十个会漏掉“诗”
    const int limit = m.penalty == 0 ? INT_MAX : kTopPerNode;
    int taken = 0;
    for (const TrieEntry* p = eb; p && p != ee && taken < limit; ++p, ++taken) {
      std::wstring_view text = m.trie->textOf(*p);
      if (text.empty()) continue;
      int cost = int(p->cost) + m.penalty;
      std::wstring key = keyOf(text, m.end);
      if (m.penalty == 0) exact.insert(key);
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
    // 候选太多时只丢简拼、模糊音这些猜出来的，拼完整的字词一个不丢
    if (out->cands.size() >= kMaxCands && !exact.count(keyOf(w.text, w.consumed))) continue;
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
