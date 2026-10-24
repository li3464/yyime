#pragma once
// 把输入的键转换成候选：查词、组整句、排序、插英文单词。
#include <string>
#include <string_view>
#include <vector>

#include "dict.h"
#include "pinyin.h"
#include "userdict.h"

namespace yy {

enum class CandKind : uint8_t { Word, Sentence, English, Custom, Ai, Wubi, Raw };

struct Candidate {
  std::wstring text;
  std::wstring comment;
  int consumed = 0;  // 用掉了多少个输入键
  int cost = 0;
  CandKind kind = CandKind::Word;
  std::vector<uint16_t> syls;  // 拼音词的音节，学习用；不是拼音词就空着
};

struct Dictionaries {
  PinyinDict pinyin;
  WubiDict wubi;
  EnglishDict english;
  SyllableTable table;
  ShuangpinScheme xiaohe, ziranma, mspy;
  const ShuangpinScheme* Shuangpin(Scheme s) const;
};

struct Conversion {
  std::vector<Candidate> cands;
  std::string display;  // 预编辑区显示的样子，比如 ni'hao
  bool clean = false;   // 整串都切成了完整音节（只允许最后一个是简拼）
};

struct ConvertOptions {
  Scheme scheme = Scheme::Pinyin;
  const FuzzyRules* fuzzy = nullptr;
  bool englishMixed = true;
  const UserTrie* user = nullptr;
};

void ConvertPinyin(const Dictionaries& d, std::string_view input, const ConvertOptions& opt, Conversion* out);
void ConvertWubi(const Dictionaries& d, std::string_view input, Conversion* out);
// 把英文单词插进候选（拼音切得干净时放第 2 个，不然放第 1 个）
void AddEnglish(const Dictionaries& d, std::string_view input, bool clean, Conversion* out);

}  // namespace yy
