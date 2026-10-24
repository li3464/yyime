#pragma once
// 二进制词库（tools/build_dict.py 生成）的只读访问。文件用内存映射打开，
// 同一台电脑上所有进程共享同一份物理内存。
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace yy {

#pragma pack(push, 1)
struct TrieNode {
  uint32_t firstChild;
  uint32_t firstEntry;
  uint32_t entryCount;
  uint16_t syl;
  uint16_t childCount;
};
struct TrieEntry {
  uint32_t textOff;
  uint16_t textLen;
  uint16_t cost;  // -ln(概率) * 1000，越小越常用
};
struct WubiEntry {
  char code[4];
  uint32_t textOff;
  uint16_t textLen;
  uint16_t cost;
};
struct EnglishEntry {
  uint32_t textOff;
  uint16_t textLen;
  uint16_t flags;  // bit0: 有大写字母
};
#pragma pack(pop)
static_assert(sizeof(TrieNode) == 16, "layout");
static_assert(sizeof(TrieEntry) == 8, "layout");
static_assert(sizeof(WubiEntry) == 12, "layout");
static_assert(sizeof(EnglishEntry) == 8, "layout");

class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile() { Close(); }
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  bool Open(const std::wstring& path);
  void Close();
  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  HANDLE file_ = INVALID_HANDLE_VALUE;
  HANDLE mapping_ = nullptr;
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

// 按音节 id 建的前缀树。系统词库的数据在映射文件里，用户词库的在内存 vector 里，
// 两边都用这个视图访问。所有访问都做了越界检查，坏文件不会把宿主程序弄崩。
struct TrieView {
  const TrieNode* nodes = nullptr;
  uint32_t nodeCount = 0;
  const TrieEntry* entries = nullptr;
  uint32_t entryCount = 0;
  const wchar_t* text = nullptr;
  uint32_t textSize = 0;

  bool empty() const { return nodeCount == 0; }
  const TrieNode* root() const { return nodeCount ? nodes : nullptr; }
  std::pair<const TrieNode*, const TrieNode*> children(const TrieNode& n) const;
  const TrieNode* child(const TrieNode& n, uint16_t syl) const;
  std::pair<const TrieEntry*, const TrieEntry*> entriesOf(const TrieNode& n) const;
  std::wstring_view textOf(const TrieEntry& e) const;
  const TrieNode* find(const uint16_t* syls, size_t count) const;
};

class PinyinDict {
 public:
  bool Load(const std::wstring& path);
  bool loaded() const { return !trie_.empty(); }
  const TrieView& trie() const { return trie_; }
  int syllableCount() const { return sylCount_; }
  std::string_view syllable(int id) const;

 private:
  MappedFile file_;
  TrieView trie_;
  const char* syls_ = nullptr;
  int sylCount_ = 0;
};

class WubiDict {
 public:
  bool Load(const std::wstring& path);
  bool loaded() const { return count_ != 0; }
  // 编码以 prefix 开头的所有条目（按编码、再按词频排好）。
  std::pair<const WubiEntry*, const WubiEntry*> PrefixRange(std::string_view prefix) const;
  std::wstring_view textOf(const WubiEntry& e) const;
  static std::string_view codeOf(const WubiEntry& e);

 private:
  MappedFile file_;
  const WubiEntry* entries_ = nullptr;
  uint32_t count_ = 0;
  const wchar_t* text_ = nullptr;
  uint32_t textSize_ = 0;
};

class EnglishDict {
 public:
  bool Load(const std::wstring& path);
  bool loaded() const { return count_ != 0; }
  // 小写形式以 lowerPrefix 开头的单词。
  std::pair<const EnglishEntry*, const EnglishEntry*> PrefixRange(std::string_view lowerPrefix) const;
  std::wstring_view textOf(const EnglishEntry& e) const;

 private:
  MappedFile file_;
  const EnglishEntry* entries_ = nullptr;
  uint32_t count_ = 0;
  const wchar_t* text_ = nullptr;
  uint32_t textSize_ = 0;
};

}  // namespace yy
