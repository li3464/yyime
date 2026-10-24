#pragma once
// 音节表、拼音/双拼切分。把用户按的键变成一张“音节图”（lattice）：
// 从第 i 个键到第 j 个键可以是哪些音节。后面查词、组句都在这张图上走。
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yy {

class PinyinDict;

// 音节集合（最多 512 个音节）
struct SylSet {
  std::array<uint64_t, 8> bits{};
  void set(int id) { bits[size_t(id) >> 6] |= 1ull << (id & 63); }
  bool test(int id) const { return (bits[size_t(id) >> 6] >> (id & 63)) & 1; }
  bool empty() const;
  int count() const;
  int first() const;  // 没有返回 -1
  void merge(const SylSet& o);
  template <typename F>
  void forEach(F&& f) const {
    for (size_t w = 0; w < bits.size(); ++w) {
      uint64_t b = bits[w];
      while (b) {
        int bit = __builtin_ctzll(b);
        f(int(w * 64 + bit));
        b &= b - 1;
      }
    }
  }
};

class SyllableTable {
 public:
  void Build(const PinyinDict& dict);
  int size() const { return int(names_.size()); }
  int Find(std::string_view s) const;
  std::string_view name(int id) const { return names_[size_t(id)]; }
  // 以 prefix 开头的所有音节；没有返回 nullptr
  const SylSet* PrefixSet(std::string_view prefix) const;
  // 声母（含 y w）；零声母音节返回空串
  std::string_view InitialOf(int id) const;
  std::string_view FinalOf(int id) const;
  static bool IsInitial(std::string_view s);

 private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, int> index_;
  std::unordered_map<std::string, SylSet> prefixes_;
};

enum class EdgeKind : uint8_t {
  Full,    // 完整音节
  Abbr,    // 只打了声母（简拼）
  Prefix,  // 最后一个音节没打完
  Sep,     // 用户打的隔音符 '
};

struct LatticeEdge {
  int from = 0;
  int to = 0;
  EdgeKind kind = EdgeKind::Full;
  int penalty = 0;
  SylSet syls;
  std::string spelling;  // 显示用：完整音节显示音节名，简拼显示声母
};

struct Lattice {
  int length = 0;
  std::vector<LatticeEdge> edges;
  std::vector<std::vector<int>> out;  // 每个位置出发的边
  void Reset(int len) {
    length = len;
    edges.clear();
    out.assign(size_t(len) + 1, {});
  }
  void Add(LatticeEdge e) {
    out[size_t(e.from)].push_back(int(edges.size()));
    edges.push_back(std::move(e));
  }
};

enum class Scheme { Pinyin, Xiaohe, Ziranma, Mspy, Wubi86 };
Scheme SchemeFromName(std::wstring_view name);
const wchar_t* SchemeName(Scheme s);       // 配置里的名字：pinyin xiaohe ...
const wchar_t* SchemeLabel(Scheme s);      // 显示的名字：全拼 小鹤双拼 ...
inline bool IsShuangpin(Scheme s) { return s == Scheme::Xiaohe || s == Scheme::Ziranma || s == Scheme::Mspy; }

struct FuzzyRules {
  // 互相模糊的成对写法，例如 {"z","zh"}、{"an","ang"}
  std::vector<std::pair<std::string, std::string>> initials, finals;
  bool empty() const { return initials.empty() && finals.empty(); }
  static FuzzyRules Parse(std::wstring_view spec);  // "z=zh c=ch an=ang"
};

class ShuangpinScheme {
 public:
  void Build(Scheme s, const SyllableTable& table);
  // 两个键对应的音节（可能 0~2 个）
  const SylSet* Pair(char k1, char k2) const;
  // 单独一个键当作声母时的音节集合（结尾没打完的情况）
  const SylSet* Single(char k) const;
  bool UsesSemicolon() const { return semicolon_; }

 private:
  std::unordered_map<uint16_t, SylSet> pairs_;
  std::unordered_map<char, SylSet> singles_;
  bool semicolon_ = false;
};

// 惩罚值（和词库的代价同一单位：-ln 概率 * 1000）
constexpr int kAbbrPenalty = 3000;
constexpr int kPrefixPenalty = 1500;
constexpr int kFuzzyPenalty = 800;

void BuildPinyinLattice(std::string_view input, const SyllableTable& table, const FuzzyRules& fuzzy, Lattice* lat);
void BuildShuangpinLattice(std::string_view input, const ShuangpinScheme& scheme, const SyllableTable& table,
                           Lattice* lat);

}  // namespace yy
