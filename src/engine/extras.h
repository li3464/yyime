#pragma once
// v 模式里的小工具：数字转中文、计算器、特殊符号；u 模式查生僻字（拆字、笔画）。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yy {

// "1234.5" → 一千二百三十四点五 / 壹仟贰佰叁拾肆点伍 / 壹仟贰佰叁拾肆元伍角 / 1,234.5 …；不是数字返回空
std::vector<std::wstring> NumberCandidates(std::string_view number);
// 四则运算、% 取余、^ 乘方、括号；算不出来返回 false
bool Calculate(std::string_view expr, std::wstring* result);
// v 后面跟分类码出符号，比如 jt 箭头；没有这个分类返回 nullptr
const std::vector<std::wstring>* SymbolCategory(std::string_view code);
// 只打了 v 时给的提示
std::wstring VModeHint();

// 生僻字的拆字表 / 笔画表（data\chaizi.txt、stroke.txt）：每行 编码\t字\t排名\t读音，按编码排好序
class CodeTable {
 public:
  struct Row {
    std::string_view code, text, reading;
    int rank = 0;  // 越小越常用
  };
  bool Load(const std::wstring& path);
  bool loaded() const { return !lines_.empty(); }
  // 编码以 prefix 开头的所有行
  std::vector<Row> WithPrefix(std::string_view prefix) const;

 private:
  Row RowAt(size_t i) const;
  std::string blob_;
  std::vector<uint32_t> lines_;  // 每行在 blob_ 里的开头
};

struct RareHit {
  std::wstring text, reading;
  bool exact = false;  // 编码整个对上了（不是还没打完）
};
// u 模式：body 全是 h s p n z 时先按笔画找，再按拆字找；整个对上的排前面，同一组里常用的排前面
std::vector<RareHit> FindRare(const CodeTable* chaizi, const CodeTable* stroke, std::string_view body, size_t limit);
// 只打了 u 时给的提示
std::wstring UModeHint();

}  // namespace yy
