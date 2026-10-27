// 端到端测试（不需要窗口在前台）：自己实现一个最简单的 TSF 文本框（ITextStoreACP），
// 明确把 TSF 焦点设到它上面，激活 yy输入法，通过按键管理器模拟按键，检查文本框里的字。
// 用法: tsf_host_test.exe <输出文件>
#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <olectl.h>

#include <algorithm>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/tsf/globals.h"
#include "../src/tsf/tsf_compat.h"

static std::string g_out;
static bool g_menuSeen = false;
static int g_menuTicks = 0;

static void CALLBACK MenuProbe(HWND, UINT, UINT_PTR timer, DWORD) {
  GUITHREADINFO info{sizeof(info)};
  if (GetGUIThreadInfo(GetCurrentThreadId(), &info) && (info.flags & GUI_INMENUMODE) && info.hwndMenuOwner) {
    g_menuSeen = IsWindowVisible(info.hwndMenuOwner) != FALSE;
    PostMessageW(info.hwndMenuOwner, WM_CANCELMODE, 0, 0);
    KillTimer(nullptr, timer);
  } else if (++g_menuTicks > 10) KillTimer(nullptr, timer);
}

static std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

static void Pump(DWORD ms) {
  DWORD start = GetTickCount();
  do {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(5);
  } while (GetTickCount() - start < ms);
}

class TextStore final : public ITextStoreACP {
 public:
  std::wstring text;
  LONG selStart = 0, selEnd = 0;
  HWND hwnd = nullptr;
  RECT caret{};

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACP)) {
      *ppv = static_cast<ITextStoreACP*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ULONG(++ref_); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = --ref_;
    if (r == 0) delete this;
    return ULONG(r);
  }
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD) override {
    if (!IsEqualIID(riid, IID_ITextStoreACPSink)) return E_INVALIDARG;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    return punk->QueryInterface(IID_ITextStoreACPSink, reinterpret_cast<void**>(&sink_));
  }
  STDMETHODIMP UnadviseSink(IUnknown*) override {
    if (sink_) sink_->Release();
    sink_ = nullptr;
    return S_OK;
  }
  STDMETHODIMP RequestLock(DWORD flags, HRESULT* phr) override {
    if (!sink_) return E_UNEXPECTED;
    if (lock_) {
      if (flags & TS_LF_SYNC) {
        *phr = TS_E_SYNCHRONOUS;
        return S_OK;
      }
      pending_ = flags & TS_LF_READWRITE;
      *phr = TS_S_ASYNC;
      return S_OK;
    }
    lock_ = flags & TS_LF_READWRITE;
    *phr = sink_->OnLockGranted(lock_);
    lock_ = 0;
    while (pending_) {
      DWORD f = pending_;
      pending_ = 0;
      lock_ = f;
      sink_->OnLockGranted(f);
      lock_ = 0;
    }
    return S_OK;
  }
  STDMETHODIMP GetStatus(TS_STATUS* s) override {
    s->dwDynamicFlags = 0;
    s->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
  }
  STDMETHODIMP QueryInsert(LONG a, LONG b, ULONG, LONG* ra, LONG* rb) override {
    *ra = a;
    *rb = b;
    return S_OK;
  }
  STDMETHODIMP GetSelection(ULONG, ULONG count, TS_SELECTION_ACP* sel, ULONG* fetched) override {
    if (!lock_) return TS_E_NOLOCK;
    if (count < 1) return E_INVALIDARG;
    sel[0].acpStart = selStart;
    sel[0].acpEnd = selEnd;
    sel[0].style.ase = TS_AE_END;
    sel[0].style.fInterimChar = FALSE;
    *fetched = 1;
    return S_OK;
  }
  STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* sel) override {
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) return TS_E_NOLOCK;
    if (count < 1) return E_INVALIDARG;
    selStart = Clamp(sel[0].acpStart);
    selEnd = Clamp(sel[0].acpEnd);
    return S_OK;
  }
  STDMETHODIMP GetText(LONG start, LONG end, WCHAR* buf, ULONG cchReq, ULONG* cchRet, TS_RUNINFO* runs,
                       ULONG runReq, ULONG* runRet, LONG* next) override {
    if (!lock_) return TS_E_NOLOCK;
    LONG len = LONG(text.size());
    if (end < 0 || end > len) end = len;
    if (start < 0 || start > end) return TS_E_INVALIDPOS;
    ULONG n = std::min<ULONG>(ULONG(end - start), cchReq);
    if (buf && n) memcpy(buf, text.data() + start, n * sizeof(WCHAR));
    *cchRet = n;
    if (runs && runReq) {
      runs[0].uCount = n;
      runs[0].type = TS_RT_PLAIN;
      *runRet = 1;
    } else {
      *runRet = 0;
    }
    *next = start + LONG(n);
    return S_OK;
  }
  STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR* s, ULONG cch, TS_TEXTCHANGE* change) override {
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) return TS_E_NOLOCK;
    start = Clamp(start);
    end = Clamp(end);
    text.replace(size_t(start), size_t(end - start), s, cch);
    change->acpStart = start;
    change->acpOldEnd = end;
    change->acpNewEnd = start + LONG(cch);
    selStart = selEnd = start + LONG(cch);
    return S_OK;
  }
  STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
  STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, WINBOOL* ok) override {
    *ok = FALSE;
    return S_OK;
  }
  STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
  STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* s, ULONG cch, LONG* ps, LONG* pe,
                                     TS_TEXTCHANGE* change) override {
    if ((lock_ & TS_LF_READWRITE) != TS_LF_READWRITE) return TS_E_NOLOCK;
    if (flags & TS_IAS_QUERYONLY) {
      if (ps) *ps = selStart;
      if (pe) *pe = selEnd;
      return S_OK;
    }
    LONG a = selStart, b = selEnd;
    text.replace(size_t(a), size_t(b - a), s, cch);
    if (change) {
      change->acpStart = a;
      change->acpOldEnd = b;
      change->acpNewEnd = a + LONG(cch);
    }
    if (ps) *ps = a;
    if (pe) *pe = a + LONG(cch);
    selStart = selEnd = a + LONG(cch);
    return S_OK;
  }
  STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override {
    return E_NOTIMPL;
  }
  STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override { return S_OK; }
  STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
  STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
  STDMETHODIMP FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, WINBOOL* found,
                                      LONG* off) override {
    *next = halt;
    *found = FALSE;
    *off = 0;
    return S_OK;
  }
  STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) override {
    *fetched = 0;
    return S_OK;
  }
  STDMETHODIMP GetEndACP(LONG* acp) override {
    if (!lock_) return TS_E_NOLOCK;
    *acp = LONG(text.size());
    return S_OK;
  }
  STDMETHODIMP GetActiveView(TsViewCookie* v) override {
    *v = 1;
    return S_OK;
  }
  STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
  STDMETHODIMP GetTextExt(TsViewCookie, LONG, LONG, RECT* rc, WINBOOL* clipped) override {
    if (!lock_) return TS_E_NOLOCK;
    *rc = caret;
    *clipped = FALSE;
    return S_OK;
  }
  STDMETHODIMP GetScreenExt(TsViewCookie, RECT* rc) override {
    *rc = caret;
    return S_OK;
  }
  STDMETHODIMP GetWnd(TsViewCookie, HWND* h) override {
    *h = hwnd;
    return S_OK;
  }

  // 程序自己（不是输入法）往框里打一个字，并通知 TSF
  void AppType(wchar_t ch) {
    LONG a = selStart, b = selEnd;
    text.replace(size_t(a), size_t(b - a), 1, ch);
    selStart = selEnd = a + 1;
    if (sink_) {
      TS_TEXTCHANGE c{a, b, a + 1};
      sink_->OnTextChange(0, &c);
      sink_->OnSelectionChange();
    }
  }

 private:
  LONG Clamp(LONG v) const { return std::max(0L, std::min(v, LONG(text.size()))); }
  LONG ref_ = 1;
  ITextStoreACPSink* sink_ = nullptr;
  DWORD lock_ = 0;
  DWORD pending_ = 0;
};

// 模拟全屏游戏：输入法要是把候选列表交过来，就说“我自己画”（show = FALSE）。
// yy 应该从来不交（交出去英雄联盟会再画一份），这里只数它交了几次
class UiSink final : public ITfUIElementSink {
 public:
  int begins = 0;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink)) {
      *ppv = static_cast<ITfUIElementSink*>(this);
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }  // 栈上的对象，不按引用计数释放
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP BeginUIElement(DWORD, WINBOOL* show) override {
    ++begins;
    if (show) *show = FALSE;
    return S_OK;
  }
  STDMETHODIMP UpdateUIElement(DWORD) override { return S_OK; }
  STDMETHODIMP EndUIElement(DWORD) override { return S_OK; }
};

// 本线程里 yy 的候选窗口是不是显示着
static BOOL CALLBACK FindVisibleCandidateWindow(HWND w, LPARAM p) {
  wchar_t cls[64] = {};
  GetClassNameW(w, cls, 63);
  if (wcscmp(cls, L"yyime.candidate") == 0 && IsWindowVisible(w)) *reinterpret_cast<bool*>(p) = true;
  return TRUE;
}

static bool OwnCandidateWindowVisible() {
  bool visible = false;
  EnumThreadWindows(GetCurrentThreadId(), FindVisibleCandidateWindow, reinterpret_cast<LPARAM>(&visible));
  return visible;
}

static void SendKey(ITfKeystrokeMgr* km, TextStore* store, UINT vk, wchar_t ch) {
  UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  LPARAM down = 1 | (LPARAM(sc) << 16);
  LPARAM up = down | (LPARAM(1) << 30) | (LPARAM(1) << 31);
  BOOL eaten = FALSE;
  km->TestKeyDown(vk, down, &eaten);
  if (eaten) km->KeyDown(vk, down, &eaten);
  if (!eaten && ch) store->AppType(ch);
  BOOL e2 = FALSE;
  km->TestKeyUp(vk, up, &e2);
  if (e2) km->KeyUp(vk, up, &e2);
  Pump(10);
}

static void Type(ITfKeystrokeMgr* km, TextStore* store, const std::string& script) {
  for (size_t i = 0; i < script.size(); ++i) {
    char c = script[i];
    if (c == '{') {
      size_t close = script.find('}', i);
      std::string name = script.substr(i + 1, close - i - 1);
      i = close;
      if (name == "wait") { Pump(800); continue; }
      if (name == "translate") {
        BYTE saved[256], modified[256];
        GetKeyboardState(saved); memcpy(modified, saved, sizeof(saved));
        modified[VK_CONTROL] = modified[VK_MENU] = 0x80;
        SetKeyboardState(modified);
        SendKey(km, store, 'T', 0);
        SetKeyboardState(saved);
        continue;
      }
      UINT vk = name == "sp" ? VK_SPACE : name == "cr" ? VK_RETURN : name == "bs" ? VK_BACK
              : name == "esc" ? VK_ESCAPE : name == "shift" ? VK_SHIFT : name == "pgdn" ? VK_NEXT : 0;
      wchar_t ch = name == "sp" ? L' ' : name == "cr" ? L'\r' : 0;
      SendKey(km, store, vk, ch);
      continue;
    }
    UINT vk = 0;
    if (c >= 'a' && c <= 'z') vk = UINT(c - 'a' + 'A');
    else if (c >= '0' && c <= '9') vk = UINT(c);
    else if (c == ',') vk = VK_OEM_COMMA;
    else if (c == '.') vk = VK_OEM_PERIOD;
    else if (c == ';') vk = VK_OEM_1;
    else if (c == '\'') vk = VK_OEM_7;
    SendKey(km, store, vk, wchar_t((unsigned char)c));
  }
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return 2;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  BYTE keys[256] = {};
  SetKeyboardState(keys);  // 这个线程的 Shift/Ctrl/CapsLock 全当没按，免得受真键盘影响
  char line[256];
  // 候选框会出现在这个位置：主屏幕右下角，尽量不挡东西
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT caret{work.right - 420, work.bottom - 140, work.right - 418, work.bottom - 120};
  // local：让自己的小测试窗口取得 TSF 键盘焦点。
  bool local = argc >= 3 && wcscmp(argv[2], L"local") == 0;
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"yyime-test", WS_POPUP | (local ? WS_VISIBLE : 0),
                              work.right - 4, work.bottom - 4, 2, 2, nullptr, nullptr, nullptr, nullptr);
  if (local) {
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);  // 消耗启动器可能指定的 SW_HIDE。
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
  }

  // uiless：像全屏游戏那样，只让 TSF 加载声明支持 UIElement 的输入法，候选由“游戏”自己画
  bool uiless = false;
  for (int i = 3; i < argc; ++i) uiless = uiless || wcscmp(argv[i], L"uiless") == 0;
  ITfThreadMgr* tm = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                                reinterpret_cast<void**>(&tm));
  TfClientId cid = 0;
  UiSink uiSink;
  DWORD uiSinkCookie = TF_INVALID_COOKIE;
  if (SUCCEEDED(hr) && uiless) {
    ITfThreadMgrEx* tmx = nullptr;
    hr = tm->QueryInterface(IID_ITfThreadMgrEx, reinterpret_cast<void**>(&tmx));
    if (SUCCEEDED(hr)) {
      hr = tmx->ActivateEx(&cid, TF_TMAE_UIELEMENTENABLEDONLY);
      tmx->Release();
    }
    ITfSource* src = nullptr;
    if (SUCCEEDED(hr) && SUCCEEDED(tm->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
      src->AdviseSink(IID_ITfUIElementSink, static_cast<ITfUIElementSink*>(&uiSink), &uiSinkCookie);
      src->Release();
    }
  } else if (SUCCEEDED(hr)) {
    hr = tm->Activate(&cid);
  }
  snprintf(line, sizeof(line), "TSF 线程管理器%s: 0x%08lx\n", uiless ? "（只要支持 UIElement 的输入法，候选自己画）" : "",
           hr);
  g_out += line;
  if (FAILED(hr)) return 1;

  ITfInputProcessorProfileMgr* pm = nullptr;
  CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                   reinterpret_cast<void**>(&pm));
  hr = pm ? pm->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, yy::kLangId, yy::CLSID_YYIME, yy::GUID_YYIME_PROFILE,
                                nullptr, 0x10000000 | 0x4)
          : E_FAIL;
  snprintf(line, sizeof(line), "激活 yy输入法: 0x%08lx\n", hr);
  g_out += line;
  Pump(300);
  ITfKeystrokeMgr* km = nullptr;
  tm->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&km));
  BOOL threadFocus = FALSE;
  tm->IsThreadFocus(&threadFocus);
  snprintf(line, sizeof(line), "TSF 认为本线程有焦点: %d；本线程焦点窗口=%p 前台窗口是自己的=%d\n", int(threadFocus),
           (void*)GetFocus(), int(GetForegroundWindow() == hwnd));
  g_out += line;
  int pass = 0, total = 0;

  if (uiless) {
    auto check = [&](bool ok, const std::string& what) {
      ++total;
      pass += ok;
      g_out += std::string(ok ? "  通过  " : "  不对  ") + what + "\n";
    };
    TextStore* store = new TextStore();
    store->hwnd = hwnd;
    store->caret = caret;
    ITfDocumentMgr* dm = nullptr;
    ITfContext* ctx = nullptr;
    TfEditCookie ec = 0;
    tm->CreateDocumentMgr(&dm);
    dm->CreateContext(cid, 0, static_cast<ITextStoreACP*>(store), &ctx, &ec);
    dm->Push(ctx);
    if (local) {
      ITfDocumentMgr* prev = nullptr;
      tm->AssociateFocus(hwnd, dm, &prev);
      if (prev) prev->Release();
      SetFocus(nullptr);
      SetFocus(hwnd);
    }
    tm->SetFocus(dm);
    Pump(30);
    Type(km, store, "nihao");
    Pump(100);
    check(OwnCandidateWindowVisible(), "打 nihao：yy 被加载，显示的是 yy 自己的候选框");
    check(uiSink.begins == 0, "候选没交给“游戏”（交了 " + std::to_string(uiSink.begins) + " 次）");
    Type(km, store, "{sp}");
    Pump(100);
    check(store->text == L"你好", "按空格上屏 →「" + Utf8(store->text) + "」");
    check(!OwnCandidateWindowVisible(), "上屏后候选框收起");
    Type(km, store, "zhongguo{esc}");
    Pump(100);
    check(store->text == L"你好" && uiSink.begins == 0, "Esc 取消 → 文字没变「" + Utf8(store->text) + "」，候选始终没交出去");

    tm->SetFocus(nullptr);
    dm->Pop(TF_POPF_ALL);
    ctx->Release();
    dm->Release();
    store->Release();
  }

  struct Case {
    const char* keys;
    const wchar_t* expect;
    const wchar_t* initial = L"";
  };
  std::vector<Case> cases = {
      {"nihao{sp}", L"你好"},
      {"zhongguoren{sp}", L"中国人"},
      {"nihao{cr}", L"nihao"},
      {"nihao2", L"拟好"},
      {"woaini,", L"我爱你，"},
      {"xi'an{sp}", L"西安"},
      {"zg{sp}", L"这个"},
      {"nihao{bs}{bs}{sp}", L"你好"},
      {"nihao{esc}", L""},
      {"{shift}hello{shift}", L"hello"},
      {"jintiantianqizhenhao{sp}", L"今天天气真好"},
      {"womenyiqiqukandianyingba{sp}", L"我们一起去看电影吧"},
      {"wo{sp}men{sp}", L"我们"},
      {"xian1shi{sp}", L"先是"},
      {"3.14", L"3.14"},
      {"rq2", nullptr},  // 日期，只看有没有东西
  };
  if (argc >= 4 && wcscmp(argv[3], L"ai") == 0) {
    cases.push_back({"{translate}{wait}{sp}", L"Hello, world!", L"你好世界"});
    cases.push_back({"{translate}{wait}{esc}", L""});
  }
  if (uiless) cases.clear();  // 上面已经单独测过
  for (auto& c : cases) {
    TextStore* store = new TextStore();
    store->text = c.initial;
    store->selStart = 0;
    store->selEnd = LONG(store->text.size());
    store->hwnd = hwnd;
    store->caret = caret;
    ITfDocumentMgr* dm = nullptr;
    ITfContext* ctx = nullptr;
    TfEditCookie ec = 0;
    tm->CreateDocumentMgr(&dm);
    dm->CreateContext(cid, 0, static_cast<ITextStoreACP*>(store), &ctx, &ec);
    dm->Push(ctx);
    if (local) {
      ITfDocumentMgr* prev = nullptr;
      tm->AssociateFocus(hwnd, dm, &prev);
      if (prev) prev->Release();
      SetFocus(nullptr);
      SetFocus(hwnd);
    }
    tm->SetFocus(dm);
    Pump(30);
    Type(km, store, c.keys);
    Pump(100);
    std::wstring got = store->text;
    bool ok = c.expect ? got == c.expect : !got.empty();
    ++total;
    pass += ok;
    g_out += std::string(ok ? "  通过  " : "  不对  ") + c.keys + "  →  「" + Utf8(got) + "」";
    if (!ok && c.expect) g_out += "  应该是「" + Utf8(c.expect) + "」";
    g_out += "\n";
    tm->SetFocus(nullptr);
    dm->Pop(TF_POPF_ALL);
    ctx->Release();
    dm->Release();
    store->Release();
    Pump(20);
  }
  // 通过真正的语言栏 COM 接口右键两次，检查原生菜单确实进入显示状态。
  ITfLangBarItemMgr* items = nullptr;
  ITfLangBarItem* item = nullptr;
  ITfLangBarItemButton* button = nullptr;
  if (SUCCEEDED(tm->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&items))) &&
      SUCCEEDED(items->GetItem(tsfx::GUID_LBI_INPUTMODE, &item)))
    item->QueryInterface(tsfx::IID_ITfLangBarItemButton, reinterpret_cast<void**>(&button));
  for (int attempt = 0; attempt < (uiless ? 0 : 2); ++attempt) {  // uiless 模式不测托盘菜单
    if (attempt == 1) {
      // 托盘不应依赖某个仍然存活的输入控件。
      DestroyWindow(hwnd);
      hwnd = nullptr;
    }
    g_menuSeen = false; g_menuTicks = 0;
    UINT_PTR timer = SetTimer(nullptr, 0, 50, MenuProbe);
    HRESULT click = button ? button->OnClick(TF_LBI_CLK_RIGHT, {work.right-300, work.bottom-50}, nullptr) : E_FAIL;
    Pump(600);
    KillTimer(nullptr, timer);
    bool ok = SUCCEEDED(click) && g_menuSeen;
    ++total; pass += ok;
    g_out += std::string(ok ? "  通过  " : "  不对  ") + "托盘右键显示菜单 #" + std::to_string(attempt+1) + "\n";
  }
  if (button) button->Release();
  if (item) item->Release();
  if (items) items->Release();
  snprintf(line, sizeof(line), "结果：%d / %d 通过\n", pass, total);
  g_out += line;
  if (km) km->Release();
  if (pm) pm->Release();
  if (uiSinkCookie != TF_INVALID_COOKIE) {
    ITfSource* src = nullptr;
    if (SUCCEEDED(tm->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
      src->UnadviseSink(uiSinkCookie);
      src->Release();
    }
  }
  tm->Deactivate();
  tm->Release();
  Pump(50);
  HANDLE f = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  DWORD n;
  WriteFile(f, g_out.data(), DWORD(g_out.size()), &n, nullptr);
  CloseHandle(f);
  CoUninitialize();
  return pass == total ? 0 : 1;
}
