#pragma once
// v 模式里的小工具：数字转中文、计算器、特殊符号。都不依赖词库。
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

}  // namespace yy
