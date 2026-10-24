#pragma once
// 用户词库：记录选过的词和自己造的词，下次排到前面。
// 存成 %APPDATA%\yyime\user_dict.txt，每行一次学习记录：词<TAB>音节<TAB>次数增量。
// 每个用到输入法的进程都只往文件末尾追加，读的时候从上次读到的位置接着读，
// 所以别的程序里学到的词这边也能马上用上。
#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "dict.h"

namespace yy {

class SyllableTable;

struct UserTrie {
  std::vector<TrieNode> nodes;
  std::vector<TrieEntry> entries;
  std::wstring text;
  TrieView view() const;
};

class UserDict {
 public:
  UserDict();
  void Init(const std::wstring& path, const PinyinDict* main, const SyllableTable* table);
  void Refresh();  // 文件有新内容就读进来
  void Learn(std::wstring_view text, const std::vector<uint16_t>& syls, int delta = 1);
  void Forget(std::wstring_view text, const std::vector<uint16_t>& syls);
  std::shared_ptr<const UserTrie> Snapshot() const;
  size_t size() const;

  // 用户词的代价：原词库里有就在原代价上往前提，没有就当一个中等常用的词
  static int CostFor(int mainCost, int count);

 private:
  void ApplyLines(const std::string& data, size_t* consumed);
  void RebuildLocked();
  int MainCost(const std::vector<uint16_t>& syls, std::wstring_view text) const;

  using Key = std::pair<std::vector<uint16_t>, std::wstring>;
  std::wstring path_;
  const PinyinDict* main_ = nullptr;
  const SyllableTable* table_ = nullptr;
  uint64_t readOffset_ = 0;
  std::map<Key, int> counts_;
  std::shared_ptr<const UserTrie> trie_;
  mutable SRWLOCK lock_;
};

}  // namespace yy
