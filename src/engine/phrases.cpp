#include "phrases.h"

#include <cstdio>
#include <cstdlib>

#include "util.h"

namespace yy {

CustomPhrases::CustomPhrases() { InitializeSRWLock(&lock_); }

void CustomPhrases::Init(const std::wstring& path) {
  path_ = path;
  if (!path_.empty() && !FileExists(path_)) WriteTextFile(path_, DefaultPhrasesText());
  stamp_ = 0;
  Refresh();
}

void CustomPhrases::Refresh() {
  if (path_.empty()) return;
  uint64_t stamp = FileStamp(path_);
  if (stamp == stamp_) return;
  std::string data;
  if (stamp && !ReadTextFile(path_, &data)) return;
  AcquireSRWLockExclusive(&lock_);
  stamp_ = stamp;
  Parse(data);
  ReleaseSRWLockExclusive(&lock_);
}

static std::wstring Unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[i + 1];
      if (n == 'n') { out += "\r\n"; ++i; continue; }
      if (n == 't') { out += '\t'; ++i; continue; }
      if (n == '\\') { out += '\\'; ++i; continue; }
    }
    out += s[i];
  }
  return Utf8ToWide(out);
}

void CustomPhrases::Parse(const std::string& data) {
  map_.clear();
  size_t pos = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) nl = data.size();
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    size_t t1 = line.find('\t');
    if (t1 == std::string::npos || t1 == 0) continue;
    size_t t2 = line.find('\t', t1 + 1);
    std::string code = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
    for (char& c : code) c = AsciiLower(c);
    while (!code.empty() && code.back() == ' ') code.pop_back();
    if (code.empty()) continue;
    Phrase p;
    p.text = Unescape(line.substr(0, t1));
    if (t2 != std::string::npos) p.position = std::max(1, atoi(line.c_str() + t2 + 1));
    map_[code].push_back(std::move(p));
  }
}

std::vector<Phrase> CustomPhrases::Lookup(std::string_view code) const {
  AcquireSRWLockShared(&lock_);
  std::vector<Phrase> out;
  auto it = map_.find(std::string(code));
  if (it != map_.end()) out = it->second;
  ReleaseSRWLockShared(&lock_);
  return out;
}

std::vector<Phrase> DynamicPhrases(std::string_view code) {
  std::vector<Phrase> out;
  if (code != "rq" && code != "sj" && code != "xq") return out;
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t buf[64];
  auto add = [&](const wchar_t* fmt, auto... args) {
    _snwprintf(buf, 63, fmt, args...);
    buf[63] = 0;
    out.push_back(Phrase{buf, 2});
  };
  static const wchar_t* kWeek[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
  if (code == "rq") {
    add(L"%04d-%02d-%02d", t.wYear, t.wMonth, t.wDay);
    add(L"%04d年%d月%d日", t.wYear, t.wMonth, t.wDay);
    add(L"%d月%d日", t.wMonth, t.wDay);
  } else if (code == "sj") {
    add(L"%02d:%02d", t.wHour, t.wMinute);
    add(L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    add(L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  } else {
    add(L"星期%ls", kWeek[t.wDayOfWeek % 7]);
    add(L"周%ls", kWeek[t.wDayOfWeek % 7]);
  }
  return out;
}

std::string DefaultPhrasesText() {
  return "# yy输入法 自定义短语\r\n"
         "# 每行一条：文本<Tab键>编码<Tab键>位置。位置是排第几个，可以不写（默认第 1 个）。\r\n"
         "# 文本里写 \\n 表示换行。改完保存就生效。\r\n"
         "# 例子（去掉行首的 # 就能用）：\r\n"
         "# 我的邮箱是 example@qq.com\tyx\t1\r\n"
         "# 收到，马上处理！\tsd\t2\r\n"
         "yy输入法\tyysrf\t1\r\n";
}

}  // namespace yy
