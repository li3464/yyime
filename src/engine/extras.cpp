#include "extras.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace yy {

namespace {

// 整数部分转中文。digits 不带前导零，最多 16 位
std::wstring IntegerToChinese(const std::string& digits, bool upper) {
  static const wchar_t* lowerDigits = L"零一二三四五六七八九";
  static const wchar_t* upperDigits = L"零壹贰叁肆伍陆柒捌玖";
  static const wchar_t* lowerUnits[] = {L"", L"十", L"百", L"千"};
  static const wchar_t* upperUnits[] = {L"", L"拾", L"佰", L"仟"};
  static const wchar_t* bigUnits[] = {L"", L"万", L"亿", L"万亿"};
  const wchar_t* d = upper ? upperDigits : lowerDigits;
  const wchar_t** units = upper ? upperUnits : lowerUnits;
  if (digits == "0") return std::wstring(1, d[0]);
  std::wstring out;
  int n = int(digits.size());
  int groups = (n + 3) / 4;
  bool pendingZero = false;
  for (int g = 0; g < groups; ++g) {
    int end = n - (groups - 1 - g) * 4;
    int begin = std::max(0, end - 4);
    std::string group(size_t(4 - (end - begin)), '0');
    group += digits.substr(size_t(begin), size_t(end - begin));
    if (group == "0000") {
      pendingZero = !out.empty();
      continue;
    }
    if (!out.empty() && (pendingZero || group[0] == '0')) out += d[0];
    pendingZero = false;
    bool zeroInside = false;
    bool started = false;
    for (int i = 0; i < 4; ++i) {
      int v = group[size_t(i)] - '0';
      if (v == 0) {
        zeroInside = started;
        continue;
      }
      if (zeroInside) out += d[0];
      zeroInside = false;
      started = true;
      out += d[v];
      out += units[3 - i];
    }
    out += bigUnits[groups - 1 - g];
    if (group[3] == '0') pendingZero = true;
  }
  if (!upper && out.rfind(L"一十", 0) == 0) out.erase(0, 1);  // 一十二 → 十二
  return out;
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ---- 计算器：递归下降
struct Parser {
  std::string_view s;
  size_t i = 0;
  bool ok = true;
  void Skip() {
    while (i < s.size() && s[i] == ' ') ++i;
  }
  double Expr() {
    double v = Term();
    while (ok) {
      Skip();
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        char op = s[i++];
        double r = Term();
        v = op == '+' ? v + r : v - r;
      } else {
        break;
      }
    }
    return v;
  }
  double Term() {
    double v = Power();
    while (ok) {
      Skip();
      if (i < s.size() && (s[i] == '*' || s[i] == '/' || s[i] == '%')) {
        char op = s[i++];
        double r = Power();
        if ((op == '/' || op == '%') && r == 0) {
          ok = false;
          return 0;
        }
        v = op == '*' ? v * r : op == '/' ? v / r : std::fmod(v, r);
      } else {
        break;
      }
    }
    return v;
  }
  double Power() {
    double base = Unary();
    Skip();
    if (ok && i < s.size() && s[i] == '^') {
      ++i;
      return std::pow(base, Power());
    }
    return base;
  }
  double Unary() {
    Skip();
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
      char op = s[i++];
      double v = Unary();
      return op == '-' ? -v : v;
    }
    return Primary();
  }
  double Primary() {
    Skip();
    if (i < s.size() && s[i] == '(') {
      ++i;
      double v = Expr();
      Skip();
      if (i >= s.size() || s[i] != ')') {
        ok = false;
        return 0;
      }
      ++i;
      return v;
    }
    size_t start = i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) ++i;
    if (start == i) {
      ok = false;
      return 0;
    }
    return std::atof(std::string(s.substr(start, i - start)).c_str());
  }
};

std::map<std::string, std::vector<std::wstring>> BuildSymbols() {
  // 分类码 → 符号，| 分隔（颜文字里有空格）
  const std::pair<const char*, const wchar_t*> table[] = {
      {"fh", L"★|☆|○|●|◎|◇|◆|□|■|△|▲|※|√|×|♥|♡|♪|♫|☀|☁|☂|☺|✓|✔|✗|✘"},
      {"jt", L"←|→|↑|↓|↖|↗|↘|↙|↔|↕|⇐|⇒|⇑|⇓|⇔|⇄|➜|➤|↩|↪"},
      {"sx", L"±|×|÷|≈|≠|≤|≥|∞|√|∑|∏|∫|∂|∈|∉|⊂|⊃|⊆|⊇|∪|∩|∠|⊥|∥|∵|∴|∝|‰|°|π"},
      {"dw", L"℃|℉|㎡|㎥|㎏|㎎|㎜|㎝|㎞|㎖|㎘|㏄|％|‰|￥|＄|€|£|¥"},
      {"xl", L"α|β|γ|δ|ε|ζ|η|θ|ι|κ|λ|μ|ν|ξ|ο|π|ρ|σ|τ|υ|φ|χ|ψ|ω|Α|Β|Γ|Δ|Θ|Λ|Π|Σ|Φ|Ψ|Ω"},
      {"xh", L"①|②|③|④|⑤|⑥|⑦|⑧|⑨|⑩|⑪|⑫|⑬|⑭|⑮|⑯|⑰|⑱|⑲|⑳|⑴|⑵|⑶|⑷|⑸|⒈|⒉|⒊|ⅰ|ⅱ|ⅲ|Ⅰ|Ⅱ|Ⅲ|Ⅳ|Ⅴ|Ⅵ|Ⅶ|Ⅷ|Ⅸ|Ⅹ|㈠|㈡|㈢"},
      {"bd", L"、|。|·|…|—|～|《|》|〈|〉|「|」|『|』|【|】|〔|〕|‘|’|“|”"},
      {"jh", L"□|■|△|▲|▽|▼|○|●|◇|◆|☆|★|◎|◐|◑|⊙|◁|▷"},
      {"bq", L"(^_^)|(≧▽≦)|(｡･ω･｡)|╮(╯▽╰)╭|ヾ(•ω•`)o|(*^▽^*)|(｀・ω・´)|(T_T)|(；′⌒`)|(╯°□°）╯︵ ┻━┻|┑(￣Д ￣)┍|orz"},
      {"py", L"ā|á|ǎ|à|ō|ó|ǒ|ò|ē|é|ě|è|ī|í|ǐ|ì|ū|ú|ǔ|ù|ǖ|ǘ|ǚ|ǜ|ü"},
  };
  std::map<std::string, std::vector<std::wstring>> out;
  for (const auto& [code, list] : table) {
    std::wstring s = list;
    auto& v = out[code];
    size_t pos = 0;
    while (pos <= s.size()) {
      size_t e = s.find(L'|', pos);
      if (e == std::wstring::npos) e = s.size();
      if (e > pos) v.push_back(s.substr(pos, e - pos));
      pos = e + 1;
    }
  }
  return out;
}

}  // namespace

std::vector<std::wstring> NumberCandidates(std::string_view number) {
  std::vector<std::wstring> out;
  size_t dot = number.find('.');
  std::string intPart(number.substr(0, dot)), frac(dot == std::string_view::npos ? "" : number.substr(dot + 1));
  if (frac.find('.') != std::string::npos || (intPart.empty() && frac.empty())) return out;
  for (char c : intPart + frac)
    if (c < '0' || c > '9') return out;
  while (intPart.size() > 1 && intPart[0] == '0') intPart.erase(0, 1);
  if (intPart.empty()) intPart = "0";
  if (intPart.size() > 16) return out;
  static const wchar_t* lowerDigits = L"零一二三四五六七八九";
  static const wchar_t* upperDigits = L"零壹贰叁肆伍陆柒捌玖";
  std::wstring lowerFrac, upperFrac;
  for (char c : frac) {
    lowerFrac += lowerDigits[c - '0'];
    upperFrac += upperDigits[c - '0'];
  }
  std::wstring lower = IntegerToChinese(intPart, false), upper = IntegerToChinese(intPart, true);
  out.push_back(frac.empty() ? lower : lower + L"点" + lowerFrac);
  out.push_back(frac.empty() ? upper : upper + L"点" + upperFrac);
  // 金额：元角分
  if (frac.size() <= 2) {
    int jiao = frac.size() >= 1 ? frac[0] - '0' : 0, fen = frac.size() >= 2 ? frac[1] - '0' : 0;
    std::wstring money;
    if (intPart != "0") money = upper + L"元";
    if (jiao == 0 && fen == 0) {
      money = (intPart == "0" ? L"零元" : money) + L"整";
    } else {
      if (jiao) money += std::wstring(1, upperDigits[jiao]) + L"角";
      else if (intPart != "0") money += L"零";
      if (fen) money += std::wstring(1, upperDigits[fen]) + L"分";
    }
    out.push_back(money);
  }
  // 逐位读：一二三
  if (intPart.size() > 1 || !frac.empty()) {
    std::wstring each;
    for (char c : intPart) each += lowerDigits[c - '0'];
    out.push_back(frac.empty() ? each : each + L"点" + lowerFrac);
  }
  // 千分位
  if (intPart.size() > 3) {
    std::string grouped;
    for (size_t i = 0; i < intPart.size(); ++i) {
      if (i && (intPart.size() - i) % 3 == 0) grouped += ',';
      grouped += intPart[i];
    }
    out.push_back(Widen(grouped + (frac.empty() ? "" : "." + frac)));
  }
  return out;
}

bool Calculate(std::string_view expr, std::wstring* result) {
  Parser p{expr};
  double v = p.Expr();
  p.Skip();
  if (!p.ok || p.i != expr.size() || !std::isfinite(v)) return false;
  char buf[64];
  if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) snprintf(buf, sizeof(buf), "%.0f", std::round(v));
  else snprintf(buf, sizeof(buf), "%.10g", v);
  *result = Widen(buf);
  if (*result == L"-0") *result = L"0";
  return true;
}

const std::vector<std::wstring>* SymbolCategory(std::string_view code) {
  static const auto symbols = BuildSymbols();
  auto it = symbols.find(std::string(code));
  return it == symbols.end() ? nullptr : &it->second;
}

std::wstring VModeHint() {
  return L"数字 v123 · 计算 v1+2 · 符号 fh jt sx dw xl xh bd jh bq py";
}

}  // namespace yy
