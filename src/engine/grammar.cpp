#include "grammar.h"

#include <algorithm>
#include <cstring>

namespace yy {

namespace {

// darts-clone 双数组的节点
inline bool HasLeaf(uint32_t u) { return ((u >> 8) & 1) == 1; }
inline uint32_t Value(uint32_t u) { return u & ((1u << 31) - 1); }
inline uint32_t Label(uint32_t u) { return u & ((1u << 31) | 0xFF); }
inline uint32_t Offset(uint32_t u) { return (u >> 10) << ((u & (1u << 9)) >> 6); }

constexpr int kMaxResults = 8;
constexpr int kMaxCollocation = 4;  // collocation_max_length
constexpr int kMinCollocation = 3;  // collocation_min_length
constexpr double kValueScale = 10000;

// octagram 自己的字符编码（gram_encoding.cc）
void EncodeChar(char32_t u, std::string& e) {
  if (u < 0x80) {
    e += u == 0 ? char(0xE0) : char(u);
  } else if (u >= 0x4000 && u < 0xA000) {
    if ((u & 0xFF) == 0) {
      e += char(0xE1);
      e += char((u >> 8) + 0x40);
    } else {
      e += char((u >> 8) + 0x40);
      e += char(u & 0xFF);
    }
  } else {
    uint32_t v = uint32_t(u);
    int bits = 32;
    while (bits > 0 && (v & 0xFE000000u) == 0) {
      bits -= 7;
      v <<= 7;
    }
    int bytes = (bits + 6) / 7;
    e += char(0xE0 | bytes);
    while (bytes > 0) {
      --bytes;
      e += char(((v >> 25) & 0x7F) | 0x80);
      v <<= 7;
    }
  }
}

std::string Encode(std::u32string_view s) {
  std::string e;
  for (char32_t c : s) EncodeChar(c, e);
  return e;
}

size_t Advance(const std::string& s, size_t p) {
  unsigned char c = (unsigned char)s[p];
  return p + ((c & 0x80) == 0 ? 1 : (c & 0xF0) == 0xE0 ? (c & 0x0F) + 1 : 2);
}

int CharCount(const std::string& encoded, size_t length) {
  int n = 0;
  for (size_t p = 0; p < length; p = Advance(encoded, p)) ++n;
  return n;
}

double Scale(int value) { return value >= 0 ? double(value) / kValueScale : -1; }

}  // namespace

std::u32string ToCodePoints(std::wstring_view s) {
  std::u32string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (char32_t(s[i + 1]) - 0xDC00);
      ++i;
    }
    out += c;
  }
  return out;
}

bool GramModel::Load(const std::wstring& path) {
  units_ = nullptr;
  unitCount_ = 0;
  if (!file_.Open(path)) return false;
  const uint8_t* d = file_.data();
  size_t size = file_.size();
  if (size < 44 || memcmp(d, "Rime::Grammar/", 14) != 0) {
    file_.Close();
    return false;
  }
  uint32_t count;
  int32_t offset;
  memcpy(&count, d + 36, 4);
  memcpy(&offset, d + 40, 4);
  size_t start = size_t(40 + int64_t(offset));
  if (offset <= 0 || start % 4 != 0 || start > size || (size - start) / 4 < count || count == 0) {
    file_.Close();
    return false;
  }
  units_ = reinterpret_cast<const uint32_t*>(d + start);
  unitCount_ = count;
  return true;
}

bool GramModel::Traverse(const char* key, size_t* nodePos) const {
  size_t id = *nodePos;
  if (id >= unitCount_) return false;
  uint32_t unit = units_[id];
  for (size_t i = 0; key[i]; ++i) {
    unsigned char c = (unsigned char)key[i];
    id ^= size_t(Offset(unit)) ^ c;
    if (id >= unitCount_) return false;
    unit = units_[id];
    if (Label(unit) != c) return false;
    *nodePos = id;
  }
  return true;
}

int GramModel::PrefixSearch(const char* key, size_t nodePos, Match* out, int maxOut) const {
  if (nodePos >= unitCount_) return 0;
  int n = 0;
  uint32_t unit = units_[nodePos];
  size_t pos = nodePos ^ Offset(unit);
  for (size_t i = 0; key[i]; ++i) {
    unsigned char c = (unsigned char)key[i];
    pos ^= c;
    if (pos >= unitCount_) break;
    unit = units_[pos];
    if (Label(unit) != c) break;
    pos ^= Offset(unit);
    if (HasLeaf(unit)) {
      if (pos >= unitCount_) break;
      if (n < maxOut) out[n++] = Match{int(Value(units_[pos])), i + 1};
    }
  }
  return n;
}

int GramModel::Lookup(const std::string& context, const std::string& word, Match* out) const {
  size_t node = 0;
  if (!Traverse(context.c_str(), &node)) return 0;
  return PrefixSearch(word.c_str(), node, out, kMaxResults);
}

double GramModel::Query(std::u32string_view context, std::u32string_view word, bool isRear) const {
  double result = kNonCollocation;
  if (!loaded() || context.empty() || word.empty()) return result;
  const int n = std::min(8, kMaxCollocation - 1);
  int contextLen = int(std::min<size_t>(size_t(n), context.size()));
  std::string contextQuery = Encode(context.substr(context.size() - size_t(contextLen)));
  int wordQueryLen = int(std::min<size_t>(size_t(n), word.size()));
  std::string wordQuery = Encode(word.substr(0, size_t(wordQueryLen)));
  Match matches[kMaxResults];
  size_t ptr = 0;
  for (; contextLen > 0; --contextLen, ptr = Advance(contextQuery, ptr)) {
    int found = Lookup(contextQuery.substr(ptr), wordQuery, matches);
    for (int i = 0; i < found; ++i) {
      int matchLen = CharCount(wordQuery, matches[i].length);
      bool strong = contextLen + matchLen >= kMinCollocation || (ptr == 0 && matches[i].length == wordQuery.size());
      result = std::max(result, Scale(matches[i].value) + (strong ? kCollocation : kWeakCollocation));
    }
  }
  if (isRear && size_t(wordQueryLen) == word.size()) {
    if (Lookup(wordQuery, "$", matches) > 0) result = std::max(result, Scale(matches[0].value) + kRear);
  }
  return result;
}

}  // namespace yy
