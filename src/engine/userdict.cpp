#include "userdict.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <tuple>

#include "pinyin.h"
#include "util.h"

namespace yy {

TrieView UserTrie::view() const {
  TrieView v;
  v.nodes = nodes.data();
  v.nodeCount = uint32_t(nodes.size());
  v.entries = entries.data();
  v.entryCount = uint32_t(entries.size());
  v.text = text.data();
  v.textSize = uint32_t(text.size());
  return v;
}

UserDict::UserDict() {
  InitializeSRWLock(&lock_);
  trie_ = std::make_shared<UserTrie>();
}

void UserDict::Init(const std::wstring& path, const PinyinDict* main, const SyllableTable* table) {
  AcquireSRWLockExclusive(&lock_);
  path_ = path;
  main_ = main;
  table_ = table;
  readOffset_ = 0;
  counts_.clear();
  ReleaseSRWLockExclusive(&lock_);
  Refresh();
}

int UserDict::CostFor(int mainCost, int count) {
  int base = mainCost >= 0 ? mainCost : 10500;
  int c = base - int(800.0 * std::log(1.0 + std::max(count, 0)));
  return std::max(c, 0);
}

int UserDict::MainCost(const std::vector<uint16_t>& syls, std::wstring_view text) const {
  if (!main_ || !main_->loaded()) return -1;
  const TrieView& t = main_->trie();
  const TrieNode* n = t.find(syls.data(), syls.size());
  if (!n) return -1;
  auto [b, e] = t.entriesOf(*n);
  for (const TrieEntry* p = b; p && p != e; ++p)
    if (t.textOf(*p) == text) return p->cost;
  return -1;
}

void UserDict::ApplyLines(const std::string& data, size_t* consumed) {
  size_t pos = 0;
  while (true) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) break;  // 不完整的一行留到下次
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t t1 = line.find('\t');
    size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    std::wstring text = Utf8ToWide(line.substr(0, t1));
    std::string sylText = line.substr(t1 + 1, t2 - t1 - 1);
    int delta = atoi(line.c_str() + t2 + 1);
    std::vector<uint16_t> syls;
    bool ok = !text.empty() && delta != 0;
    for (size_t i = 0; ok && i < sylText.size();) {
      size_t sp = sylText.find(' ', i);
      if (sp == std::string::npos) sp = sylText.size();
      int id = table_ ? table_->Find(std::string_view(sylText).substr(i, sp - i)) : -1;
      if (id < 0) ok = false;
      else syls.push_back(uint16_t(id));
      i = sp + 1;
    }
    if (!ok || syls.empty()) continue;
    Key key{std::move(syls), std::move(text)};
    int& c = counts_[key];
    c += delta;
    if (c <= 0) counts_.erase(key);
  }
  *consumed = pos;
}

void UserDict::Refresh() {
  AcquireSRWLockExclusive(&lock_);
  if (!path_.empty() && table_) {
    HANDLE f = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER size{};
      GetFileSizeEx(f, &size);
      uint64_t total = uint64_t(size.QuadPart);
      if (total < readOffset_) {  // 文件被整理过，从头读
        counts_.clear();
        readOffset_ = 0;
      }
      if (total > readOffset_ && total - readOffset_ < (16u << 20)) {
        std::string data(size_t(total - readOffset_), '\0');
        LARGE_INTEGER off;
        off.QuadPart = LONGLONG(readOffset_);
        DWORD got = 0;
        if (SetFilePointerEx(f, off, nullptr, FILE_BEGIN) &&
            ReadFile(f, data.data(), DWORD(data.size()), &got, nullptr)) {
          data.resize(got);
          size_t consumed = 0;
          ApplyLines(data, &consumed);
          readOffset_ += consumed;
          RebuildLocked();
        }
      }
      CloseHandle(f);
    }
  }
  ReleaseSRWLockExclusive(&lock_);
}

void UserDict::Learn(std::wstring_view text, const std::vector<uint16_t>& syls, int delta) {
  if (text.empty() || syls.empty() || !table_) return;
  std::string line = WideToUtf8(text);
  line += '\t';
  for (size_t i = 0; i < syls.size(); ++i) {
    if (i) line += ' ';
    line += table_->name(syls[i]);
  }
  line += '\t';
  line += std::to_string(delta);
  line += '\n';
  if (!path_.empty() && AppendTextFile(path_, line)) {
    Refresh();
    return;
  }
  // 写不了文件（比如在沙盒里的应用）：只在内存里记
  AcquireSRWLockExclusive(&lock_);
  Key key{syls, std::wstring(text)};
  int& c = counts_[key];
  c += delta;
  if (c <= 0) counts_.erase(key);
  RebuildLocked();
  ReleaseSRWLockExclusive(&lock_);
}

void UserDict::Forget(std::wstring_view text, const std::vector<uint16_t>& syls) {
  int count = 0;
  AcquireSRWLockShared(&lock_);
  auto it = counts_.find(Key{syls, std::wstring(text)});
  if (it != counts_.end()) count = it->second;
  ReleaseSRWLockShared(&lock_);
  if (count > 0) Learn(text, syls, -count);
}

std::shared_ptr<const UserTrie> UserDict::Snapshot() const {
  AcquireSRWLockShared(&lock_);
  auto t = trie_;
  ReleaseSRWLockShared(&lock_);
  return t;
}

size_t UserDict::size() const {
  AcquireSRWLockShared(&lock_);
  size_t n = counts_.size();
  ReleaseSRWLockShared(&lock_);
  return n;
}

void UserDict::RebuildLocked() {
  // counts_ 按音节序列排好了序，同一个序列的词挨在一起
  std::vector<const std::vector<uint16_t>*> keys;
  std::vector<std::vector<std::pair<int, const std::wstring*>>> words;
  for (auto& [key, count] : counts_) {
    if (keys.empty() || *keys.back() != key.first) {
      keys.push_back(&key.first);
      words.emplace_back();
    }
    words.back().emplace_back(CostFor(MainCost(key.first, key.second), count), &key.second);
  }
  auto t = std::make_shared<UserTrie>();
  t->nodes.push_back(TrieNode{0, 0, 0, 0, 0});
  std::vector<int> keyOfNode{-1};
  std::deque<std::tuple<size_t, size_t, size_t, size_t>> queue;  // 节点, lo, hi, 深度
  queue.emplace_back(0, 0, keys.size(), 0);
  while (!queue.empty()) {
    auto [ni, lo, hi, depth] = queue.front();
    queue.pop_front();
    size_t i = lo;
    if (i < hi && keys[i]->size() == depth) {
      keyOfNode[ni] = int(i);
      ++i;
    }
    uint32_t first = uint32_t(t->nodes.size());
    uint16_t count = 0;
    while (i < hi) {
      uint16_t s = (*keys[i])[depth];
      size_t j = i + 1;
      while (j < hi && (*keys[j])[depth] == s) ++j;
      t->nodes.push_back(TrieNode{0, 0, 0, s, 0});
      keyOfNode.push_back(-1);
      queue.emplace_back(t->nodes.size() - 1, i, j, depth + 1);
      ++count;
      i = j;
    }
    t->nodes[ni].firstChild = first;
    t->nodes[ni].childCount = count;
  }
  for (size_t ni = 0; ni < t->nodes.size(); ++ni) {
    TrieNode& n = t->nodes[ni];
    n.firstEntry = uint32_t(t->entries.size());
    n.entryCount = 0;
    if (keyOfNode[ni] < 0) continue;
    auto& list = words[size_t(keyOfNode[ni])];
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [cost, text] : list) {
      t->entries.push_back(TrieEntry{uint32_t(t->text.size()), uint16_t(text->size()), uint16_t(cost)});
      t->text += *text;
      ++n.entryCount;
    }
  }
  trie_ = std::move(t);
}

}  // namespace yy
