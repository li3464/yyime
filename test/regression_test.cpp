#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/engine/engine.h"
#include "../src/engine/hotkey.h"
#include "../src/engine/session.h"
#include "../src/engine/util.h"
#include "../src/ai/ai_client.h"
using namespace yy;
static int failures = 0, checks = 0;
static void Check(bool condition, const char* name) {
  ++checks;
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) ++failures;
}
static void Type(InputSession& session, const char* text) {
  for (; *text; ++text) {
    KeyEvent key; key.vk = unsigned(*text - 'a' + 'A'); key.ch = *text;
    session.OnKeyDown(key);
  }
}
// 字母、数字、符号都能打；返回这一串里上屏的字
static std::wstring Keys(InputSession& session, const char* text) {
  std::wstring out;
  for (; *text; ++text) {
    KeyEvent key; key.ch = wchar_t(*text);
    if (*text >= 'a' && *text <= 'z') key.vk = unsigned(*text - 'a' + 'A');
    else if (*text >= '0' && *text <= '9') key.vk = unsigned(*text);
    else if (*text == ' ') key.vk = VK_SPACE;
    else key.vk = 0xBA;
    out += session.OnKeyDown(key).commit;
  }
  return out;
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) return 2;
  std::wstring data = argv[1], user = argv[2];
  CreateDirectoryW(user.c_str(), nullptr);
  auto path = user + L"\\config-test.ini";
  Check(WriteTextFile(path, "; keep this comment\r\n[ai]\r\napi_key = fake-test-only\r\ncustom_key = retained\r\n[theme.mine]\r\nbackground = #123456\r\n"), "create isolated config");
  Check(SaveConfigValues(path, {{"ai", "translate_hotkey", "Ctrl+Shift+T"}, {"general", "sentence_model", "false"}, {"ui", "theme", "mine"}}), "atomic batch save");
  Config cfg; Check(LoadConfig(path, &cfg), "reload saved config");
  Check(cfg.aiKey == L"fake-test-only" && cfg.translateHotkey == L"Ctrl+Shift+T" && !cfg.sentenceModel, "new options and key preserved");
  std::string raw; ReadTextFile(path, &raw);
  Check(raw.find("; keep this comment") != std::string::npos && raw.find("custom_key = retained") != std::string::npos, "comments and unknown options preserved");
  Check(cfg.ResolveTheme(false).background == 0xFF123456, "custom theme preserved");
  const wchar_t* characterThemes[] = {L"deepseek",L"gpt",L"claude",L"gemini",L"qwen",L"glm",L"minimax",L"zcode",L"kimi",L"grok"};
  bool allThemes = true;
  for (const auto* name : characterThemes) {
    cfg.themeName = name;
    const auto& theme = cfg.ResolveTheme(false);
    allThemes = allThemes && theme.name == name && theme.artwork == std::wstring(name) + L".png";
  }
  Check(allThemes, "all ten character themes resolve to their own artwork");
  Check(cfg.characterArt && SaveConfigValues(path, {{"ui", "character_art", "false"}}) &&
        LoadConfig(path, &cfg) && !cfg.characterArt, "character decoration can be disabled and reloaded");
  Check(!SaveConfigValues(path, {{"ai", "model", "invalid\n[general]"}}), "reject multiline config injection");
  Check(ParseKeyChord(L"Ctrl+Alt+Space").Matches(VK_SPACE, true, true, false), "modifier shortcut");
  Check(ParseKeyChord(L" ctrl + shift + f24 ").Matches(VK_F24, true, false, true), "whitespace and F24");
  Check(!ParseKeyChord(L"Ctrl+Bogus").vk && !ParseKeyChord(L"Ctrl+F2xyz").vk && !ParseKeyChord(L"Ctrl+T+O").vk, "invalid shortcuts rejected");
  Check(!SafeShortcut(ParseKeyChord(L"Alt+F4")) && !SafeShortcut(ParseKeyChord(L"T")), "reserved and typing shortcuts rejected");

  Engine::Get().SetDirs(data, user); Engine::Get().EnsureInit();
  const auto& dicts = Engine::Get().dicts();
  Check(dicts.gram.loaded(), "grammar model loaded");
  Check(dicts.gram.Query(U"看电影", U"吧", true) > dicts.gram.Query(U"看电影", U"把", true), "context distinguishes ba");
  GramModel model;
  Check(model.Load(data + L"\\zh-hans-t-essay-bgw.gram"), "standalone model load");
  Check(!model.Load(user + L"\\missing.gram") && !model.loaded(), "failed reload clears model pointer");
  Check(ToCodePoints(L"\U0001F600你好").size() == 3, "UTF16 surrogate conversion");
  struct Case { const char* input; const wchar_t* expected; Scheme scheme = Scheme::Pinyin; } cases[] = {
    {"nihao", L"你好"}, {"zhongguo", L"中国"}, {"xi'an", L"西安"}, {"bjdx", L"北京大学"},
    {"yixiaoshi", L"一小时"}, {"shurufa", L"输入法"}, {"jintiantianqizhenhao", L"今天天气真好"},
    {"wozaixuexiaochifan", L"我在学校吃饭"}, {"womenyiqiqukandianyingba", L"我们一起去看电影吧"},
    {"nihc", L"你好", Scheme::Xiaohe}, {"vsgo", L"中国", Scheme::Xiaohe},
    {"nihk", L"你好", Scheme::Ziranma}, {"nihk", L"你好", Scheme::Mspy}
  };
  for (const auto& item : cases) {
    ConvertOptions options; options.englishMixed = false; options.scheme = item.scheme;
    Conversion output; ConvertPinyin(dicts, item.input, options, &output);
    Check(!output.cands.empty() && output.cands[0].text == item.expected, item.input);
    if (!output.cands.empty()) printf("  -> %s\n", WideToUtf8(output.cands[0].text).c_str());
  }
  ConvertOptions options; options.englishMixed = false; options.useGrammar = false;
  Conversion output; ConvertPinyin(dicts, "womenyiqiqukandianyingba", options, &output);
  Check(!output.cands.empty() && output.cands[0].text == L"我们一起去看电影把", "grammar-off baseline fallback");

  InputSession session;
  Config settings; settings.learn = false; settings.aiComposeKey = L"Ctrl+Shift+J";
  session.ApplyConfig(settings); Type(session, "nihao");
  KeyEvent chord{'J', 0, true, true, false};
  Check(session.OnKeyDown(chord).aiRequested, "configured compose chord triggers AI");
  KeyEvent tab{VK_TAB}; Check(!session.OnKeyDown(tab).aiRequested, "old Tab binding stops requesting AI");
  settings.aiEnabled = false; session.ApplyConfig(settings);
  Check(!session.OnKeyDown(chord).aiRequested, "AI disabled respects shortcut");
  int id = session.BeginAiPanel(AiMode::Translate);
  Check(session.AuxText() == L"AI 翻译", "translation panel title");
  session.SetAiResult(id, {L"Hello"}, L"");
  Check(session.ClickCandidate(0).commit == L"Hello", "translation candidate commit");
  int old = session.BeginAiPanel(AiMode::Translate); session.Clear();
  session.SetAiResult(old, {L"stale"}, L"");
  Check(!session.panelVisible() && session.PageView().items.empty(), "cancel invalidates asynchronous result");
  AiRequest req; req.mode = AiMode::Translate; req.targetLanguage = L"English"; req.selection = L"你好世界";
  req.model = L"test-model"; req.apiKey = L"fake-test-only";
  auto body = BuildAiBody(req);
  Check(body.find("English") != std::string::npos && body.find("你好世界") != std::string::npos && body.find("candidates") != std::string::npos, "translation prompt and JSON contract");
  AiResponse response;
  Check(ParseAiReply(R"({"choices":[{"message":{"content":"{\"candidates\":[\"Hello, world!\"]}"}}]})", &response) && response.texts.size() == 1 && response.texts[0] == L"Hello, world!", "parse translated API response");
  if (argc > 3) {
    req.baseUrl = argv[3]; req.timeoutMs = 3000;
    response = RunAiRequestBlocking(req);
    if (!response.error.empty()) printf("  transport error: %s\n", WideToUtf8(response.error).c_str());
    Check(response.error.empty() && response.texts.size() == 1 && response.texts[0] == L"Hello, world!", "WinHTTP translation with local mock server");
  }
  {
    InputSession extra; Config ec; ec.learn = false; extra.ApplyConfig(ec);
    Check(Keys(extra, "xianzai[") == L"现" && Keys(extra, "xianzai]") == L"在", "pick first / last char of word");
    Check(Keys(extra, "v123 ") == L"一百二十三", "v-mode lowercase number");
    Keys(extra, "v1234.5");
    bool money = false;
    for (auto* c : extra.PageView().items) money = money || c->text == L"壹仟贰佰叁拾肆元伍角";
    Check(money, "v-mode money in capitals"); extra.Clear();
    Check(Keys(extra, "v(1+2)*3 ") == L"9" && Keys(extra, "v2^10 ") == L"1024", "v-mode calculator");
    Keys(extra, "v1+");
    Check(extra.composing() && extra.PageView().items.empty() && !extra.PageView().status.empty(), "v-mode incomplete formula hint");
    extra.Clear();
    Check(Keys(extra, "vjt2") == L"→", "v-mode symbol picked with digit");
    Check(Keys(extra, "very ") == L"very", "v-word still types English");
    Keys(extra, "kaixin");
    auto items = extra.PageView().items;
    Check(items.size() > 1 && items[0]->text == L"开心" && items[1]->text == L"😄", "emoji follows word");
    extra.Clear();
    ec.emoji = false; extra.ApplyConfig(ec); Keys(extra, "kaixin");
    items = extra.PageView().items;
    Check(items.size() > 1 && items[1]->text != L"😄", "emoji switch off");
    extra.Clear();
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
