#include "dict.h"

#include <algorithm>
#include <cstring>

namespace yy {

bool MappedFile::Open(const std::wstring& path) {
  Close();
  file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file_ == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file_, &size) || size.QuadPart < 64 || size.QuadPart > (1ll << 30)) {
    Close();
    return false;
  }
  mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapping_) {
    Close();
    return false;
  }
  data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
  if (!data_) {
    Close();
    return false;
  }
  size_ = size_t(size.QuadPart);
  return true;
}

void MappedFile::Close() {
  if (data_) UnmapViewOfFile(data_);
  if (mapping_) CloseHandle(mapping_);
  if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  data_ = nullptr;
  mapping_ = nullptr;
  file_ = INVALID_HANDLE_VALUE;
  size_ = 0;
}

// ---------------------------------------------------------------- TrieView

std::pair<const TrieNode*, const TrieNode*> TrieView::children(const TrieNode& n) const {
  if (n.childCount == 0 || n.firstChild >= nodeCount || nodeCount - n.firstChild < n.childCount) {
    return {nullptr, nullptr};
  }
  return {nodes + n.firstChild, nodes + n.firstChild + n.childCount};
}

const TrieNode* TrieView::child(const TrieNode& n, uint16_t syl) const {
  auto [b, e] = children(n);
  if (!b) return nullptr;
  auto it = std::lower_bound(b, e, syl, [](const TrieNode& x, uint16_t s) { return x.syl < s; });
  return (it != e && it->syl == syl) ? it : nullptr;
}

std::pair<const TrieEntry*, const TrieEntry*> TrieView::entriesOf(const TrieNode& n) const {
  if (n.entryCount == 0 || n.firstEntry >= entryCount || entryCount - n.firstEntry < n.entryCount) {
    return {nullptr, nullptr};
  }
  return {entries + n.firstEntry, entries + n.firstEntry + n.entryCount};
}

std::wstring_view TrieView::textOf(const TrieEntry& e) const {
  if (e.textOff >= textSize || textSize - e.textOff < e.textLen) return {};
  return {text + e.textOff, e.textLen};
}

const TrieNode* TrieView::find(const uint16_t* syls, size_t count) const {
  const TrieNode* n = root();
  for (size_t i = 0; n && i < count; ++i) n = child(*n, syls[i]);
  return n;
}

// ---------------------------------------------------------------- 文件头

namespace {

template <typename T>
bool RangeOk(size_t fileSize, uint32_t off, uint64_t count) {
  return off <= fileSize && (fileSize - off) / sizeof(T) >= count && off % alignof(uint16_t) == 0;
}

uint32_t U32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

}  // namespace

bool PinyinDict::Load(const std::wstring& path) {
  if (!file_.Open(path)) return false;
  const uint8_t* d = file_.data();
  size_t size = file_.size();
  if (memcmp(d, "YYPYDIC1", 8) != 0) return false;
  uint32_t sylCount = U32(d + 8), nodeCount = U32(d + 12), entryCount = U32(d + 16), textSize = U32(d + 20);
  uint32_t offSyl = U32(d + 24), offNodes = U32(d + 28), offEntries = U32(d + 32), offText = U32(d + 36);
  if (sylCount == 0 || sylCount > 512 || !RangeOk<char[8]>(size, offSyl, sylCount) ||
      !RangeOk<TrieNode>(size, offNodes, nodeCount) || !RangeOk<TrieEntry>(size, offEntries, entryCount) ||
      !RangeOk<wchar_t>(size, offText, textSize) || nodeCount == 0) {
    file_.Close();
    return false;
  }
  syls_ = reinterpret_cast<const char*>(d + offSyl);
  sylCount_ = int(sylCount);
  trie_.nodes = reinterpret_cast<const TrieNode*>(d + offNodes);
  trie_.nodeCount = nodeCount;
  trie_.entries = reinterpret_cast<const TrieEntry*>(d + offEntries);
  trie_.entryCount = entryCount;
  trie_.text = reinterpret_cast<const wchar_t*>(d + offText);
  trie_.textSize = textSize;
  return true;
}

std::string_view PinyinDict::syllable(int id) const {
  if (id < 0 || id >= sylCount_) return {};
  const char* p = syls_ + size_t(id) * 8;
  return {p, strnlen(p, 8)};
}

template <typename Entry>
static bool LoadFlat(MappedFile& file, const std::wstring& path, const char* magic, const Entry** entries,
                     uint32_t* count, const wchar_t** text, uint32_t* textSize) {
  if (!file.Open(path)) return false;
  const uint8_t* d = file.data();
  size_t size = file.size();
  if (memcmp(d, magic, 8) != 0) return false;
  uint32_t n = U32(d + 8), ts = U32(d + 12), offEntries = U32(d + 16), offText = U32(d + 20);
  if (!RangeOk<Entry>(size, offEntries, n) || !RangeOk<wchar_t>(size, offText, ts)) {
    file.Close();
    return false;
  }
  *entries = reinterpret_cast<const Entry*>(d + offEntries);
  *count = n;
  *text = reinterpret_cast<const wchar_t*>(d + offText);
  *textSize = ts;
  return true;
}

bool WubiDict::Load(const std::wstring& path) {
  return LoadFlat(file_, path, "YYWBDIC1", &entries_, &count_, &text_, &textSize_);
}

std::string_view WubiDict::codeOf(const WubiEntry& e) { return {e.code, strnlen(e.code, 4)}; }

std::wstring_view WubiDict::textOf(const WubiEntry& e) const {
  if (e.textOff >= textSize_ || textSize_ - e.textOff < e.textLen) return {};
  return {text_ + e.textOff, e.textLen};
}

std::pair<const WubiEntry*, const WubiEntry*> WubiDict::PrefixRange(std::string_view prefix) const {
  const WubiEntry* b = entries_;
  const WubiEntry* e = entries_ + count_;
  auto lo = std::lower_bound(b, e, prefix, [](const WubiEntry& x, std::string_view p) { return codeOf(x) < p; });
  auto hi = std::find_if(lo, e, [&](const WubiEntry& x) { return codeOf(x).substr(0, prefix.size()) != prefix; });
  return {lo, hi};
}

bool EnglishDict::Load(const std::wstring& path) {
  return LoadFlat(file_, path, "YYENDIC1", &entries_, &count_, &text_, &textSize_);
}

std::wstring_view EnglishDict::textOf(const EnglishEntry& e) const {
  if (e.textOff >= textSize_ || textSize_ - e.textOff < e.textLen) return {};
  return {text_ + e.textOff, e.textLen};
}

// 比较单词的小写形式和 ASCII 前缀；返回 <0 / 0（是前缀）/ >0
static int ComparePrefix(std::wstring_view word, std::string_view prefix) {
  size_t n = std::min(word.size(), prefix.size());
  for (size_t i = 0; i < n; ++i) {
    wchar_t c = word[i];
    if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
    if (c != wchar_t((unsigned char)prefix[i])) return c < wchar_t((unsigned char)prefix[i]) ? -1 : 1;
  }
  return word.size() >= prefix.size() ? 0 : -1;
}

std::pair<const EnglishEntry*, const EnglishEntry*> EnglishDict::PrefixRange(std::string_view lowerPrefix) const {
  const EnglishEntry* b = entries_;
  const EnglishEntry* e = entries_ + count_;
  auto lo = std::lower_bound(b, e, lowerPrefix, [&](const EnglishEntry& x, std::string_view p) {
    return ComparePrefix(textOf(x), p) < 0;
  });
  auto hi = lo;
  while (hi != e && ComparePrefix(textOf(*hi), lowerPrefix) == 0) ++hi;
  return {lo, hi};
}

}  // namespace yy
