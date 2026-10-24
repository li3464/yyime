#include "pinyin.h"

#include "dict.h"
#include "util.h"

namespace yy {

// ---------------------------------------------------------------- SylSet

bool SylSet::empty() const {
  for (uint64_t b : bits)
    if (b) return false;
  return true;
}

int SylSet::count() const {
  int n = 0;
  for (uint64_t b : bits) n += __builtin_popcountll(b);
  return n;
}

int SylSet::first() const {
  for (size_t w = 0; w < bits.size(); ++w)
    if (bits[w]) return int(w * 64 + __builtin_ctzll(bits[w]));
  return -1;
}

void SylSet::merge(const SylSet& o) {
  for (size_t i = 0; i < bits.size(); ++i) bits[i] |= o.bits[i];
}

// ---------------------------------------------------------------- 音节表

static const char kConsonants[] = "bpmfdtnlgkhjqxrzcsyw";

static bool IsConsonant(char c) {
  for (const char* p = kConsonants; *p; ++p)
    if (*p == c) return true;
  return false;
}

bool SyllableTable::IsInitial(std::string_view s) {
  if (s == "zh" || s == "ch" || s == "sh") return true;
  return s.size() == 1 && IsConsonant(s[0]);
}

void SyllableTable::Build(const PinyinDict& dict) {
  names_.clear();
  index_.clear();
  prefixes_.clear();
  for (int id = 0; id < dict.syllableCount(); ++id) {
    std::string name(dict.syllable(id));
    names_.push_back(name);
    index_[name] = id;
    for (size_t len = 1; len <= name.size(); ++len) prefixes_[name.substr(0, len)].set(id);
  }
}

int SyllableTable::Find(std::string_view s) const {
  auto it = index_.find(std::string(s));
  return it == index_.end() ? -1 : it->second;
}

const SylSet* SyllableTable::PrefixSet(std::string_view prefix) const {
  auto it = prefixes_.find(std::string(prefix));
  return it == prefixes_.end() ? nullptr : &it->second;
}

std::string_view SyllableTable::InitialOf(int id) const {
  std::string_view n = names_[size_t(id)];
  if (n.size() >= 2 && (n.substr(0, 2) == "zh" || n.substr(0, 2) == "ch" || n.substr(0, 2) == "sh")) {
    return n.substr(0, 2);
  }
  if (!n.empty() && IsConsonant(n[0])) return n.substr(0, 1);
  return {};
}

std::string_view SyllableTable::FinalOf(int id) const {
  return std::string_view(names_[size_t(id)]).substr(InitialOf(id).size());
}

// ---------------------------------------------------------------- 方案名

Scheme SchemeFromName(std::wstring_view n) {
  if (n == L"xiaohe") return Scheme::Xiaohe;
  if (n == L"ziranma") return Scheme::Ziranma;
  if (n == L"mspy") return Scheme::Mspy;
  if (n == L"wubi86" || n == L"wubi") return Scheme::Wubi86;
  return Scheme::Pinyin;
}

const wchar_t* SchemeName(Scheme s) {
  switch (s) {
    case Scheme::Xiaohe: return L"xiaohe";
    case Scheme::Ziranma: return L"ziranma";
    case Scheme::Mspy: return L"mspy";
    case Scheme::Wubi86: return L"wubi86";
    default: return L"pinyin";
  }
}

const wchar_t* SchemeLabel(Scheme s) {
  switch (s) {
    case Scheme::Xiaohe: return L"小鹤双拼";
    case Scheme::Ziranma: return L"自然码双拼";
    case Scheme::Mspy: return L"微软双拼";
    case Scheme::Wubi86: return L"五笔86";
    default: return L"全拼";
  }
}

// ---------------------------------------------------------------- 模糊音

FuzzyRules FuzzyRules::Parse(std::wstring_view spec) {
  FuzzyRules r;
  std::string s = WideToUtf8(spec);
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == ';' || s[i] == '\t')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != ',' && s[j] != ';' && s[j] != '\t') ++j;
    std::string tok = s.substr(i, j - i);
    size_t eq = tok.find('=');
    if (eq != std::string::npos && eq > 0 && eq + 1 < tok.size()) {
      std::string a = tok.substr(0, eq), b = tok.substr(eq + 1);
      if (SyllableTable::IsInitial(a) && SyllableTable::IsInitial(b)) {
        r.initials.emplace_back(a, b);
      } else {
        r.finals.emplace_back(a, b);
      }
    }
    i = j;
  }
  return r;
}

static void FuzzyVariants(const SyllableTable& t, int id, const FuzzyRules& fz, std::vector<int>* out) {
  std::string ini(t.InitialOf(id)), fin(t.FinalOf(id));
  auto add = [&](const std::string& name) {
    int v = t.Find(name);
    if (v >= 0 && v != id) out->push_back(v);
  };
  for (auto& [a, b] : fz.initials) {
    if (ini == a) add(b + fin);
    if (ini == b) add(a + fin);
  }
  auto endsWith = [](const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
  };
  for (auto& [a, b] : fz.finals) {
    // 用后缀替换，这样 an=ang 也能管到 ian/iang、uan/uang
    if (endsWith(fin, b)) add(ini + fin.substr(0, fin.size() - b.size()) + a);
    else if (endsWith(fin, a)) add(ini + fin.substr(0, fin.size() - a.size()) + b);
  }
}

// ---------------------------------------------------------------- 全拼切分

static std::string NormalizeSpelling(std::string_view s) {
  if (s.size() >= 3 && s.substr(s.size() - 2) == "ue" && (s[0] == 'l' || s[0] == 'n') && s.size() == 3) {
    return std::string(1, s[0]) + "ve";  // lue/nue 按 lve/nve 处理
  }
  return std::string(s);
}

void BuildPinyinLattice(std::string_view input, const SyllableTable& table, const FuzzyRules& fuzzy, Lattice* lat) {
  const int n = int(input.size());
  lat->Reset(n);
  std::vector<int> variants;
  for (int i = 0; i < n; ++i) {
    char c = input[size_t(i)];
    if (c == '\'') {
      LatticeEdge e;
      e.from = i;
      e.to = i + 1;
      e.kind = EdgeKind::Sep;
      e.spelling = "'";
      lat->Add(std::move(e));
      continue;
    }
    if (!IsAsciiLower(c)) continue;
    int run = 0;
    while (i + run < n && IsAsciiLower(input[size_t(i + run)])) ++run;
    int maxLen = run < 6 ? run : 6;
    int longestFull = 0;
    for (int len = 1; len <= maxLen; ++len) {
      std::string_view sub = input.substr(size_t(i), size_t(len));
      int id = table.Find(NormalizeSpelling(sub));
      if (id < 0) continue;
      LatticeEdge e;
      e.from = i;
      e.to = i + len;
      e.kind = EdgeKind::Full;
      e.syls.set(id);
      e.spelling = std::string(sub);
      lat->Add(e);
      if (!fuzzy.empty()) {
        variants.clear();
        FuzzyVariants(table, id, fuzzy, &variants);
        if (!variants.empty()) {
          LatticeEdge f = e;
          f.syls = SylSet{};
          for (int v : variants) f.syls.set(v);
          f.penalty = kFuzzyPenalty;
          lat->Add(std::move(f));
        }
      }
      longestFull = len;
    }
    // 简拼：只在这里没有更长的完整音节时才允许（不然 zhong 会被拆成 z+h+...）
    bool twoLetterInitial = run >= 2 && SyllableTable::IsInitial(input.substr(size_t(i), 2));
    for (int len = 1; len <= 2 && len <= run; ++len) {
      std::string_view sub = input.substr(size_t(i), size_t(len));
      if (!SyllableTable::IsInitial(sub) || longestFull > len) continue;
      if (len == 1 && twoLetterInitial) continue;  // zh/ch/sh 开头时不再单拆 z/c/s
      const SylSet* set = table.PrefixSet(sub);
      if (!set) continue;
      LatticeEdge e;
      e.from = i;
      e.to = i + len;
      e.kind = EdgeKind::Abbr;
      e.penalty = kAbbrPenalty;
      e.syls = *set;
      e.spelling = std::string(sub);
      lat->Add(std::move(e));
    }
    // 结尾没打完的音节，比如 zhon、qio
    if (i + run == n && run >= 2 && run <= 6) {
      std::string_view rest = input.substr(size_t(i));
      if (table.Find(NormalizeSpelling(rest)) < 0 && !SyllableTable::IsInitial(rest)) {
        if (const SylSet* set = table.PrefixSet(rest)) {
          LatticeEdge e;
          e.from = i;
          e.to = n;
          e.kind = EdgeKind::Prefix;
          e.penalty = kPrefixPenalty;
          e.syls = *set;
          e.spelling = std::string(rest);
          lat->Add(std::move(e));
        }
      }
    }
  }
}

// ---------------------------------------------------------------- 双拼

// 每个方案的“韵母 → 键”表
static const char* FinalKeys(Scheme s) {
  switch (s) {
    case Scheme::Xiaohe:
      return "a:a o:o e:e i:i u:u v:v ai:d ei:w ao:c ou:z an:j en:f ang:h eng:g ong:s ia:x ie:p iao:n iu:q "
             "ian:m in:b iang:l ing:k iong:s ua:x uo:o uai:k ui:v uan:r un:y uang:l ue:t ve:t";
    case Scheme::Ziranma:
      return "a:a o:o e:e i:i u:u v:v ai:l ei:z ao:k ou:b an:j en:f ang:h eng:g ong:s ia:w ie:x iao:c iu:q "
             "ian:m in:n iang:d ing:y iong:s ua:w uo:o uai:y ui:v uan:r un:p uang:d ue:t ve:t";
    case Scheme::Mspy:
      return "a:a o:o e:e i:i u:u v:y ai:l ei:z ao:k ou:b an:j en:f ang:h eng:g ong:s er:r ia:w ie:x iao:c "
             "iu:q ian:m in:n iang:d ing:; iong:s ua:w uo:o uai:y ui:v uan:r un:p uang:d ue:t ve:v";
    default:
      return "";
  }
}

static uint16_t PairKey(char a, char b) { return uint16_t((uint8_t(a) << 8) | uint8_t(b)); }

void ShuangpinScheme::Build(Scheme s, const SyllableTable& table) {
  pairs_.clear();
  singles_.clear();
  semicolon_ = s == Scheme::Mspy;
  std::unordered_map<std::string, char> fk;
  std::string spec = FinalKeys(s);
  for (size_t i = 0; i < spec.size();) {
    size_t j = spec.find(' ', i);
    if (j == std::string::npos) j = spec.size();
    std::string tok = spec.substr(i, j - i);
    size_t colon = tok.find(':');
    if (colon != std::string::npos && colon + 1 < tok.size()) fk[tok.substr(0, colon)] = tok[colon + 1];
    i = j + 1;
  }
  auto finalKey = [&](std::string_view f) -> char {
    auto it = fk.find(std::string(f));
    return it == fk.end() ? 0 : it->second;
  };
  for (int id = 0; id < table.size(); ++id) {
    std::string_view ini = table.InitialOf(id), fin = table.FinalOf(id);
    char k1 = 0, k2 = 0;
    if (!ini.empty()) {
      k1 = ini == "zh" ? 'v' : ini == "ch" ? 'i' : ini == "sh" ? 'u' : ini[0];
      k2 = finalKey(fin);
      singles_[k1].set(id);
    } else if (s == Scheme::Mspy) {
      k1 = 'o';
      k2 = finalKey(fin);
      singles_['o'].set(id);
    } else {
      k1 = fin[0];
      k2 = fin.size() == 1 ? fin[0] : fin.size() == 2 ? fin[1] : finalKey(fin);
      singles_[k1].set(id);
    }
    if (k1 && k2) pairs_[PairKey(k1, k2)].set(id);
  }
}

const SylSet* ShuangpinScheme::Pair(char k1, char k2) const {
  auto it = pairs_.find(PairKey(k1, k2));
  return it == pairs_.end() ? nullptr : &it->second;
}

const SylSet* ShuangpinScheme::Single(char k) const {
  auto it = singles_.find(k);
  return it == singles_.end() ? nullptr : &it->second;
}

void BuildShuangpinLattice(std::string_view input, const ShuangpinScheme& scheme, const SyllableTable& table,
                           Lattice* lat) {
  const int n = int(input.size());
  lat->Reset(n);
  auto isKey = [&](char c) { return IsAsciiLower(c) || (c == ';' && scheme.UsesSemicolon()); };
  int i = 0;
  while (i < n) {
    char c = input[size_t(i)];
    if (c == '\'') {
      LatticeEdge e;
      e.from = i;
      e.to = i + 1;
      e.kind = EdgeKind::Sep;
      e.spelling = "'";
      lat->Add(std::move(e));
      ++i;
      continue;
    }
    if (!IsAsciiLower(c)) {
      ++i;
      continue;
    }
    if (i + 1 < n && isKey(input[size_t(i + 1)])) {
      if (const SylSet* set = scheme.Pair(c, input[size_t(i + 1)])) {
        LatticeEdge e;
        e.from = i;
        e.to = i + 2;
        e.kind = EdgeKind::Full;
        e.syls = *set;
        e.spelling = std::string(table.name(set->first()));
        lat->Add(std::move(e));
        i += 2;
        continue;
      }
    }
    if (const SylSet* set = scheme.Single(c)) {
      LatticeEdge e;
      e.from = i;
      e.to = i + 1;
      e.kind = EdgeKind::Abbr;
      e.penalty = kAbbrPenalty;
      e.syls = *set;
      e.spelling = std::string(table.InitialOf(set->first()));
      if (e.spelling.empty()) e.spelling = std::string(1, c);
      lat->Add(std::move(e));
    }
    ++i;
  }
}

}  // namespace yy
