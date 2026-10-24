#pragma once
// 自定义短语：%APPDATA%\yyime\custom_phrases.txt，每行 文本<TAB>编码<TAB>位置。
// 另外 rq/sj/xq 这几个会动态出当前日期、时间、星期。
#include <windows.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yy {

struct Phrase {
  std::wstring text;
  int position = 1;  // 排在第几个（从 1 开始）
};

class CustomPhrases {
 public:
  CustomPhrases();
  void Init(const std::wstring& path);
  void Refresh();
  std::vector<Phrase> Lookup(std::string_view code) const;

 private:
  void Parse(const std::string& data);
  std::wstring path_;
  uint64_t stamp_ = 0;
  std::unordered_map<std::string, std::vector<Phrase>> map_;
  mutable SRWLOCK lock_;
};

std::vector<Phrase> DynamicPhrases(std::string_view code);
std::string DefaultPhrasesText();

}  // namespace yy
