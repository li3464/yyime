// 语言模型读取测试：打印几组“前文 + 词”的搭配分。用法: gram_test.exe <gram 文件> <输出文件>
#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/engine/grammar.h"
#include "../src/engine/util.h"

using namespace yy;

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  GramModel g;
  std::string out = g.Load(argv[1]) ? "加载成功\n" : "加载失败\n";
  struct Q {
    const wchar_t* ctx;
    const wchar_t* word;
    bool rear;
  } qs[] = {
      {L"看电影", L"吧", true},  {L"看电影", L"把", true},  {L"我们", L"一起", false}, {L"我们", L"以期", false},
      {L"今天", L"天气", false}, {L"今天", L"添加", false}, {L"说", L"得", false},     {L"说", L"的", false},
      {L"你好", L"吗", true},    {L"你好", L"嘛", true},    {L"中华人民", L"共和国", true},
      {L"天下", L"大义", true},  {L"天下", L"答疑", true},  {L"", L"我", false},
  };
  for (auto& q : qs) {
    double v = g.Query(ToCodePoints(q.ctx), ToCodePoints(q.word), q.rear);
    char buf[64];
    snprintf(buf, sizeof(buf), " = %.3f\n", v);
    out += WideToUtf8(std::wstring(q.ctx) + L" + " + q.word + (q.rear ? L" (句末)" : L"")) + buf;
  }
  HANDLE f = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  DWORD n;
  WriteFile(f, out.data(), DWORD(out.size()), &n, nullptr);
  CloseHandle(f);
  return 0;
}
