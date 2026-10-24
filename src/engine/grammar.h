#pragma once
// 语言模型：读 Rime 的 octagram 模型（rime-octagram-data 的 zh-hans-t-essay-bgw.gram），
// 给“前文 + 下一个词”打搭配分，组整句时用。格式和打分方法照 librime-octagram（BSD 协议）。
#include <string>
#include <string_view>

#include "dict.h"

namespace yy {

class GramModel {
 public:
  // 跟 octagram 的默认设置一样
  static constexpr double kNonCollocation = -12;
  static constexpr double kCollocation = -12;
  static constexpr double kWeakCollocation = -24;
  static constexpr double kRear = -18;

  bool Load(const std::wstring& path);
  bool loaded() const { return units_ != nullptr; }
  // context：前文（只看最后 3 个字）；word：下一个词；isRear：这个词是不是放在最后。
  // 返回对数分，-12 表示没有已知搭配，越大越好。
  double Query(std::u32string_view context, std::u32string_view word, bool isRear) const;

 private:
  struct Match {
    int value;
    size_t length;  // 匹配到的编码字节数
  };
  bool Traverse(const char* key, size_t* nodePos) const;
  int PrefixSearch(const char* key, size_t nodePos, Match* out, int maxOut) const;
  int Lookup(const std::string& context, const std::string& word, Match* out) const;

  MappedFile file_;
  const uint32_t* units_ = nullptr;
  size_t unitCount_ = 0;
};

// 把 UTF-16 转成码点
std::u32string ToCodePoints(std::wstring_view s);

}  // namespace yy
