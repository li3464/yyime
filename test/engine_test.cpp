// 引擎测试：不经过 TSF，直接喂按键/输入串，把候选和上屏结果写进 UTF-8 文件。
// 用法: engine_test.exe <词库目录> <用户目录> <输出文件> [方案]
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/engine/engine.h"
#include "../src/engine/session.h"
#include "../src/engine/util.h"

using namespace yy;

static std::string g_out;

static void Out(const std::wstring& s) { g_out += WideToUtf8(s); }
static void Out(const std::string& s) { g_out += s; }

static std::wstring CandList(const std::vector<Candidate>& cands, size_t limit) {
  std::wstring s;
  for (size_t i = 0; i < cands.size() && i < limit; ++i) {
    const Candidate& c = cands[i];
    wchar_t buf[64];
    _snwprintf(buf, 63, L"%zu.", i + 1);
    s += buf;
    s += c.text;
    const wchar_t* tag = L"";
    switch (c.kind) {
      case CandKind::Sentence: tag = L"[句]"; break;
      case CandKind::English: tag = L"[英]"; break;
      case CandKind::Custom: tag = L"[短]"; break;
      case CandKind::Raw: tag = L"[原]"; break;
      default: break;
    }
    s += tag;
    if (!c.comment.empty()) s += L"(" + c.comment + L")";
    _snwprintf(buf, 63, L"<%d,%d> ", c.consumed, c.cost);
    s += buf;
  }
  return s;
}

static void Convert(const std::string& input, Scheme scheme) {
  Engine& e = Engine::Get();
  Conversion conv;
  auto user = e.user().Snapshot();
  FuzzyRules fz;
  ConvertOptions opt;
  opt.scheme = scheme;
  opt.fuzzy = &fz;
  opt.user = user.get();
  auto t0 = std::chrono::steady_clock::now();
  if (scheme == Scheme::Wubi86) ConvertWubi(e.dicts(), input, &conv);
  else ConvertPinyin(e.dicts(), input, opt, &conv);
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  char head[160];
  snprintf(head, sizeof(head), "[%s] %s  显示=%s 干净=%d 候选=%zu 用时=%lldus\n    ",
           WideToUtf8(SchemeName(scheme)).c_str(), input.c_str(), conv.display.c_str(), int(conv.clean),
           conv.cands.size(), (long long)us);
  Out(head);
  Out(CandList(conv.cands, 10));
  Out("\n");
}

// 脚本：普通字符照打；{sp}空格 {cr}回车 {bs}退格 {esc} {tab} {shift} {pgdn} {down}
static void Type(InputSession& s, const std::string& script) {
  std::wstring committed;
  for (size_t i = 0; i < script.size(); ++i) {
    KeyEvent k;
    SessionResult r;
    if (script[i] == '{') {
      size_t close = script.find('}', i);
      std::string name = script.substr(i + 1, close - i - 1);
      i = close;
      if (name == "shift") {
        r = s.OnShiftTap();
        committed += r.commit;
        continue;
      }
      k.vk = name == "sp" ? VK_SPACE : name == "cr" ? VK_RETURN : name == "bs" ? VK_BACK : name == "esc" ? VK_ESCAPE
           : name == "tab" ? VK_TAB : name == "pgdn" ? VK_NEXT : name == "down" ? VK_DOWN : 0;
      if (k.vk == VK_SPACE) k.ch = L' ';
    } else {
      char c = script[i];
      k.ch = wchar_t((unsigned char)c);
      if (c >= 'a' && c <= 'z') k.vk = unsigned(c - 'a' + 'A');
      else if (c >= 'A' && c <= 'Z') { k.vk = unsigned(c); k.shift = true; }
      else if (c >= '0' && c <= '9') k.vk = unsigned(c);
      else k.vk = 0xBA;  // 随便一个符号键
    }
    bool eat = s.TestKeyDown(k);
    if (eat) r = s.OnKeyDown(k);
    if (!eat) committed += L"«" + std::wstring(1, k.ch) + L"»";  // 没吃掉的键会直接进程序
    committed += r.commit;
  }
  Out("  " + script + "  →  上屏「");
  Out(committed);
  Out("」");
  if (s.composing()) {
    Out("  还在打：");
    Out(s.Preedit());
    auto v = s.PageView();
    Out("  候选：");
    for (auto* c : v.items) Out(c->text + L" ");
  }
  Out("\n");
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 4) return 2;
  Engine::Get().SetDirs(argv[1], argv[2]);
  auto t0 = std::chrono::steady_clock::now();
  Engine::Get().EnsureInit();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  Out("初始化用时 " + std::to_string(ms) + "ms，音节数 " + std::to_string(Engine::Get().dicts().table.size()) + "\n");

  const char* pinyin[] = {"nihao", "zhongguo", "woaini", "xian", "xi'an", "fangan", "zg", "bjdx", "nh",
                          "jintiantianqizhenhao", "wozaixuexiaochifan", "zhonghuarenmingongheguo", "shangh",
                          "zhon", "hello", "wifi", "bus", "change", "lvse", "lueguo", "nihaoiu",
                          "yixiaoshi", "shurufa", "womenyiqiqukandianyingba", "tianxiadayi"};
  for (const char* p : pinyin) Convert(p, Scheme::Pinyin);
  Out("--- 双拼\n");
  Convert("nihc", Scheme::Xiaohe);       // 小鹤：你好
  Convert("vsgo", Scheme::Xiaohe);       // 小鹤：中国
  Convert("uurufa", Scheme::Xiaohe);     // 小鹤：输入法
  Convert("nihk", Scheme::Ziranma);      // 自然码：你好
  Convert("vsgo", Scheme::Ziranma);      // 自然码：中国
  Convert("nihk", Scheme::Mspy);         // 微软：你好
  Convert("m;", Scheme::Mspy);           // 微软：ming
  Convert("ahbz", Scheme::Xiaohe);       // 小鹤零声母：ang bei? 测试切分
  Out("--- 五笔\n");
  for (const char* w : {"wq", "trnt", "aa", "g", "ggtt", "tr"}) Convert(w, Scheme::Wubi86);

  Out("--- 模拟打字（全拼）\n");
  InputSession s;
  Config cfg;
  s.ApplyConfig(cfg);
  for (const char* sc : {"nihao{sp}", "nihao,", "zhongguoren{sp}", "nihao{cr}", "nihao1", "nihao2",
                         "xianzai{sp}", "nihaoma?", "wo{sp}ai{sp}ni{sp}", "zg{sp}", "3.14", "nihao{bs}{bs}{sp}",
                         "nihao{esc}abc{esc}", "\"nihao{sp}\"", "rq{sp}", "rq2", "jintiantianqi{sp}",
                         "nihao{shift}nihao{shift}nihao{sp}", "hello{sp}", "wo3", "yysrf{sp}"}) {
    Type(s, sc);
    s.Clear();
    s.SetChinese(true);
  }
  Out("--- 分段选词（xianshi 先选 西，再选 安市）\n");
  Type(s, "xianshi");
  s.Clear();

  Out("--- 五笔打字\n");
  Config wb;
  wb.scheme = Scheme::Wubi86;
  InputSession w;
  w.ApplyConfig(wb);
  for (const char* sc : {"wq{sp}", "trnt", "trntwq{sp}", "ggtt{sp}", "zni", "aa{sp}"}) {
    Type(w, sc);
    w.Clear();
  }

  Out("--- 学习：连选三次 线（xian 第4个）后排第几\n");
  for (int i = 0; i < 3; ++i) Type(s, "xian4");
  Convert("xian", Scheme::Pinyin);

  HANDLE f = CreateFileW(argv[3], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  DWORD n;
  WriteFile(f, g_out.data(), DWORD(g_out.size()), &n, nullptr);
  CloseHandle(f);
  return 0;
}
