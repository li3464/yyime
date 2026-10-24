// yy输入法 设置窗口。
// 整个界面跟着所选的 AI 娘换主题色。开关、分段按钮、标签、键帽、皮肤卡片都是自绘按钮，
// 文字输入是放在自绘圆角框里的无边框 EDIT。坐标都按 96 DPI 写，摆放和绘制时再按实际 DPI 缩放。
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <imm.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "../engine/config.h"
#include "../engine/hotkey.h"
#include "../engine/util.h"
#include "artwork.h"

using namespace yy;
namespace {

// ---------------------------------------------------------------- 尺寸、配色

constexpr int kW = 1040, kH = 700;       // 客户区
constexpr int kSide = 224;                // 侧栏宽
constexpr int kX = 256, kRight = 1008;    // 内容区左右
constexpr int kTop = 104, kBottom = 632;  // 内容区上下，下面是底栏
constexpr int kPages = 4;

constexpr uint32_t kWhite = 0xFFFFFFFF, kBg = 0xFFF5F6FA, kLine = 0xFFE8EAF0, kText = 0xFF1F2433,
                   kSub = 0xFF7A8194, kMuted = 0xFFA6ABBB, kField = 0xFFF6F7FA, kFieldLine = 0xFFE0E3EB,
                   kTrack = 0xFFD5D9E2, kSegBg = 0xFFEEF0F5, kDanger = 0xFFD9485F, kOk = 0xFF1F9D6B;
constexpr uint32_t kAccentText = 1;  // 文字颜色用这个值时，画的时候换成主题色

uint32_t Mix(uint32_t a, uint32_t b, double t) {
  auto ch = [&](int s) { return uint32_t(std::lround(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t)) << s; };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}
double ContrastWithWhite(uint32_t c) {
  auto lin = [](unsigned v) { double x = v / 255.0; return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4); };
  double l = .2126 * lin((c >> 16) & 255) + .7152 * lin((c >> 8) & 255) + .0722 * lin(c & 255);
  return 1.05 / (l + .05);
}
uint32_t Darken(uint32_t c, double contrast) {
  for (int i = 0; i < 40 && ContrastWithWhite(c) < contrast; ++i) c = Mix(c, 0xFF000000, 0.06);
  return c;
}

struct Palette { uint32_t accent, soft, text, side; } pal{0xFF4F6BED, 0xFFEDF0FD, 0xFF3F57C9, 0xFFF8F9FE};

// ---------------------------------------------------------------- 选项数据

const wchar_t* kSchemeIds[] = {L"pinyin", L"xiaohe", L"ziranma", L"mspy", L"wubi86"};
const wchar_t* kLangIds[] = {L"auto", L"English", L"简体中文", L"日本語", L"한국어", L"Français", L"Deutsch"};
const wchar_t* kLangLabels[] = {L"自动：中文译英语，其他译中文", L"英语", L"简体中文", L"日语", L"韩语", L"法语", L"德语"};
const wchar_t* kCharacters[] = {L"deepseek", L"gpt",     L"claude", L"gemini", L"qwen",
                                L"glm",      L"minimax", L"zcode",  L"kimi",   L"grok"};
const wchar_t* kGreetings[] = {L"深海里也能捞出好句子～", L"想说什么，我陪你斟酌。", L"今天想写点什么呢？",
                               L"双子星一起，灵感加倍！", L"千问不倦，字字有回应。", L"安静一点，话也说得清。",
                               L"灵感像海螺，贴近就听见。", L"蓝图画好了，开工吧。",   L"夜深了，月光陪你打字。",
                               L"嘿嘿，今天想闹点什么？"};
const wchar_t* kFonts[] = {L"Microsoft YaHei UI", L"微软雅黑", L"等线", L"HarmonyOS Sans SC", L"MiSans",
                           L"思源黑体", L"Source Han Sans SC", L"Noto Sans SC", L"霞鹜文楷", L"LXGW WenKai",
                           L"楷体", L"宋体", L"仿宋"};
struct FuzzyPair { const wchar_t* a; const wchar_t* b; int width; };
const FuzzyPair kFuzzy[] = {{L"z", L"zh", 66},    {L"c", L"ch", 66},    {L"s", L"sh", 66},      {L"n", L"l", 58},
                            {L"f", L"h", 58},     {L"r", L"l", 58},     {L"an", L"ang", 84},    {L"en", L"eng", 84},
                            {L"in", L"ing", 80},  {L"ian", L"iang", 98}, {L"uan", L"uang", 102}};
constexpr int kFuzzyCount = int(sizeof(kFuzzy) / sizeof(kFuzzy[0]));

enum Sw { SwSentence, SwLearn, SwEnglish, SwPunct, SwShift, SwStartCn, SwDynamic, SwWubi, SwPreedit, SwArt, SwAi, SwCount };
struct SwitchRow { const wchar_t* title; const wchar_t* desc; };
const SwitchRow kHabitRows[] = {
    {L"本地整句优化", L"用语言模型挑更通顺的整句，不联网"}, {L"记住我选过的词", L"常用的词会自动往前排"},
    {L"中英混打", L"直接打英文单词也有候选"},              {L"中文标点", L"中文状态下打出 ，。？！"},
    {L"单按 Shift 切换中英", L"打到一半按，字母原样上屏"},  {L"默认中文模式", L"切到 yy 输入法时先用中文"},
    {L"日期和时间", L"rq 日期 · sj 时间 · xq 星期"},       {L"五笔四码自动上屏", L"只剩一个候选时直接上屏"}};

struct KeyRow { const wchar_t* icon; const wchar_t* title; const wchar_t* desc; bool compose; };
const KeyRow kKeyRows[] = {{L"", L"AI 整句", L"打拼音时按，AI 帮你组句", true},
                           {L"", L"润色 / 续写", L"选中就润色，没选中就续写", false},
                           {L"", L"翻译", L"选中文字后按，挑译文替换", false},
                           {L"", L"打开设置", L"随时调出这个窗口", false},
                           {L"", L"中英文标点", L"在 ，。和 ,. 之间切换", false}};
constexpr int kKeyCount = 5;
const wchar_t* kFixedKeys[][2] = {{L"空格", L"选第一个候选"}, {L"1–9", L"选对应的候选"},   {L"- =", L"上一页 / 下一页"},
                                  {L"↑ ↓", L"移动高亮"},     {L"回车", L"字母原样上屏"},   {L"Esc", L"取消这次输入"},
                                  {L"Shift", L"切换中 / 英"}, {L"Win 空格", L"切换输入法"}};

enum : int {
  IdNav = 100, IdSave = 120, IdClose, IdPhrases, IdFolder, IdReset, IdReveal, IdGetKey, IdLanguage,
  IdFont, IdFontSize, IdLayout, IdScheme, IdPageSize, IdGoSkins,
  IdSwitch = 200, IdChip = 300, IdCard = 400, IdSwatch = 450, IdKey = 500, IdKeyClear = 520,
  IdEdKey = 600, IdEdUrl, IdEdModel, IdEdProxy, IdEdTimeout,
};

// ---------------------------------------------------------------- 状态

HWND mainWnd, tooltip;
HFONT inputFont;
UINT dpi = 96;
std::wstring configPath;
Config config;
std::map<std::wstring, Artwork> artworkCache;
bool initializing = true, dirty = false, shotMode = false;
int pageIndex = 0, hoverId = 0, recording = -1;

int scheme = 0, pageSize = 7, fontSize = 17, language = 0;
bool vertical = false, sw[SwCount] = {}, fuzzyOn[kFuzzyCount] = {};
std::wstring fontFace, themeName, fuzzyOther, keyValues[kKeyCount];
std::wstring status = L"改动后点「保存」生效";
uint32_t statusColor = kMuted;

int S(int v) { return MulDiv(v, int(dpi), 96); }
float F(int v) { return float(v); }

// ---------------------------------------------------------------- 主题

const Theme* FindTheme(const std::wstring& name) {
  for (const auto& t : config.themes)
    if (t.name == name) return &t;
  return nullptr;
}
bool SystemDark() {
  DWORD light = 1, bytes = sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
  return light == 0;
}
const Theme& CurrentTheme() {
  if (themeName != L"auto")
    if (auto* t = FindTheme(themeName)) return *t;
  auto* t = FindTheme(SystemDark() ? L"dark" : L"light");
  return t ? *t : config.themes.front();
}
void UpdatePalette() {
  uint32_t a = Darken(CurrentTheme().highlightBg, 3.0);
  pal = {a, Mix(kWhite, a, 0.11), Darken(a, 4.6), Mix(kWhite, a, 0.045)};
}
std::wstring Brand(const Theme& t) {
  auto split = t.label.find(L" · ");
  return split == std::wstring::npos ? t.label : t.label.substr(0, split);
}
std::wstring Nickname(const Theme& t) {
  auto split = t.label.find(L" · ");
  return split == std::wstring::npos ? L"经典配色" : t.label.substr(split + 3);
}
int CharacterIndex(const Theme& t) {
  for (int i = 0; i < 10; ++i)
    if (t.name == kCharacters[i]) return i;
  return -1;
}
Artwork* ArtFor(const Theme& t) {
  if (t.artwork.empty()) return nullptr;
  auto& art = artworkCache[t.name];
  if (art.pixels.empty() && art.width == 0) {
    if (!art.Load(ThemeAssetPath(t), 512)) art.width = 1;  // 记下“加载过但失败”，不再反复读
  }
  return art.pixels.empty() ? nullptr : &art;
}

// ---------------------------------------------------------------- 画图工具

using Gdiplus::Graphics;
using Gdiplus::RectF;
Gdiplus::Color C(uint32_t c) { return Gdiplus::Color(c == kAccentText ? pal.text : c); }
const wchar_t* iconFont = L"Segoe Fluent Icons";

void Setup(Graphics& g) {
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  g.ScaleTransform(float(dpi) / 96, float(dpi) / 96);
}
void RoundPath(Gdiplus::GraphicsPath& p, RectF r, float rad) {
  float d = std::min({rad * 2, r.Width, r.Height});
  if (d <= 0.5f) { p.AddRectangle(r); return; }
  p.AddArc(r.X, r.Y, d, d, 180, 90);
  p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  p.CloseFigure();
}
void Round(Graphics& g, RectF r, float rad, uint32_t fill, uint32_t line = 0, float stroke = 1) {
  Gdiplus::GraphicsPath p;
  RoundPath(p, r, rad);
  if (fill) { Gdiplus::SolidBrush b(C(fill)); g.FillPath(&b, &p); }
  if (line) { Gdiplus::Pen pen(C(line), stroke); g.DrawPath(&pen, &p); }
}
void Shadow(Graphics& g, RectF r, float rad) {
  for (int i = 3; i >= 1; --i) {
    float k = float(i) * 2.2f;
    RectF s(r.X - k * 0.4f, r.Y - k * 0.2f + 2, r.Width + k * 0.8f, r.Height + k * 0.8f);
    Round(g, s, rad + k * 0.5f, (uint32_t(4 + (3 - i) * 3) << 24) | 0x1A2040);
  }
}
enum Align { Near, Center, Far };
void Text(Graphics& g, const std::wstring& s, RectF r, float size, uint32_t color, bool bold = false,
          Align align = Near, bool wrap = false, const wchar_t* face = L"Microsoft YaHei UI") {
  Gdiplus::Font font(face, size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush brush(C(color));
  Gdiplus::StringFormat f;
  f.SetAlignment(align == Near ? Gdiplus::StringAlignmentNear
                 : align == Center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentFar);
  f.SetLineAlignment(wrap ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
  f.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  if (!wrap) f.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  g.DrawString(s.c_str(), -1, &font, r, &f, &brush);
}
void Glyph(Graphics& g, const wchar_t* code, RectF r, float size, uint32_t color) {
  Text(g, code, r, size, color, false, Center, false, iconFont);
}
float Measure(Graphics& g, const std::wstring& s, float size, bool bold = false) {
  Gdiplus::Font font(L"Microsoft YaHei UI", size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                     Gdiplus::UnitPixel);
  RectF out;
  g.MeasureString(s.c_str(), -1, &font, Gdiplus::PointF(0, 0), &out);
  return out.Width;
}
// 角色图：按宽度铺满 r，只取图片上面 fraction 那一段（大头和上半身）
void ArtCover(Graphics& g, const Artwork& art, RectF r, float fraction) {
  Gdiplus::Bitmap bmp(INT(art.width), INT(art.height), INT(art.width * 4), PixelFormat32bppPARGB,
                      const_cast<BYTE*>(art.pixels.data()));
  float srcH = art.height * fraction;
  float scale = std::max(r.Width / art.width, r.Height / srcH);
  float drawW = art.width * scale, drawH = srcH * scale;
  g.DrawImage(&bmp, RectF(r.X + (r.Width - drawW) / 2, r.Y, drawW, drawH), 0, 0, float(art.width), srcH,
              Gdiplus::UnitPixel);
}
void ArtFit(Graphics& g, const Artwork& art, RectF r) {
  Gdiplus::Bitmap bmp(INT(art.width), INT(art.height), INT(art.width * 4), PixelFormat32bppPARGB,
                      const_cast<BYTE*>(art.pixels.data()));
  float scale = std::min(r.Width / art.width, r.Height / art.height);
  float w = art.width * scale, h = art.height * scale;
  g.DrawImage(&bmp, RectF(r.X + (r.Width - w) / 2, r.Y + r.Height - h, w, h), 0, 0, float(art.width),
              float(art.height), Gdiplus::UnitPixel);
}
void Keycaps(Graphics& g, const std::vector<std::wstring>& keys, float x, float cy, float size = 12) {
  for (const auto& k : keys) {
    float w = std::max(26.0f, Measure(g, k, size, true) + 14);
    RectF cap(x, cy - 13, w, 26);
    Round(g, RectF(cap.X, cap.Y + 1.5f, cap.Width, cap.Height), 6, 0xFFDCDFE7);
    Round(g, cap, 6, 0xFFF7F8FB, 0xFFD9DCE5);
    Text(g, k, cap, size, kText, true, Center);
    x += w + 5;
  }
}
std::vector<std::wstring> KeyParts(const std::wstring& spec) {
  std::vector<std::wstring> out;
  size_t pos = 0;
  while (pos <= spec.size() && !spec.empty()) {
    size_t end = spec.find(L'+', pos);
    if (end == pos && end < spec.size()) end = spec.find(L'+', pos + 1);  // “Ctrl++” 这种
    if (end == std::wstring::npos) end = spec.size();
    std::wstring part = spec.substr(pos, end - pos);
    if (part == L"Space") part = L"空格";
    else if (part == L"Enter") part = L"回车";
    else if (part == L"PageUp") part = L"PgUp";
    else if (part == L"PageDown") part = L"PgDn";
    if (!part.empty()) out.push_back(part);
    pos = end + 1;
  }
  return out;
}

// ---------------------------------------------------------------- 页面上的静态元素

enum class V { Card, Text, Wrap, Field, Icon, Divider, Callout, Preview, Fixed };
struct Visual {
  int page;
  V kind;
  RectF r;
  std::wstring text, text2;
  float size = 13;
  uint32_t color = kText;
  bool bold = false;
  int id = 0;
};
std::vector<Visual> visuals;

void AddText(int page, int x, int y, int w, int h, const std::wstring& s, float size, uint32_t color,
             bool bold = false) {
  visuals.push_back({page, V::Text, RectF(F(x), F(y), F(w), F(h)), s, L"", size, color, bold});
}
// 卡片：白底圆角，左上角是图标和标题
void AddCard(int page, int x, int y, int w, int h, const wchar_t* title = nullptr, const wchar_t* icon = nullptr,
             const wchar_t* note = nullptr) {
  visuals.push_back({page, V::Card, RectF(F(x), F(y), F(w), F(h))});
  if (!title) return;
  int tx = x + 20;
  if (icon) {
    visuals.push_back({page, V::Icon, RectF(F(x + 20), F(y + 18), 28, 28), icon});
    tx = x + 58;
  }
  AddText(page, tx, y + 18, w - (tx - x) - 20, 28, title, 15, kText, true);
  if (note) AddText(page, x + 20, y + 50, w - 40, 20, note, 12.5f, kSub);
}

// ---------------------------------------------------------------- 自绘控件

enum class K { Nav, Switch, Seg, Chip, Button, Primary, Link, ThemeCard, Swatch, Keycap, KeyClear, Dropdown, Stepper, Reveal };
struct Ctl {
  HWND hwnd = nullptr;
  int page = -1;
  RECT r{};
  K kind = K::Button;
  uint32_t surface = kWhite;
  int index = 0;
  std::wstring text, icon;
};
std::map<int, Ctl> ctls;
struct EditBox { HWND hwnd; int page; RECT box; };
std::map<int, EditBox> edits;

LRESULT CALLBACK ButtonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  int id = GetDlgCtrlID(h);
  switch (msg) {
    case WM_MOUSEMOVE:
      if (hoverId != id) {
        int old = hoverId;
        hoverId = id;
        if (old && ctls.count(old)) InvalidateRect(ctls[old].hwnd, nullptr, FALSE);
        InvalidateRect(h, nullptr, FALSE);
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
      }
      break;
    case WM_MOUSELEAVE:
      if (hoverId == id) { hoverId = 0; InvalidateRect(h, nullptr, FALSE); }
      break;
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr, IDC_HAND));
      return TRUE;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefSubclassProc(h, msg, wp, lp);
}

HWND AddCtl(int id, K kind, int page, int x, int y, int w, int h, uint32_t surface, const std::wstring& text = L"",
            int index = 0, const std::wstring& icon = L"") {
  HWND hwnd = CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(x), S(y),
                              S(w), S(h), mainWnd, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr),
                              nullptr);
  SetWindowSubclass(hwnd, ButtonProc, 0, 0);
  Ctl c;
  c.hwnd = hwnd;
  c.page = page;
  c.r = {x, y, x + w, y + h};
  c.kind = kind;
  c.surface = surface;
  c.index = index;
  c.text = text;
  c.icon = icon;
  ctls[id] = c;
  return hwnd;
}
void Tip(HWND target, const wchar_t* text) {
  TOOLINFOW ti{};
  ti.cbSize = sizeof(ti);
  ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  ti.hwnd = mainWnd;
  ti.uId = reinterpret_cast<UINT_PTR>(target);
  ti.lpszText = const_cast<wchar_t*>(text);
  SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}
void AddEdit(int id, int page, int x, int y, int w, const std::wstring& text, DWORD style = 0, int rightInset = 0) {
  visuals.push_back({page, V::Field, RectF(F(x), F(y), F(w), 38), L"", L"", 13, kText, false, id});
  HWND hwnd = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | style, S(x + 13),
                              S(y + 9), S(w - 26 - rightInset), S(20), mainWnd, reinterpret_cast<HMENU>(INT_PTR(id)),
                              GetModuleHandleW(nullptr), nullptr);
  SendMessageW(hwnd, WM_SETFONT, WPARAM(inputFont), TRUE);
  SendMessageW(hwnd, EM_SETLIMITTEXT, 2048, 0);
  edits[id] = {hwnd, page, {x, y, x + w, y + 38}};
}
std::wstring EditText(int id) {
  HWND h = edits[id].hwnd;
  int n = GetWindowTextLengthW(h);
  std::wstring s(size_t(n) + 1, L'\0');
  GetWindowTextW(h, s.data(), n + 1);
  s.resize(size_t(n));
  return s;
}
void Invalidate(int id) {
  if (ctls.count(id)) InvalidateRect(ctls[id].hwnd, nullptr, FALSE);
}
void InvalidateAll() {
  InvalidateRect(mainWnd, nullptr, FALSE);
  for (auto& [id, c] : ctls) InvalidateRect(c.hwnd, nullptr, FALSE);
  for (auto& [id, e] : edits) InvalidateRect(e.hwnd, nullptr, TRUE);
}
void InvalidateBox(RECT r) {
  RECT px{S(r.left - 4), S(r.top - 4), S(r.right + 4), S(r.bottom + 4)};
  InvalidateRect(mainWnd, &px, FALSE);
}
void MarkChanged() {
  if (initializing) return;
  dirty = true;
  status = L"有未保存的修改";
  statusColor = kAccentText;
  RECT foot{0, S(kBottom), S(kW), S(kH)};
  InvalidateRect(mainWnd, &foot, FALSE);
}
void SetStatus(const std::wstring& s, uint32_t color) {
  status = s;
  statusColor = color;
  RECT foot{0, S(kBottom), S(kW), S(kH)};
  InvalidateRect(mainWnd, &foot, FALSE);
}

// ---------------------------------------------------------------- 画控件

constexpr uint32_t kSideSurface = 2;  // 控件底色用侧栏色（跟着主题变）
std::vector<std::wstring> swatches = {L"auto", L"light", L"dark", L"sakura", L"mint", L"ink", L"violet", L"amber"};
bool keyRevealed = false;

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
  std::vector<std::wstring> out;
  size_t pos = 0;
  while (true) {
    size_t e = s.find(sep, pos);
    out.push_back(s.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos));
    if (e == std::wstring::npos) break;
    pos = e + 1;
  }
  return out;
}
int SegValue(int id) { return id == IdScheme ? scheme : id == IdPageSize ? pageSize - 3 : vertical ? 1 : 0; }
float CursorX(const Ctl& c) {
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(c.hwnd, &p);
  RECT r;
  GetClientRect(c.hwnd, &r);
  if (p.x < 0 || p.y < 0 || p.x >= r.right || p.y >= r.bottom) return -1;
  return float(p.x) * 96 / float(dpi);
}
Gdiplus::Color Ca(uint32_t c, BYTE a) { return Gdiplus::Color(a, BYTE(c >> 16), BYTE(c >> 8), BYTE(c)); }

void DrawCtl(const DRAWITEMSTRUCT& d) {
  int id = int(d.CtlID);
  auto it = ctls.find(id);
  if (it == ctls.end()) return;
  Ctl& c = it->second;
  int pw = d.rcItem.right - d.rcItem.left, ph = d.rcItem.bottom - d.rcItem.top;
  HDC mem = CreateCompatibleDC(d.hDC);
  HBITMAP bmp = CreateCompatibleBitmap(d.hDC, pw, ph);
  auto oldBmp = SelectObject(mem, bmp);
  {
    Graphics g(mem);
    Setup(g);
    float w = float(pw) * 96 / float(dpi), h = float(ph) * 96 / float(dpi);
    uint32_t surface = c.surface == kSideSurface ? pal.side : c.surface;
    Gdiplus::SolidBrush bg(C(surface));
    g.FillRectangle(&bg, -1.0f, -1.0f, w + 2, h + 2);
    bool hover = hoverId == id, pressed = (d.itemState & ODS_SELECTED) != 0;
    bool focus = (d.itemState & ODS_FOCUS) && !(SendMessageW(c.hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    switch (c.kind) {
      case K::Nav: {
        bool sel = c.index == pageIndex;
        if (sel) Round(g, RectF(0, 0, w, h), 10, pal.soft);
        else if (hover) Round(g, RectF(0, 0, w, h), 10, Mix(pal.side, pal.accent, 0.07));
        if (sel) Round(g, RectF(0, 12, 3.5f, h - 24), 2, pal.accent);
        Glyph(g, c.icon.c_str(), RectF(14, 0, 24, h), 16, sel ? pal.accent : kSub);
        Text(g, c.text, RectF(48, 0, w - 56, h), 14, sel ? kAccentText : 0xFF4A5164, sel);
        break;
      }
      case K::Switch: {
        bool on = sw[c.index];
        RectF track(0, (h - 24) / 2, 44, 24);
        uint32_t fill = on ? (hover ? Mix(pal.accent, 0xFF000000, 0.08) : pal.accent) : (hover ? 0xFFC6CAD5 : kTrack);
        Round(g, track, 12, fill);
        float kx = on ? track.X + 23 : track.X + 3;
        Round(g, RectF(kx, track.Y + 3.8f, 18, 18), 9, 0x26000000);
        Round(g, RectF(kx, track.Y + 3, 18, 18), 9, kWhite);
        break;
      }
      case K::Seg: {
        auto opts = Split(c.text, L'|');
        int cur = SegValue(id);
        float segW = (w - 6) / float(opts.size());
        float mx = hover ? CursorX(c) : -1;
        int hov = mx >= 0 ? int((mx - 3) / segW) : -1;
        Round(g, RectF(0, 0, w, h), 10, kSegBg);
        for (int i = 0; i < int(opts.size()); ++i) {
          RectF seg(3 + i * segW, 3, segW, h - 6);
          if (i == cur) {
            Round(g, RectF(seg.X, seg.Y + 0.8f, seg.Width, seg.Height), 8, 0x16000000);
            Round(g, seg, 8, kWhite);
          }
          Text(g, opts[size_t(i)], seg, 13, i == cur ? kAccentText : i == hov ? kText : kSub, i == cur, Center);
        }
        break;
      }
      case K::Chip: {
        bool on = fuzzyOn[c.index];
        RectF r(0.5f, 0.5f, w - 1, h - 1);
        uint32_t line = on ? pal.accent : hover ? Mix(kFieldLine, pal.accent, 0.5) : kFieldLine;
        Round(g, r, h / 2, on ? pal.soft : kWhite, line, on ? 1.3f : 1);
        const auto& f = kFuzzy[c.index];
        float aw = Measure(g, f.a, 13, on), bw = Measure(g, f.b, 13, on);
        float x0 = (w - (aw + bw + 22)) / 2;
        uint32_t tc = on ? kAccentText : kSub;
        Text(g, f.a, RectF(x0, 0, aw + 2, h), 13, tc, on);
        Glyph(g, L"", RectF(x0 + aw, 0, 22, h), 10, on ? pal.accent : kMuted);
        Text(g, f.b, RectF(x0 + aw + 22, 0, bw + 2, h), 13, tc, on);
        break;
      }
      case K::Button: {
        RectF r(0.5f, 0.5f, w - 1, h - 1);
        Round(g, r, 10, pressed ? 0xFFEDEFF4 : hover ? 0xFFF7F8FB : kWhite,
              hover ? Mix(kFieldLine, pal.accent, 0.4) : kFieldLine);
        float tw = Measure(g, c.text, 13.5f) + (c.icon.empty() ? 0 : 24);
        float x0 = (w - tw) / 2;
        if (!c.icon.empty()) {
          Glyph(g, c.icon.c_str(), RectF(x0, 0, 18, h), 14, pal.accent);
          x0 += 24;
        }
        Text(g, c.text, RectF(x0, 0, w - x0, h), 13.5f, kText);
        break;
      }
      case K::Primary: {
        uint32_t fill = pressed ? Mix(pal.accent, 0xFF000000, 0.14) : hover ? Mix(pal.accent, 0xFF000000, 0.07) : pal.accent;
        Round(g, RectF(0, 1.5f, w, h - 1.5f), 10, Mix(fill, 0xFF000000, 0.22));
        Round(g, RectF(0, 0, w, h - 1.5f), 10, fill);
        Text(g, c.text, RectF(0, 0, w, h - 1.5f), 14, kWhite, true, Center);
        break;
      }
      case K::Link: {
        float tw = Measure(g, c.text, 13);
        Text(g, c.text, RectF(0, 0, tw + 4, h), 13, kAccentText, hover);
        Glyph(g, L"", RectF(tw + 2, 0, 16, h), 10, pal.accent);
        break;
      }
      case K::ThemeCard: {
        const Theme* t = FindTheme(kCharacters[c.index]);
        if (!t) break;
        bool sel = themeName == t->name;
        uint32_t ring = Darken(t->highlightBg, 3.0);
        RectF r(1, 1, w - 2, h - 2);
        Gdiplus::GraphicsPath p;
        RoundPath(p, r, 14);
        Gdiplus::SolidBrush cardBg(C(t->background));
        g.FillPath(&cardBg, &p);
        if (auto* art = ArtFor(*t)) {
          // 上半截露头像，下面 42 高是实底的名字区
          RectF area(r.X, r.Y, r.Width, r.Height - 40);
          Gdiplus::Region clip(&p);
          clip.Intersect(area);
          g.SetClip(&clip);
          float frac = area.Height * float(art->width) / (area.Width * float(art->height));
          ArtCover(g, *art, RectF(area.X, area.Y + (hover ? 0 : 2), area.Width, area.Height), frac);
          RectF fade(area.X, area.GetBottom() - 22, area.Width, 23);
          Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(0, fade.Y - 1), Gdiplus::PointF(0, fade.GetBottom()),
                                          Ca(t->background, 0), Ca(t->background, 255));
          g.FillRectangle(&lg, fade);
          g.ResetClip();
        }
        Text(g, Brand(*t), RectF(r.X + 12, r.Y + r.Height - 42, r.Width - 24, 20), 13.5f, t->text, true);
        Text(g, Nickname(*t), RectF(r.X + 12, r.Y + r.Height - 23, r.Width - 24, 16), 11, t->index);
        if (sel) {
          Round(g, RectF(1.6f, 1.6f, w - 3.2f, h - 3.2f), 13.5f, 0, ring, 2.6f);
          Round(g, RectF(w - 31, 9, 22, 22), 11, ring);
          Glyph(g, L"", RectF(w - 31, 9, 22, 22), 11, kWhite);
        } else {
          Round(g, r, 14, 0, hover ? Mix(t->border, ring, 0.55) : t->border, hover ? 1.5f : 1);
        }
        break;
      }
      case K::Swatch: {
        const std::wstring& name = swatches[size_t(c.index)];
        bool sel = themeName == name;
        RectF r(4, 4, w - 8, h - 8);
        if (name == L"auto") {
          Gdiplus::GraphicsPath p;
          RoundPath(p, r, r.Width / 2);
          g.SetClip(&p);
          Gdiplus::SolidBrush light(C(kWhite)), dark(C(0xFF26262C));
          g.FillRectangle(&light, r.X, r.Y, r.Width / 2, r.Height);
          g.FillRectangle(&dark, r.X + r.Width / 2, r.Y, r.Width / 2 + 1, r.Height);
          g.ResetClip();
          Round(g, r, r.Width / 2, 0, 0xFFD5D8E0);
        } else if (const Theme* t = FindTheme(name)) {
          Round(g, r, r.Width / 2, t->background, t->border);
          float dot = r.Width * 0.46f;
          Round(g, RectF(r.X + (r.Width - dot) / 2, r.Y + (r.Height - dot) / 2, dot, dot), dot / 2, t->highlightBg);
        }
        if (sel) Round(g, RectF(1, 1, w - 2, h - 2), (w - 2) / 2, 0, pal.accent, 2);
        else if (hover) Round(g, RectF(1.5f, 1.5f, w - 3, h - 3), (w - 3) / 2, 0, Mix(kLine, pal.accent, 0.5), 1.4f);
        break;
      }
      case K::Keycap: {
        bool rec = recording == c.index;
        RectF r(0.5f, 0.5f, w - 1, h - 1);
        Round(g, r, 9, rec ? kWhite : hover ? 0xFFF0F2F6 : kField, rec ? pal.accent : kFieldLine, rec ? 1.8f : 1);
        if (rec) Text(g, L"请按下新的组合键…", RectF(12, 0, w - 24, h), 12.5f, kAccentText);
        else if (keyValues[c.index].empty()) Text(g, L"未设置 · 点击录入", RectF(12, 0, w - 24, h), 12.5f, kMuted);
        else Keycaps(g, KeyParts(keyValues[c.index]), 9, h / 2);
        break;
      }
      case K::KeyClear: {
        if (hover) Round(g, RectF(0, 0, w, h), w / 2, 0xFFFCEEF0);
        Gdiplus::Pen x(C(hover ? kDanger : kMuted), 1.6f);
        x.SetStartCap(Gdiplus::LineCapRound);
        x.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&x, w / 2 - 4.5f, h / 2 - 4.5f, w / 2 + 4.5f, h / 2 + 4.5f);
        g.DrawLine(&x, w / 2 + 4.5f, h / 2 - 4.5f, w / 2 - 4.5f, h / 2 + 4.5f);
        break;
      }
      case K::Dropdown: {
        RectF r(0.5f, 0.5f, w - 1, h - 1);
        Round(g, r, 9, hover ? 0xFFF0F2F6 : kField, hover ? Mix(kFieldLine, pal.accent, 0.4) : kFieldLine);
        bool fontBox = id == IdFont;
        std::wstring v = fontBox ? fontFace : kLangLabels[language];
        Text(g, v, RectF(13, 0, w - 44, h), 13, kText, false, Near, false, fontBox ? fontFace.c_str() : L"Microsoft YaHei UI");
        Glyph(g, L"", RectF(w - 32, 0, 20, h), 10, kSub);
        break;
      }
      case K::Stepper: {
        RectF r(0.5f, 0.5f, w - 1, h - 1);
        Round(g, r, 9, kField, kFieldLine);
        float mx = hover ? CursorX(c) : -1;
        bool left = mx >= 0 && mx < 34, right = mx > w - 34;
        if (left) Round(g, RectF(3, 3, 30, h - 6), 7, 0xFFE9ECF2);
        if (right) Round(g, RectF(w - 33, 3, 30, h - 6), 7, 0xFFE9ECF2);
        Gdiplus::Pen pl(C(left ? pal.accent : kSub), 1.6f), pr(C(right ? pal.accent : kSub), 1.6f);
        float cy = h / 2;
        g.DrawLine(&pl, 13.0f, cy, 23.0f, cy);
        g.DrawLine(&pr, w - 23, cy, w - 13, cy);
        g.DrawLine(&pr, w - 18, cy - 5, w - 18, cy + 5);
        Text(g, std::to_wstring(fontSize), RectF(34, 0, w - 68, h), 14, kText, true, Center);
        break;
      }
      case K::Reveal:
        Text(g, keyRevealed ? L"隐藏" : L"显示", RectF(0, 0, w, h), 12.5f, kAccentText, hover, Center);
        break;
    }
    if (focus && c.kind != K::Keycap && c.kind != K::ThemeCard && c.kind != K::Swatch)
      Round(g, RectF(1, 1, w - 2, h - 2), c.kind == K::Chip ? h / 2 - 1 : 9, 0, Mix(pal.accent, kWhite, 0.25), 1.6f);
    if (focus && (c.kind == K::ThemeCard || c.kind == K::Swatch))
      Round(g, RectF(0.8f, 0.8f, w - 1.6f, h - 1.6f), c.kind == K::Swatch ? (w - 1.6f) / 2 : 14, 0, pal.accent, 1.2f);
  }
  BitBlt(d.hDC, d.rcItem.left, d.rcItem.top, pw, ph, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
}

// ---------------------------------------------------------------- 画页面

enum : int { VSkinInfo = 1, VAiStatus = 2 };

// 皮肤预览：一行聊天输入，下面浮着和真实候选框一样画法的候选框
void DrawPreview(Graphics& g, RectF r) {
  const Theme& t = CurrentTheme();
  Gdiplus::GraphicsPath stage;
  RoundPath(stage, r, 12);
  Gdiplus::LinearGradientBrush bg(Gdiplus::PointF(r.X, r.Y), Gdiplus::PointF(r.X + r.Width, r.Y + r.Height),
                                  C(Mix(kWhite, pal.accent, 0.16)), C(Mix(kWhite, pal.accent, 0.04)));
  g.FillPath(&bg, &stage);
  float lx = r.X + 20, ly = r.Y + 16;
  std::wstring lead = L"今天想和你说：", comp = sw[SwPreedit] ? L"ni'hao" : L"nihao";
  float pw = Measure(g, lead, 14), cw = Measure(g, comp, 14);
  Text(g, lead, RectF(lx, ly, pw + 4, 22), 14, 0xFF3A4153);
  Text(g, comp, RectF(lx + pw - 3, ly, cw + 6, 22), 14, 0xFF3A4153);
  Gdiplus::Pen dots(C(0xFF8890A4), 1.2f);
  dots.SetDashStyle(Gdiplus::DashStyleDot);
  g.DrawLine(&dots, lx + pw, ly + 20, lx + pw + cw - 6, ly + 20);
  Gdiplus::Pen caret(C(pal.accent), 1.6f);
  g.DrawLine(&caret, lx + pw + cw - 2, ly + 3, lx + pw + cw - 2, ly + 19);

  float fs = std::clamp(float(fontSize), 12.0f, 17.0f), small = fs * 0.8f;
  const wchar_t* words[] = {L"你好", L"拟好", L"你", L"尼", L"泥"};
  Artwork* art = sw[SwArt] ? ArtFor(t) : nullptr;
  float padX = 10, padY = 7, artW = art ? 62 : 0, gap = 2;
  float auxH = sw[SwPreedit] ? small * 1.45f : 0;
  float itemH = fs * 1.45f + 6;
  // 竖排时预览区放不下就少画几行
  int count = vertical ? 3 : 5;
  if (vertical)
    while (count > 1 && auxH + count * (itemH + 1) + 2 * padY > r.GetBottom() - 8 - (ly + 30)) --count;
  std::vector<float> widths;
  for (int i = 0; i < count; ++i) widths.push_back(Measure(g, std::to_wstring(i + 1), small) + Measure(g, words[i], fs) + 14);
  float contentW, contentH;
  if (vertical) {
    contentW = *std::max_element(widths.begin(), widths.end()) + 20;
    contentH = auxH + count * (itemH + 1);
  } else {
    contentW = 0;
    for (float wv : widths) contentW += wv + gap;
    contentH = auxH + itemH;
  }
  float panelW = contentW + 2 * padX + artW, panelH = std::max(contentH + 2 * padY, art ? 62.0f : 0.0f);
  RectF panel(lx + pw - 8, ly + 30, panelW, panelH);
  if (panel.GetRight() > r.GetRight() - 10) panel.X = r.GetRight() - 10 - panelW;
  float radius = std::min(t.cornerRadius, 14.0f);
  if (t.shadow) {
    for (int i = 4; i >= 1; --i) {
      float k = float(i) * 1.6f;
      Round(g, RectF(panel.X - k, panel.Y - k + 2, panel.Width + 2 * k, panel.Height + 2 * k), radius + k,
            (uint32_t(5 + (4 - i) * 4) << 24));
    }
  }
  Round(g, panel, radius, t.background, t.borderWidth > 0 ? t.border : 0, std::max(t.borderWidth, 1.0f));
  float y = panel.Y + padY;
  if (auxH > 0) {
    Text(g, L"ni'hao", RectF(panel.X + padX, y, contentW, auxH), small, t.preedit);
    y += auxH;
  }
  float x = panel.X + padX;
  for (int i = 0; i < count; ++i) {
    RectF item(x, y, vertical ? contentW : widths[size_t(i)], itemH);
    bool hl = i == 0;
    if (hl) Round(g, item, std::min(radius * 0.7f, 7.0f), t.highlightBg);
    float iw = Measure(g, std::to_wstring(i + 1), small);
    Text(g, std::to_wstring(i + 1), RectF(item.X + 6, item.Y, iw + 4, item.Height), small,
         hl ? Mix(t.highlightText, t.highlightBg, 0.25) : t.index);
    Text(g, words[i], RectF(item.X + 8 + iw, item.Y, item.Width - iw - 8, item.Height), fs, hl ? t.highlightText : t.text,
         false, Near, false, fontFace.c_str());
    if (vertical) y += itemH + 1;
    else x += widths[size_t(i)] + gap;
  }
  if (art) ArtFit(g, *art, RectF(panel.GetRight() - artW - 2, panel.Y + 4, artW, panel.Height - 6));
}

void DrawSkinInfo(Graphics& g, RectF r) {
  const Theme& t = CurrentTheme();
  bool character = CharacterIndex(t) >= 0;
  std::wstring name = themeName == L"auto" ? L"跟随系统" : character ? Brand(t) : t.label;
  Text(g, name, RectF(r.X, r.Y, r.Width, 26), 17, kText, true);
  std::wstring desc = themeName == L"auto" ? L"白天晴空，晚上夜幕，跟着系统深浅色走"
                      : character          ? Nickname(t) + L" · " + t.description
                                           : L"经典配色";
  Text(g, desc, RectF(r.X, r.Y + 28, r.Width, 18), 12, kSub);
}

void DrawAiStatus(Graphics& g, RectF r) {
  bool on = sw[SwAi];
  bool hasKey = edits.count(IdEdKey) && GetWindowTextLengthW(edits[IdEdKey].hwnd) > 0;
  Round(g, RectF(r.X, r.Y, 46, 46), 14, pal.soft);
  Glyph(g, L"", RectF(r.X, r.Y, 46, 46), 21, pal.accent);
  Text(g, L"AI 助手", RectF(r.X + 60, r.Y + 1, r.Width - 120, 24), 16, kText, true);
  std::wstring st = !on ? L"已关闭" : hasKey ? L"已就绪" : L"还没填写 API Key";
  uint32_t col = !on ? kMuted : hasKey ? kOk : kDanger;
  Round(g, RectF(r.X + 61, r.Y + 32, 7, 7), 3.5f, col);
  Text(g, st, RectF(r.X + 74, r.Y + 26, r.Width - 74 - 54, 18), 12, !on ? kSub : col);
}

void DrawVisual(Graphics& g, const Visual& v) {
  switch (v.kind) {
    case V::Card:
      Shadow(g, v.r, 16);
      Round(g, v.r, 16, kWhite, kLine);
      break;
    case V::Text:
      Text(g, v.text, v.r, v.size, v.color, v.bold);
      break;
    case V::Wrap:
      Text(g, v.text, v.r, v.size, v.color, v.bold, Near, true);
      break;
    case V::Icon:
      Round(g, v.r, 8, pal.soft);
      Glyph(g, v.text.c_str(), v.r, 14, pal.accent);
      break;
    case V::Divider: {
      Gdiplus::Pen p(C(kLine), 1);
      g.DrawLine(&p, v.r.X, v.r.Y, v.r.X + v.r.Width, v.r.Y);
      break;
    }
    case V::Field: {
      bool focus = edits.count(v.id) && GetFocus() == edits[v.id].hwnd;
      Round(g, v.r, 9, kField, focus ? pal.accent : kFieldLine, focus ? 1.6f : 1);
      break;
    }
    case V::Callout:
      Round(g, v.r, 16, pal.soft);
      Round(g, RectF(v.r.X + 20, v.r.Y + 20, 32, 32), 10, kWhite);
      Glyph(g, L"", RectF(v.r.X + 20, v.r.Y + 20, 32, 32), 15, pal.accent);
      Text(g, v.text, RectF(v.r.X + 64, v.r.Y + 20, v.r.Width - 84, 32), 14.5f, kAccentText, true);
      Text(g, v.text2, RectF(v.r.X + 20, v.r.Y + 66, v.r.Width - 40, v.r.Height - 80), 12.5f, 0xFF4A5164, false, Near,
           true);
      break;
    case V::Preview:
      DrawPreview(g, v.r);
      break;
    case V::Fixed: {
      Keycaps(g, Split(v.text, L' '), v.r.X, v.r.Y + v.r.Height / 2);
      Text(g, v.text2, RectF(v.r.X + 96, v.r.Y, v.r.Width - 96, v.r.Height), 13, kSub, false, Far);
      break;
    }
  }
  if (v.kind == V::Text && v.id == VSkinInfo) DrawSkinInfo(g, v.r);
  if (v.kind == V::Text && v.id == VAiStatus) DrawAiStatus(g, v.r);
}

const wchar_t* kTitles[] = {L"输入习惯", L"AI 娘皮肤", L"AI 与翻译", L"快捷键"};
const wchar_t* kSubtitles[] = {L"输入方案、候选和打字时的小习惯", L"十位 AI 娘，挑一位陪你打字",
                               L"组句、润色、续写和翻译，按需开启", L"常用操作的组合键，点一下就能改"};

void DrawCompanion(Graphics& g) {
  const Theme& t = CurrentTheme();
  int ci = CharacterIndex(t);
  Artwork* art = ci >= 0 ? ArtFor(t) : nullptr;
  std::wstring line = art ? kGreetings[ci] : L"挑一位 AI 娘陪你打字吧～";
  bool twoLines = Measure(g, line, 12.5f) > 158;
  float bh = twoLines ? 58 : 40;
  RectF bubble(18, 384 - bh, 188, bh);
  Round(g, bubble, 14, kWhite, Mix(kLine, pal.accent, 0.3));
  Gdiplus::PointF tail[] = {{58, bubble.GetBottom() - 1}, {72, bubble.GetBottom() - 1}, {60, bubble.GetBottom() + 9}};
  Gdiplus::SolidBrush white(C(kWhite));
  g.FillPolygon(&white, tail, 3);
  Gdiplus::Pen edge(C(Mix(kLine, pal.accent, 0.3)), 1);
  g.DrawLine(&edge, tail[1], tail[2]);
  g.DrawLine(&edge, tail[0], tail[2]);
  if (twoLines)
    Text(g, line, RectF(bubble.X + 14, bubble.Y + 9, bubble.Width - 26, bubble.Height - 14), 12.5f, 0xFF3A4153, false,
         Near, true);
  else
    Text(g, line, RectF(bubble.X + 14, bubble.Y, bubble.Width - 26, bubble.Height), 12.5f, 0xFF3A4153);
  if (art) {
    RectF ar(10, 396, 204, 254);
    g.SetClip(ar);
    ArtCover(g, *art, ar, 0.78f);
    RectF fade(ar.X, ar.GetBottom() - 44, ar.Width, 45);
    Gdiplus::LinearGradientBrush lg(Gdiplus::PointF(0, fade.Y - 1), Gdiplus::PointF(0, fade.GetBottom()), Ca(pal.side, 0),
                                    Ca(pal.side, 255));
    g.FillRectangle(&lg, fade);
    g.ResetClip();
    Text(g, Brand(t) + L" · " + Nickname(t), RectF(12, 652, 200, 20), 12, kSub, false, Center);
  } else {
    Glyph(g, L"", RectF(12, 440, 200, 120), 64, Mix(pal.side, pal.accent, 0.35));
  }
}

void Paint(HWND wnd) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(wnd, &ps);
  RECT cr;
  GetClientRect(wnd, &cr);
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
  auto oldBmp = SelectObject(mem, bmp);
  {
    Graphics g(mem);
    Setup(g);
    Gdiplus::SolidBrush bg(C(kBg)), side(C(pal.side));
    g.FillRectangle(&bg, 0.0f, 0.0f, float(kW) + 2, float(kH) + 2);
    g.FillRectangle(&side, 0.0f, 0.0f, float(kSide), float(kH) + 2);
    Gdiplus::Pen divider(C(Mix(kLine, pal.accent, 0.12)), 1);
    g.DrawLine(&divider, float(kSide) - 0.5f, 0.0f, float(kSide) - 0.5f, float(kH) + 2);
    // 标志
    RectF logo(24, 30, 40, 40);
    Gdiplus::GraphicsPath lp;
    RoundPath(lp, logo, 12);
    Gdiplus::LinearGradientBrush lgb(Gdiplus::PointF(24, 30), Gdiplus::PointF(64, 70), C(Mix(pal.accent, kWhite, 0.25)),
                                     C(Darken(pal.accent, 4.0)));
    g.FillPath(&lgb, &lp);
    Text(g, L"yy", logo, 19, kWhite, true, Center, false, L"Segoe UI");
    Text(g, L"yy 输入法", RectF(76, 30, 140, 22), 16.5f, kText, true);
    Text(g, L"偏好设置", RectF(76, 52, 140, 18), 12, kSub);
    DrawCompanion(g);
    // 标题
    Text(g, kTitles[pageIndex], RectF(F(kX), 32, 600, 36), 25, kText, true);
    Text(g, kSubtitles[pageIndex], RectF(F(kX), 70, 600, 20), 13, kSub);
    for (const auto& v : visuals)
      if (v.page == pageIndex) DrawVisual(g, v);
    // 底栏
    g.DrawLine(&divider, float(kSide), float(kBottom) + 0.5f, float(kW), float(kBottom) + 0.5f);
    Round(g, RectF(F(kX), F(kBottom) + 31, 8, 8), 4, statusColor == kMuted ? kTrack : statusColor);
    Text(g, status, RectF(F(kX) + 16, F(kBottom), 520, F(kH - kBottom)), 13, statusColor == kMuted ? kSub : statusColor);
  }
  BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(wnd, &ps);
}

// ---------------------------------------------------------------- 搭页面

void LoadState() {
  for (int i = 0; i < 5; ++i)
    if (std::wstring(kSchemeIds[i]) == SchemeName(config.scheme)) scheme = i;
  pageSize = std::clamp(config.pageSize, 3, 9);
  fontSize = std::clamp(int(std::lround(config.fontSize)), 10, 40);
  fontFace = config.fontFace.empty() ? L"Microsoft YaHei UI" : config.fontFace;
  vertical = config.vertical;
  themeName = config.themeName.empty() ? L"auto" : config.themeName;
  bool values[SwCount] = {config.sentenceModel,  config.learn,         config.englishMixed,   config.fullwidthPunct,
                          config.shiftToggle,    config.startChinese,  config.dynamicPhrases, config.wubiAutoCommit,
                          config.preeditInWindow, config.characterArt, config.aiEnabled};
  std::copy(values, values + SwCount, sw);
  for (int i = 0; i < 7; ++i)
    if (config.translateTarget == kLangIds[i]) language = i;
  std::wstring keys[kKeyCount] = {config.aiComposeKey, config.aiHotkey, config.translateHotkey, config.settingsHotkey,
                                  config.punctHotkey};
  std::copy(keys, keys + kKeyCount, keyValues);
  // 模糊音：认识的点亮成标签，不认识的原样保留
  std::wstring spec = config.fuzzy;
  for (auto& ch : spec)
    if (ch == L',' || ch == L';' || ch == L'\t') ch = L' ';
  for (const auto& token : Split(spec, L' ')) {
    auto eq = token.find(L'=');
    if (eq == std::wstring::npos) continue;
    std::wstring a = token.substr(0, eq), b = token.substr(eq + 1);
    bool known = false;
    for (int i = 0; i < kFuzzyCount; ++i)
      if ((a == kFuzzy[i].a && b == kFuzzy[i].b) || (a == kFuzzy[i].b && b == kFuzzy[i].a)) fuzzyOn[i] = known = true;
    if (!known) fuzzyOther += (fuzzyOther.empty() ? L"" : L" ") + token;
  }
}

void BuildPages() {
  // 侧栏导航
  const wchar_t* navIcons[] = {L"", L"", L"", L""};
  for (int i = 0; i < kPages; ++i) AddCtl(IdNav + i, K::Nav, -1, 16, 98 + i * 50, 192, 42, kSideSurface, kTitles[i], i, navIcons[i]);
  AddCtl(IdGoSkins, K::Button, -1, 30, 580, 164, 40, kSideSurface, L"去看看皮肤", 0, L"");

  // ---- 输入习惯
  const int lw = 364, rx = kX + lw + 24, rw = 364;
  AddCard(0, kX, kTop, lw, 172, L"输入方案", L"");
  AddCtl(IdScheme, K::Seg, 0, kX + 20, kTop + 60, lw - 40, 38, kWhite, L"全拼|小鹤|自然码|微软|五笔");
  AddText(0, kX + 20, kTop + 112, 80, 38, L"每页候选", 13, kSub);
  AddCtl(IdPageSize, K::Seg, 0, kX + 100, kTop + 112, lw - 120, 38, kWhite, L"3|4|5|6|7|8|9");
  int fy = kTop + 172 + 16;
  AddCard(0, kX, fy, lw, 196, L"模糊音", L"");
  AddText(0, kX + 20, fy + 50, lw - 40, 20, L"点亮以后，两个音打哪个都能出字", 12.5f, kSub);
  int cx = kX + 20, cy = fy + 82;
  for (int i = 0; i < kFuzzyCount; ++i) {
    if (cx + kFuzzy[i].width > kX + lw - 20) { cx = kX + 20; cy += 36; }
    AddCtl(IdChip + i, K::Chip, 0, cx, cy, kFuzzy[i].width, 30, kWhite, L"", i);
    cx += kFuzzy[i].width + 8;
  }
  int dy = fy + 196 + 16;
  AddCard(0, kX, dy, lw, kBottom - dy, L"我的词库", L"");
  int bw = (lw - 52) / 2;
  AddCtl(IdPhrases, K::Button, 0, kX + 20, dy + 62, bw, 40, kWhite, L"自定义短语", 0, L"");
  AddCtl(IdFolder, K::Button, 0, kX + 32 + bw, dy + 62, bw, 40, kWhite, L"用户文件夹", 0, L"");

  AddCard(0, rx, kTop, rw, kBottom - kTop, L"打字习惯", L"");
  for (int i = 0; i < 8; ++i) {
    int ry = kTop + 58 + i * 57;
    AddText(0, rx + 20, ry + 9, rw - 100, 20, kHabitRows[i].title, 14, kText);
    AddText(0, rx + 20, ry + 30, rw - 100, 18, kHabitRows[i].desc, 12, kSub);
    AddCtl(IdSwitch + i, K::Switch, 0, rx + rw - 64, ry + 16, 44, 24, kWhite, L"", i);
    if (i < 7) visuals.push_back({0, V::Divider, RectF(F(rx + 20), F(ry + 57), F(rw - 40), 0)});
  }

  // ---- AI 娘皮肤
  AddCard(1, kX, kTop, 752, 180);
  visuals.push_back({1, V::Preview, RectF(F(kX + 14), F(kTop + 14), 470, 152)});
  int sx = kX + 504, swd = 752 - 504 - 22;
  visuals.push_back({1, V::Text, RectF(F(sx), F(kTop + 18), F(swd), 48), L"", L"", 13, kText, false, VSkinInfo});
  AddText(1, sx, kTop + 74, 150, 26, L"候选框上显示拼音", 13, kText);
  AddCtl(IdSwitch + int(SwPreedit), K::Switch, 1, sx + swd - 44, kTop + 75, 44, 24, kWhite, L"", SwPreedit);
  AddText(1, sx, kTop + 106, 150, 26, L"显示角色装饰", 13, kText);
  AddCtl(IdSwitch + int(SwArt), K::Switch, 1, sx + swd - 44, kTop + 107, 44, 24, kWhite, L"", SwArt);
  AddCtl(IdLayout, K::Seg, 1, sx, kTop + 138, swd, 34, kWhite, L"横排|竖排");
  int gy = kTop + 180 + 14;
  for (int i = 0; i < 10; ++i)
    AddCtl(IdCard + i, K::ThemeCard, 1, kX + (i % 5) * 153, gy + (i / 5) * 136, 140, 124, kBg, L"", i);
  int by = gy + 2 * 136 + 2;
  AddCard(1, kX, by, 752, kBottom - by);
  AddText(1, kX + 20, by, 70, kBottom - by, L"经典配色", 13, kSub);
  static std::vector<std::wstring> tips;
  tips.reserve(16);
  for (int i = 0; i < int(swatches.size()); ++i) {
    HWND h = AddCtl(IdSwatch + i, K::Swatch, 1, kX + 90 + i * 36, by + (kBottom - by - 32) / 2, 32, 32, kWhite, L"", i);
    const Theme* t = FindTheme(swatches[size_t(i)]);
    tips.push_back(swatches[size_t(i)] == L"auto" ? L"跟随系统深浅色" : t ? t->label : swatches[size_t(i)]);
    Tip(h, tips.back().c_str());
  }
  int stepX = kRight - 20 - 104, fontX = stepX - 12 - 176;
  AddText(1, fontX - 42, by, 40, kBottom - by, L"字体", 13, kSub);
  AddCtl(IdFont, K::Dropdown, 1, fontX, by + (kBottom - by - 38) / 2, 176, 38, kWhite);
  AddCtl(IdFontSize, K::Stepper, 1, stepX, by + (kBottom - by - 38) / 2, 104, 38, kWhite);

  // ---- AI 与翻译
  const int lw2 = 440, rx2 = kX + lw2 + 20, rw2 = 752 - lw2 - 20;
  AddCard(2, kX, kTop, lw2, kBottom - kTop, L"接口设置", L"");
  struct Field { int id; const wchar_t* label; std::wstring value; DWORD style; int width; };
  Field fields[] = {{IdEdKey, L"API Key", config.aiKey, ES_PASSWORD, lw2 - 40},
                    {IdEdUrl, L"接口地址", config.aiBaseUrl, 0, lw2 - 40},
                    {IdEdModel, L"模型名称", config.aiModel, 0, lw2 - 40},
                    {IdEdProxy, L"代理（可选）", config.aiProxy, 0, lw2 - 40},
                    {IdEdTimeout, L"超时（毫秒）", std::to_wstring(config.aiTimeoutMs), ES_NUMBER, 180}};
  for (int i = 0; i < 5; ++i) {
    int ry = kTop + 60 + i * 78;
    AddText(2, kX + 20, ry, 300, 18, fields[i].label, 12.5f, kSub);
    AddEdit(fields[i].id, 2, kX + 20, ry + 22, fields[i].width, fields[i].value, fields[i].style, i == 0 ? 52 : 0);
    if (i == 0) {
      AddCtl(IdReveal, K::Reveal, 2, kX + lw2 - 20 - 54, ry + 26, 48, 30, kField);
      SendMessageW(edits[IdEdKey].hwnd, EM_SETPASSWORDCHAR, L'●', 0);
    }
  }
  visuals.push_back({2, V::Wrap, RectF(F(kX + 20), F(kTop + 60 + 5 * 78 + 4), F(lw2 - 40), 60),
                     L"兼容 OpenAI 格式的接口都能用（DeepSeek、通义、Kimi、智谱等），改接口地址和模型名称就行。代理一般不用填。",
                     L"", 12.5f, kSub});
  AddCard(2, rx2, kTop, rw2, 176);
  visuals.push_back({2, V::Text, RectF(F(rx2 + 20), F(kTop + 22), F(rw2 - 40), 50), L"", L"", 13, kText, false, VAiStatus});
  AddCtl(IdSwitch + int(SwAi), K::Switch, 2, rx2 + rw2 - 64, kTop + 33, 44, 24, kWhite, L"", SwAi);
  visuals.push_back({2, V::Wrap, RectF(F(rx2 + 20), F(kTop + 82), F(rw2 - 40), 40),
                     L"打拼音时按 Tab 组句；选中文字可以润色、续写、翻译", L"", 12.5f, kSub});
  AddCtl(IdGetKey, K::Link, 2, rx2 + 20, kTop + 128, rw2 - 40, 30, kWhite, L"去 DeepSeek 开放平台申请 Key");
  int ty = kTop + 176 + 16;
  AddCard(2, rx2, ty, rw2, 156, L"翻译", L"");
  visuals.push_back({2, V::Wrap, RectF(F(rx2 + 20), F(ty + 50), F(rw2 - 40), 40),
                     L"选中文字后按翻译键，在候选里挑一个译文替换原文", L"", 12.5f, kSub});
  AddCtl(IdLanguage, K::Dropdown, 2, rx2 + 20, ty + 98, rw2 - 40, 38, kWhite);
  int py = ty + 156 + 16;
  visuals.push_back({2, V::Callout, RectF(F(rx2), F(py), F(rw2), F(kBottom - py)), L"只在你按键时才联网",
                     L"平时打字全在本机完成。只有按下 AI 或翻译的快捷键时，才会把光标附近的文字和选中的内容发给 AI 服务。"});

  // ---- 快捷键
  const int lw3 = 476, rx3 = kX + lw3 + 20, rw3 = 752 - lw3 - 20;
  AddCard(3, kX, kTop, lw3, kBottom - kTop, L"自定义快捷键", L"");
  AddText(3, kX + 20, kTop + 50, lw3 - 40, 20, L"点一下键位，再按下新的组合键；Backspace 停用，Esc 取消", 12.5f, kSub);
  for (int i = 0; i < kKeyCount; ++i) {
    int ry = kTop + 80 + i * 74;
    visuals.push_back({3, V::Icon, RectF(F(kX + 20), F(ry + 19), 34, 34), kKeyRows[i].icon});
    AddText(3, kX + 68, ry + 15, 176, 22, kKeyRows[i].title, 14, kText);
    AddText(3, kX + 68, ry + 38, 176, 18, kKeyRows[i].desc, 12, kSub);
    AddCtl(IdKey + i, K::Keycap, 3, kX + lw3 - 20 - 30 - 6 - 168, ry + 17, 168, 38, kWhite, L"", i);
    HWND clear = AddCtl(IdKeyClear + i, K::KeyClear, 3, kX + lw3 - 20 - 30, ry + 21, 30, 30, kWhite, L"", i);
    Tip(clear, L"清除，停用这个快捷键");
    if (i < kKeyCount - 1) visuals.push_back({3, V::Divider, RectF(F(kX + 20), F(ry + 72), F(lw3 - 40), 0)});
  }
  AddCtl(IdReset, K::Button, 3, kX + 20, kBottom - 20 - 40, 150, 40, kWhite, L"恢复默认", 0, L"");
  AddCard(3, rx3, kTop, rw3, kBottom - kTop, L"固定按键", L"");
  for (int i = 0; i < 8; ++i)
    visuals.push_back({3, V::Fixed, RectF(F(rx3 + 20), F(kTop + 62 + i * 56), F(rw3 - 40), 40), kFixedKeys[i][0],
                       kFixedKeys[i][1]});

  // ---- 底栏
  AddCtl(IdClose, K::Button, -1, kRight - 96, kBottom + 14, 96, 40, kBg, L"关闭");
  AddCtl(IdSave, K::Primary, -1, kRight - 96 - 12 - 124, kBottom + 14, 124, 40, kBg, L"保存设置");
}

void UpdateVisibility() {
  bool character = CharacterIndex(CurrentTheme()) >= 0 && ArtFor(CurrentTheme());
  for (auto& [id, c] : ctls) {
    bool show = c.page == -1 || c.page == pageIndex;
    if (id == IdGoSkins) show = !character && pageIndex != 1;
    ShowWindow(c.hwnd, show ? SW_SHOW : SW_HIDE);
  }
  for (auto& [id, e] : edits) ShowWindow(e.hwnd, e.page == pageIndex ? SW_SHOW : SW_HIDE);
}
void StopRecording() {
  int old = recording;
  recording = -1;
  if (old >= 0) Invalidate(IdKey + old);
}
void SwitchPage(int page) {
  StopRecording();
  pageIndex = std::clamp(page, 0, kPages - 1);
  UpdateVisibility();
  InvalidateAll();
}
void ChooseTheme(const std::wstring& name) {
  themeName = name;
  UpdatePalette();
  UpdateVisibility();
  InvalidateAll();
  MarkChanged();
}

// ---------------------------------------------------------------- 保存

bool Error(const wchar_t* message, int focusId, int page) {
  SwitchPage(page);
  SetStatus(message, kDanger);
  if (edits.count(focusId)) SetFocus(edits[focusId].hwnd);
  return false;
}
bool Save() {
  std::vector<KeyChord> keys;
  for (int i = 0; i < kKeyCount; ++i) {
    if (keyValues[i].empty()) continue;
    KeyChord k = ParseKeyChord(keyValues[i]);
    if (!SafeShortcut(k, kKeyRows[i].compose)) return Error(L"有个快捷键不能用，请到快捷键页重新录一下", 0, 3);
    for (const auto& old : keys)
      if (old == k) return Error(L"两个功能用了同一个快捷键，请改成不同的", 0, 3);
    keys.push_back(k);
  }
  std::wstring timeout = EditText(IdEdTimeout), url = EditText(IdEdUrl), model = EditText(IdEdModel);
  long long ms = timeout.empty() ? 0 : _wtoi64(timeout.c_str());
  if (ms < 3000 || ms > 120000) return Error(L"超时请填 3000 到 120000 之间的毫秒数", IdEdTimeout, 2);
  if (sw[SwAi] && url.rfind(L"https://", 0) != 0 && url.rfind(L"http://", 0) != 0)
    return Error(L"接口地址要以 https:// 或 http:// 开头", IdEdUrl, 2);
  if (sw[SwAi] && model.empty()) return Error(L"请填写模型名称", IdEdModel, 2);

  std::wstring fuzzy;
  for (int i = 0; i < kFuzzyCount; ++i)
    if (fuzzyOn[i]) fuzzy += (fuzzy.empty() ? L"" : L" ") + std::wstring(kFuzzy[i].a) + L"=" + kFuzzy[i].b;
  if (!fuzzyOther.empty()) fuzzy += (fuzzy.empty() ? L"" : L" ") + fuzzyOther;

  std::vector<ConfigChange> changes;
  auto value = [&](const char* section, const char* key, const std::wstring& text) {
    changes.push_back({section, key, WideToUtf8(text)});
  };
  auto flag = [&](const char* section, const char* key, int index) {
    changes.push_back({section, key, sw[index] ? "true" : "false"});
  };
  value("general", "scheme", kSchemeIds[scheme]);
  value("general", "page_size", std::to_wstring(pageSize));
  flag("general", "sentence_model", SwSentence);
  flag("general", "english_mixed", SwEnglish);
  flag("general", "shift_toggle", SwShift);
  flag("general", "fullwidth_punct", SwPunct);
  flag("general", "learn", SwLearn);
  flag("general", "dynamic_phrases", SwDynamic);
  flag("general", "start_chinese", SwStartCn);
  flag("general", "wubi_auto_commit", SwWubi);
  value("general", "fuzzy", fuzzy);
  value("ui", "theme", themeName);
  value("ui", "layout", vertical ? L"vertical" : L"horizontal");
  value("ui", "font", fontFace);
  value("ui", "font_size", std::to_wstring(fontSize));
  flag("ui", "preedit_in_window", SwPreedit);
  flag("ui", "character_art", SwArt);
  flag("ai", "enabled", SwAi);
  value("ai", "api_key", EditText(IdEdKey));
  value("ai", "base_url", url);
  value("ai", "model", model);
  value("ai", "proxy", EditText(IdEdProxy));
  value("ai", "timeout_ms", std::to_wstring(ms));
  value("ai", "translate_target", kLangIds[language]);
  value("ai", "compose_key", keyValues[0]);
  value("ai", "hotkey", keyValues[1]);
  value("ai", "translate_hotkey", keyValues[2]);
  value("hotkeys", "settings", keyValues[3]);
  value("hotkeys", "punctuation", keyValues[4]);
  if (!SaveConfigValues(configPath, changes)) {
    SetStatus(L"没能保存，请检查设置文件能不能写入", kDanger);
    return false;
  }
  dirty = false;
  SetStatus(L"已保存，下次按键时就会用上新设置", kOk);
  return true;
}

// ---------------------------------------------------------------- 交互

BOOL CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
  *reinterpret_cast<bool*>(found) = true;
  return FALSE;
}
bool FontInstalled(const std::wstring& name) {
  LOGFONTW lf{};
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, name.c_str(), LF_FACESIZE);
  bool found = false;
  HDC dc = GetDC(nullptr);
  EnumFontFamiliesExW(dc, &lf, reinterpret_cast<FONTENUMPROCW>(FontExists), reinterpret_cast<LPARAM>(&found), 0);
  ReleaseDC(nullptr, dc);
  return found;
}
int PopupAt(int id, const std::vector<std::wstring>& items, int checked) {
  HMENU menu = CreatePopupMenu();
  for (size_t i = 0; i < items.size(); ++i)
    AppendMenuW(menu, MF_STRING | (int(i) == checked ? MF_CHECKED : 0), i + 1, items[i].c_str());
  RECT r;
  GetWindowRect(ctls[id].hwnd, &r);
  int cmd = int(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom + 4, mainWnd, nullptr));
  DestroyMenu(menu);
  return cmd - 1;
}
void OnClick(int id) {
  if (recording >= 0 && !(id >= IdKey && id < IdKey + kKeyCount)) StopRecording();
  if (id >= IdNav && id < IdNav + kPages) { SwitchPage(id - IdNav); return; }
  if (id >= IdSwitch && id < IdSwitch + int(SwCount)) {
    sw[id - IdSwitch] = !sw[id - IdSwitch];
    Invalidate(id);
    if (id - IdSwitch == SwAi) InvalidateRect(mainWnd, nullptr, FALSE);
    if (id - IdSwitch == SwPreedit || id - IdSwitch == SwArt) InvalidateRect(mainWnd, nullptr, FALSE);
    MarkChanged();
    return;
  }
  if (id >= IdChip && id < IdChip + kFuzzyCount) {
    fuzzyOn[id - IdChip] = !fuzzyOn[id - IdChip];
    Invalidate(id);
    MarkChanged();
    return;
  }
  if (id >= IdCard && id < IdCard + 10) { ChooseTheme(kCharacters[id - IdCard]); return; }
  if (id >= IdSwatch && id < IdSwatch + int(swatches.size())) { ChooseTheme(swatches[size_t(id - IdSwatch)]); return; }
  if (id >= IdKey && id < IdKey + kKeyCount) {
    int i = id - IdKey;
    if (recording == i) { StopRecording(); return; }
    StopRecording();
    recording = i;
    SetStatus(L"请按下新的组合键，Esc 取消，Backspace 停用", kAccentText);
    Invalidate(id);
    SetFocus(ctls[id].hwnd);
    return;
  }
  if (id >= IdKeyClear && id < IdKeyClear + kKeyCount) {
    keyValues[id - IdKeyClear].clear();
    Invalidate(IdKey + id - IdKeyClear);
    MarkChanged();
    return;
  }
  switch (id) {
    case IdScheme:
    case IdPageSize:
    case IdLayout: {
      Ctl& c = ctls[id];
      int n = int(Split(c.text, L'|').size());
      float x = CursorX(c);
      float w = float(c.r.right - c.r.left);
      int i = x < 0 ? (SegValue(id) + 1) % n : std::clamp(int((x - 3) / ((w - 6) / n)), 0, n - 1);
      if (id == IdScheme) scheme = i;
      else if (id == IdPageSize) pageSize = i + 3;
      else vertical = i == 1;
      Invalidate(id);
      if (id == IdLayout) InvalidateRect(mainWnd, nullptr, FALSE);
      MarkChanged();
      return;
    }
    case IdFontSize: {
      Ctl& c = ctls[id];
      float x = CursorX(c), w = float(c.r.right - c.r.left);
      int delta = x >= 0 && x < 34 ? -1 : (x < 0 || x > w - 34) ? 1 : 0;
      if (!delta) return;
      fontSize = std::clamp(fontSize + delta, 10, 40);
      Invalidate(id);
      InvalidateRect(mainWnd, nullptr, FALSE);
      MarkChanged();
      return;
    }
    case IdFont: {
      std::vector<std::wstring> fonts;
      for (const wchar_t* f : kFonts)
        if (FontInstalled(f)) fonts.push_back(f);
      if (std::find(fonts.begin(), fonts.end(), fontFace) == fonts.end()) fonts.insert(fonts.begin(), fontFace);
      int cur = int(std::find(fonts.begin(), fonts.end(), fontFace) - fonts.begin());
      int pick = PopupAt(id, fonts, cur);
      if (pick >= 0 && fonts[size_t(pick)] != fontFace) {
        fontFace = fonts[size_t(pick)];
        Invalidate(id);
        InvalidateRect(mainWnd, nullptr, FALSE);
        MarkChanged();
      }
      return;
    }
    case IdLanguage: {
      std::vector<std::wstring> labels(std::begin(kLangLabels), std::end(kLangLabels));
      int pick = PopupAt(id, labels, language);
      if (pick >= 0 && pick != language) {
        language = pick;
        Invalidate(id);
        MarkChanged();
      }
      return;
    }
    case IdReveal:
      keyRevealed = !keyRevealed;
      SendMessageW(edits[IdEdKey].hwnd, EM_SETPASSWORDCHAR, keyRevealed ? 0 : L'●', 0);
      InvalidateRect(edits[IdEdKey].hwnd, nullptr, TRUE);
      Invalidate(id);
      return;
    case IdGetKey:
      ShellExecuteW(mainWnd, L"open", L"https://platform.deepseek.com/api_keys", nullptr, nullptr, SW_SHOWNORMAL);
      return;
    case IdGoSkins:
      SwitchPage(1);
      return;
    case IdReset: {
      Config defaults;
      std::wstring keys[kKeyCount] = {defaults.aiComposeKey, defaults.aiHotkey, defaults.translateHotkey,
                                      defaults.settingsHotkey, defaults.punctHotkey};
      std::copy(keys, keys + kKeyCount, keyValues);
      for (int i = 0; i < kKeyCount; ++i) Invalidate(IdKey + i);
      MarkChanged();
      return;
    }
    case IdPhrases:
    case IdFolder: {
      auto folder = DirName(configPath);
      if (id == IdPhrases) {
        auto file = folder + L"\\custom_phrases.txt";
        if (!FileExists(file)) WriteTextFile(file, "# 每行：短语<Tab>缩写<Tab>候选位置\r\n");
        auto args = L"\"" + file + L"\"";
        ShellExecuteW(mainWnd, L"open", L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
      } else {
        ShellExecuteW(mainWnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      return;
    }
    case IdSave:
      Save();
      return;
    case IdClose:
      SendMessageW(mainWnd, WM_CLOSE, 0, 0);
      return;
  }
}

bool eatAltUp = false;
// 录快捷键时，按键先到这里
bool RecordKey(UINT vk) {
  if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_CAPITAL)
    return true;
  bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, alt = GetKeyState(VK_MENU) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
  int i = recording;
  eatAltUp = alt;
  if (!ctrl && !alt && !shift && vk == VK_ESCAPE) {
    StopRecording();
    SetStatus(dirty ? L"有未保存的修改" : L"改动后点「保存」生效", dirty ? kAccentText : kMuted);
    return true;
  }
  if (!ctrl && !alt && !shift && (vk == VK_BACK || vk == VK_DELETE)) {
    keyValues[i].clear();
    StopRecording();
    MarkChanged();
    return true;
  }
  KeyChord k{vk, ctrl, alt, shift};
  std::wstring spec = FormatKeyChord(k);
  if (spec.empty() || !SafeShortcut(k, kKeyRows[i].compose)) {
    SetStatus(L"这个键不适合当快捷键，换一个试试：Ctrl / Alt 组合键或 F1–F24", kDanger);
    return true;
  }
  for (int j = 0; j < kKeyCount; ++j) {
    if (j != i && !keyValues[j].empty() && ParseKeyChord(keyValues[j]) == k) {
      SetStatus(std::wstring(L"和「") + kKeyRows[j].title + L"」重复了，换一个吧", kDanger);
      return true;
    }
  }
  keyValues[i] = spec;
  StopRecording();
  MarkChanged();
  return true;
}

UINT FitDpi(UINT actual, const RECT& work) {
  return UINT(std::max(48, std::min({int(actual), (int(work.right - work.left) - 28) * 96 / kW,
                                     (int(work.bottom - work.top) - 52) * 96 / kH})));
}
void MakeFonts() {
  if (inputFont) DeleteObject(inputFont);
  inputFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                          L"Microsoft YaHei UI");
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  static HBRUSH fieldBrush = CreateSolidBrush(RGB((kField >> 16) & 255, (kField >> 8) & 255, kField & 255));
  switch (msg) {
    case WM_CREATE:
      mainWnd = wnd;
      LoadState();
      UpdatePalette();
      MakeFonts();
      tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0,
                                0, 0, wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
      BuildPages();
      // 设置窗口里不用输入法：录快捷键时按键不会被 yy 自己的快捷键吃掉
      ImmAssociateContextEx(wnd, nullptr, IACE_CHILDREN);
      initializing = false;
      SwitchPage(pageIndex);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      Paint(wnd);
      return 0;
    case WM_DRAWITEM:
      DrawCtl(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
      return TRUE;
    case WM_CTLCOLOREDIT: {
      HDC hdc = reinterpret_cast<HDC>(wp);
      SetBkColor(hdc, RGB((kField >> 16) & 255, (kField >> 8) & 255, kField & 255));
      SetTextColor(hdc, RGB((kText >> 16) & 255, (kText >> 8) & 255, kText & 255));
      return reinterpret_cast<LRESULT>(fieldBrush);
    }
    case WM_LBUTTONDOWN:
      StopRecording();
      SetFocus(wnd);
      return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      if (edits.count(id)) {
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateBox(edits[id].box);
        if (code == EN_CHANGE && !initializing) {
          MarkChanged();
          if (id == IdEdKey) InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
      }
      if (code == BN_CLICKED) OnClick(id);
      if (code == BN_KILLFOCUS && id == IdKey + recording) StopRecording();
      return 0;
    }
    case WM_DPICHANGED: {
      if (shotMode) return 0;
      auto rect = reinterpret_cast<RECT*>(lp);
      MONITORINFO monitor{sizeof(monitor)};
      GetMonitorInfoW(MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST), &monitor);
      dpi = FitDpi(HIWORD(wp), monitor.rcWork);
      MakeFonts();
      RECT frame{0, 0, S(kW), S(kH)};
      AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(wnd, GWL_STYLE)), FALSE, WS_EX_CONTROLPARENT, HIWORD(wp));
      int width = frame.right - frame.left, height = frame.bottom - frame.top;
      int left = std::clamp(int(rect->left), int(monitor.rcWork.left), int(monitor.rcWork.right) - width);
      int top = std::clamp(int(rect->top), int(monitor.rcWork.top), int(monitor.rcWork.bottom) - height);
      SetWindowPos(wnd, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
      for (auto& [id, c] : ctls)
        MoveWindow(c.hwnd, S(c.r.left), S(c.r.top), S(c.r.right - c.r.left), S(c.r.bottom - c.r.top), TRUE);
      for (auto& [id, e] : edits) {
        int extra = id == IdEdKey ? 52 : 0;
        MoveWindow(e.hwnd, S(e.box.left + 13), S(e.box.top + 9), S(e.box.right - e.box.left - 26 - extra), S(20), TRUE);
        SendMessageW(e.hwnd, WM_SETFONT, WPARAM(inputFont), TRUE);
      }
      InvalidateAll();
      return 0;
    }
    case WM_CLOSE:
      if (dirty) {
        int answer = MessageBoxW(wnd, L"有修改还没保存，要保存吗？", L"yy输入法设置", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !Save())) return 0;
      }
      DestroyWindow(wnd);
      return 0;
    case WM_DESTROY:
      if (inputFont) DeleteObject(inputFont);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

// 开发用截图：窗口开在屏幕外，画完存成 PNG
void SaveShot(HWND window, const std::wstring& path) {
  RECT client;
  GetClientRect(window, &client);
  int cw = client.right, ch = client.bottom;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC mem = CreateCompatibleDC(nullptr);
  HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  auto oldBmp = SelectObject(mem, bmp);
  PrintWindow(window, mem, PW_CLIENTONLY | 2);
  GdiFlush();
  for (int i = 0; i < cw * ch; ++i) static_cast<BYTE*>(bits)[i * 4 + 3] = 255;
  IWICImagingFactory* wic = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* enc = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&wic));
  if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
  if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
  if (SUCCEEDED(hr)) hr = frame->SetSize(UINT(cw), UINT(ch));
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
  if (SUCCEEDED(hr)) hr = frame->WritePixels(UINT(ch), UINT(cw * 4), UINT(cw * ch * 4), static_cast<BYTE*>(bits));
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = enc->Commit();
  if (frame) frame->Release();
  if (enc) enc->Release();
  if (stream) stream->Release();
  if (wic) wic->Release();
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  Gdiplus::GdiplusStartupInput startup;
  ULONG_PTR gdiplus = 0;
  if (Gdiplus::GdiplusStartup(&gdiplus, &startup, nullptr) != Gdiplus::Ok) return 1;
  {
    Gdiplus::FontFamily fluent(L"Segoe Fluent Icons");
    if (!fluent.IsAvailable()) iconFont = L"Segoe MDL2 Assets";  // Windows 10
  }
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  configPath = count > 1 ? args[1] : UserDataDir() + L"\\yyime.ini";
  if (count > 2 && wcscmp(args[2], L"--appearance") == 0) pageIndex = 1;
  std::wstring shotPath;
  if (count > 4 && wcscmp(args[2], L"--shot") == 0) {
    shotPath = args[3];
    pageIndex = _wtoi(args[4]);
    shotMode = true;
  }
  bool saveTest = count > 2 && wcscmp(args[2], L"--savetest") == 0;  // 开发用：改几项再保存，检查写出的配置
  if (saveTest) shotMode = true;
  LocalFree(args);
  if (!LoadConfig(configPath, &config)) {
    MessageBoxW(nullptr, L"无法读取配置文件。", L"yy输入法", MB_OK | MB_ICONERROR);
    return 1;
  }
  HWND existing = shotMode ? nullptr : FindWindowW(L"YYIME.Settings.1", L"yy输入法设置");
  if (existing) {
    ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
    return 0;
  }
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
  InitCommonControlsEx(&common);
  WNDCLASSW cls{};
  cls.lpfnWndProc = WndProc;
  cls.hInstance = instance;
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  cls.lpszClassName = L"YYIME.Settings.1";
  RegisterClassW(&cls);
  UINT actualDpi = GetDpiForSystem();
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
  dpi = FitDpi(actualDpi, monitor.rcWork);
  RECT frame{0, 0, S(kW), S(kH)};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  AdjustWindowRectExForDpi(&frame, style, FALSE, WS_EX_CONTROLPARENT, actualDpi);
  int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
  HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"yy输入法设置", style,
                                monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - fw) / 2,
                                monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - fh) / 2, fw, fh,
                                nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  DWORD round = 2;
  DwmSetWindowAttribute(window, 33, &round, sizeof(round));
  COLORREF caption = RGB((kBg >> 16) & 255, (kBg >> 8) & 255, kBg & 255);
  DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
  if (saveTest) {
    scheme = 1;
    pageSize = 5;
    fontSize = 18;
    vertical = true;
    fuzzyOn[0] = true;
    fuzzyOn[6] = true;
    sw[SwEnglish] = false;
    themeName = L"claude";
    keyValues[2] = L"Ctrl+Alt+Y";
    keyValues[4].clear();
    language = 3;
    int code = Save() ? 0 : 3;
    dirty = false;
    DestroyWindow(window);
    artworkCache.clear();
    Gdiplus::GdiplusShutdown(gdiplus);
    CoUninitialize();
    return code;
  }
  if (shotMode) {
    SetWindowPos(window, nullptr, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    DWORD until = GetTickCount() + 1500;
    MSG pending;
    while (GetTickCount() < until) {
      while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
      }
      Sleep(10);
    }
    SaveShot(window, shotPath);
    dirty = false;
    DestroyWindow(window);
  } else {
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      bool keyDown = msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN;
      if (recording >= 0 && keyDown) { RecordKey(UINT(msg.wParam)); continue; }
      if (recording >= 0 && (msg.message == WM_CHAR || msg.message == WM_SYSCHAR || msg.message == WM_KEYUP ||
                             msg.message == WM_SYSKEYUP))
        continue;
      if (eatAltUp && (msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) && msg.wParam == VK_MENU) {
        eatAltUp = false;
        continue;
      }
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
        SendMessageW(window, WM_CLOSE, 0, 0);
        continue;
      }
      if (!IsDialogMessageW(window, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
  artworkCache.clear();
  Gdiplus::GdiplusShutdown(gdiplus);
  CoUninitialize();
  return 0;
}
