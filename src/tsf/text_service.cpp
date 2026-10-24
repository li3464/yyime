#include "text_service.h"

#include <olectl.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>
#include <new>

#include "../ai/ai_client.h"
#include "../engine/engine.h"
#include "../engine/util.h"
#include "../ui/candidate_window.h"
#include "display_attribute.h"
#include "globals.h"
#include "langbar.h"

namespace yy {

namespace {

// 简单的 COM 指针，出作用域自动 Release
template <typename T>
class Com {
 public:
  Com() = default;
  explicit Com(T* p) : p_(p) {}
  ~Com() { reset(); }
  Com(const Com&) = delete;
  Com& operator=(const Com&) = delete;
  void reset() {
    if (p_) p_->Release();
    p_ = nullptr;
  }
  T** put() {
    reset();
    return &p_;
  }
  void** putVoid() { return reinterpret_cast<void**>(put()); }
  T* get() const { return p_; }
  T* operator->() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  T* p_ = nullptr;
};

// 把 lambda 包成编辑会话；会话存在期间拿着 TextService 的引用，防止它先被释放
class EditSession final : public ITfEditSession {
 public:
  EditSession(std::function<HRESULT(TfEditCookie)> fn, IUnknown* owner) : fn_(std::move(fn)), owner_(owner) {
    owner_->AddRef();
    DllAddRef();
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
      *ppv = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return ULONG(r);
  }
  STDMETHODIMP DoEditSession(TfEditCookie ec) override { return fn_ ? fn_(ec) : S_OK; }

 private:
  ~EditSession() {
    owner_->Release();
    DllRelease();
  }
  LONG ref_ = 1;
  std::function<HRESULT(TfEditCookie)> fn_;
  IUnknown* owner_;
};

ITfCategoryMgr* CreateCategoryMgr() {
  using Fn = HRESULT(WINAPI*)(ITfCategoryMgr**);
  if (HMODULE m = GetModuleHandleW(L"msctf.dll")) {
    if (auto fn = reinterpret_cast<Fn>(GetProcAddress(m, "TF_CreateCategoryMgr"))) {
      ITfCategoryMgr* c = nullptr;
      if (SUCCEEDED(fn(&c)) && c) return c;
    }
  }
  ITfCategoryMgr* c = nullptr;
  CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                   reinterpret_cast<void**>(&c));
  return c;
}

// 按美式键盘把虚拟键换成字符
wchar_t VkToChar(UINT vk, bool shift, bool caps) {
  if (vk >= 'A' && vk <= 'Z') return (shift != caps) ? wchar_t(vk) : wchar_t(vk + 32);
  if (vk >= '0' && vk <= '9') {
    static const wchar_t kShifted[] = L")!@#$%^&*(";
    return shift ? kShifted[vk - '0'] : wchar_t(vk);
  }
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return wchar_t(L'0' + (vk - VK_NUMPAD0));
  switch (vk) {
    case VK_SPACE: return L' ';
    case VK_OEM_1: return shift ? L':' : L';';
    case VK_OEM_PLUS: return shift ? L'+' : L'=';
    case VK_OEM_COMMA: return shift ? L'<' : L',';
    case VK_OEM_MINUS: return shift ? L'_' : L'-';
    case VK_OEM_PERIOD: return shift ? L'>' : L'.';
    case VK_OEM_2: return shift ? L'?' : L'/';
    case VK_OEM_3: return shift ? L'~' : L'`';
    case VK_OEM_4: return shift ? L'{' : L'[';
    case VK_OEM_5: return shift ? L'|' : L'\\';
    case VK_OEM_6: return shift ? L'}' : L']';
    case VK_OEM_7: return shift ? L'"' : L'\'';
    default: return 0;
  }
}

bool AppsUseDarkTheme() {
  DWORD v = 1, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) {
    return false;
  }
  return v == 0;
}

// "Ctrl+Alt+Space" 这样的写法
void ParseHotkey(const std::wstring& spec, UINT* vk, bool* ctrl, bool* alt, bool* shift) {
  *vk = 0;
  *ctrl = *alt = *shift = false;
  std::wstring s;
  for (wchar_t c : spec) s += (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(L'+', i);
    if (j == std::wstring::npos) j = s.size();
    std::wstring t = s.substr(i, j - i);
    while (!t.empty() && t.front() == L' ') t.erase(0, 1);
    while (!t.empty() && t.back() == L' ') t.pop_back();
    if (t == L"ctrl" || t == L"control") *ctrl = true;
    else if (t == L"alt") *alt = true;
    else if (t == L"shift") *shift = true;
    else if (t == L"space") *vk = VK_SPACE;
    else if (t == L"tab") *vk = VK_TAB;
    else if (t == L"enter") *vk = VK_RETURN;
    else if (t == L"`") *vk = VK_OEM_3;
    else if (t == L";") *vk = VK_OEM_1;
    else if (t == L"/") *vk = VK_OEM_2;
    else if (t == L".") *vk = VK_OEM_PERIOD;
    else if (t.size() == 1 && t[0] >= L'a' && t[0] <= L'z') *vk = UINT(t[0] - 32);
    else if (t.size() == 1 && t[0] >= L'0' && t[0] <= L'9') *vk = UINT(t[0]);
    else if (t.size() >= 2 && t[0] == L'f') {
      int n = _wtoi(t.c_str() + 1);
      if (n >= 1 && n <= 12) *vk = UINT(VK_F1 + n - 1);
    }
    i = j + 1;
  }
}

enum MenuId : UINT {
  kMenuToggle = 1,
  kMenuVertical,
  kMenuPunct,
  kMenuOpenConfig,
  kMenuOpenPhrases,
  kMenuOpenDir,
  kMenuSchemeBase = 100,
  kMenuThemeBase = 200,
};

}  // namespace

TextService::TextService() { DllAddRef(); }
TextService::~TextService() { DllRelease(); }

// ---------------------------------------------------------------- IUnknown

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) ||
      IsEqualIID(riid, tsfx::IID_ITfTextInputProcessorEx)) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (IsEqualIID(riid, tsfx::IID_ITfDisplayAttributeProvider)) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
    *ppv = static_cast<ITfCompartmentEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfThreadFocusSink)) {
    *ppv = static_cast<ITfThreadFocusSink*>(this);
  }
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }

STDMETHODIMP_(ULONG) TextService::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return ULONG(r);
}

// ---------------------------------------------------------------- 激活 / 停用

STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) { return ActivateEx(ptim, tid, 0); }

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD flags) {
  if (!ptim) return E_INVALIDARG;
  threadMgr_ = ptim;
  threadMgr_->AddRef();
  clientId_ = tid;
  activateFlags_ = flags;
  Engine::Get().EnsureInit();
  SyncConfig();
  Log(L"激活 flags=0x%lx", flags);
  if (!InitThreadMgrSinks() || !InitKeySink()) {
    Deactivate();
    return E_FAIL;
  }
  InitDisplayAttributeAtom();
  InitCompartment();
  candWnd_ = new (std::nothrow) CandidateWindow();
  if (candWnd_) {
    candWnd_->Create();
    candWnd_->onClick = [this](int i) { OnCandidateClick(i); };
    candWnd_->onWheel = [this](int d) { OnCandidateWheel(d); };
    candWnd_->onAiDone = [this](WPARAM wp, LPARAM lp) { OnAiDone(int(wp), reinterpret_cast<void*>(lp)); };
  }
  ApplyStyle();
  InitLangBar();
  return S_OK;
}

void TextService::FinishCompositionNow() {
  if (!composition_ || !compositionContext_) return;
  std::wstring raw = session_.composing() ? session_.RawText() : std::wstring();
  ITfContext* ctx = compositionContext_;
  ctx->AddRef();
  // 必须同步做完：异步的话等它执行时可能已经开始了新的组字
  RequestEdit(ctx, TF_ES_READWRITE | TF_ES_SYNC, [this, raw, ctx](TfEditCookie ec) {
    if (!composition_) return S_OK;
    if (!raw.empty()) CommitText(ec, ctx, raw);
    else EndComposition(ec, true);
    return S_OK;
  });
  ctx->Release();
  if (composition_) {  // 程序不让同步改：放弃这次组字，字留在原处
    composition_->Release();
    composition_ = nullptr;
    if (compositionContext_) {
      compositionContext_->Release();
      compositionContext_ = nullptr;
    }
  }
}

STDMETHODIMP TextService::Deactivate() {
  Log(L"停用");
  FinishCompositionNow();
  session_.Clear();
  UninitLangBar();
  UninitCompartment();
  UninitKeySink();
  UninitThreadMgrSinks();
  if (candWnd_) {
    MSG msg;
    if (candWnd_->hwnd()) {
      while (PeekMessageW(&msg, candWnd_->hwnd(), CandidateWindow::kMsgAiDone, CandidateWindow::kMsgAiDone,
                          PM_REMOVE)) {
        delete reinterpret_cast<AiResponse*>(msg.lParam);
      }
    }
    candWnd_->Destroy();
    delete candWnd_;
    candWnd_ = nullptr;
  }
  if (composition_) {
    composition_->Release();
    composition_ = nullptr;
  }
  if (compositionContext_) {
    compositionContext_->Release();
    compositionContext_ = nullptr;
  }
  if (lastContext_) {
    lastContext_->Release();
    lastContext_ = nullptr;
  }
  if (threadMgr_) {
    threadMgr_->Release();
    threadMgr_ = nullptr;
  }
  clientId_ = TF_CLIENTID_NULL;
  return S_OK;
}

bool TextService::InitThreadMgrSinks() {
  Com<ITfSource> src;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfSource, src.putVoid()))) return false;
  if (FAILED(src->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
                             &threadMgrSinkCookie_))) {
    threadMgrSinkCookie_ = TF_INVALID_COOKIE;
    return false;
  }
  if (FAILED(src->AdviseSink(IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this),
                             &threadFocusSinkCookie_))) {
    threadFocusSinkCookie_ = TF_INVALID_COOKIE;
  }
  return true;
}

void TextService::UninitThreadMgrSinks() {
  if (!threadMgr_) return;
  Com<ITfSource> src;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfSource, src.putVoid()))) return;
  if (threadMgrSinkCookie_ != TF_INVALID_COOKIE) src->UnadviseSink(threadMgrSinkCookie_);
  if (threadFocusSinkCookie_ != TF_INVALID_COOKIE) src->UnadviseSink(threadFocusSinkCookie_);
  threadMgrSinkCookie_ = threadFocusSinkCookie_ = TF_INVALID_COOKIE;
}

bool TextService::InitKeySink() {
  Com<ITfKeystrokeMgr> km;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, km.putVoid()))) return false;
  keySinkAdvised_ = SUCCEEDED(km->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE));
  if (keySinkAdvised_) PreserveShiftKey(true);
  return keySinkAdvised_;
}

void TextService::UninitKeySink() {
  if (!threadMgr_ || !keySinkAdvised_) return;
  PreserveShiftKey(false);
  Com<ITfKeystrokeMgr> km;
  if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, km.putVoid()))) km->UnadviseKeyEventSink(clientId_);
  keySinkAdvised_ = false;
}

// 有些程序不把“松开 Shift”交给输入法的按键处理，但登记过的热键一定会送到（微软示例也是这么做的）
void TextService::PreserveShiftKey(bool on) {
  if (!threadMgr_ || on == shiftPreserved_) return;
  Com<ITfKeystrokeMgr> km;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, km.putVoid()))) return;
  TF_PRESERVEDKEY key{};
  key.uVKey = VK_SHIFT;
  key.uModifiers = TF_MOD_ON_KEYUP;
  if (on) {
    static const wchar_t kDesc[] = L"中/英切换";
    shiftPreserved_ = SUCCEEDED(km->PreserveKey(clientId_, GUID_YYIME_KEY_SHIFT, &key, kDesc, ULONG(wcslen(kDesc))));
    Log(L"登记 Shift 热键：%d", int(shiftPreserved_));
  } else {
    km->UnpreserveKey(GUID_YYIME_KEY_SHIFT, &key);
    shiftPreserved_ = false;
  }
}

bool TextService::TakeShiftTap() {
  bool tap = shiftAlone_ && GetTickCount() - shiftDownAt_ < 600;
  shiftAlone_ = false;
  return tap;
}

void TextService::InitDisplayAttributeAtom() {
  Com<ITfCategoryMgr> cat(CreateCategoryMgr());
  if (cat) cat->RegisterGUID(GUID_YYIME_ATTR_INPUT, &inputAtom_);
}

void TextService::InitCompartment() {
  Com<ITfCompartmentMgr> cm;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfCompartmentMgr, cm.putVoid()))) return;
  Com<ITfCompartment> c;
  if (FAILED(cm->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, c.put()))) return;
  bool start = !cfg_ || cfg_->startChinese;
  session_.SetChinese(start);
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = start ? 1 : 0;
  c->SetValue(clientId_, &v);
  Com<ITfSource> src;
  if (SUCCEEDED(c->QueryInterface(IID_ITfSource, src.putVoid()))) {
    if (FAILED(src->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this),
                               &openCloseSinkCookie_))) {
      openCloseSinkCookie_ = TF_INVALID_COOKIE;
    }
  }
}

void TextService::UninitCompartment() {
  if (!threadMgr_ || openCloseSinkCookie_ == TF_INVALID_COOKIE) return;
  Com<ITfCompartmentMgr> cm;
  Com<ITfCompartment> c;
  Com<ITfSource> src;
  if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfCompartmentMgr, cm.putVoid())) &&
      SUCCEEDED(cm->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, c.put())) &&
      SUCCEEDED(c->QueryInterface(IID_ITfSource, src.putVoid()))) {
    src->UnadviseSink(openCloseSinkCookie_);
  }
  openCloseSinkCookie_ = TF_INVALID_COOKIE;
}

void TextService::InitLangBar() {
  Com<ITfLangBarItemMgr> lbm;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, lbm.putVoid()))) return;
  langBar_ = new (std::nothrow) LangBarButton(this);
  if (langBar_ && FAILED(lbm->AddItem(langBar_))) {
    langBar_->Detach();
    langBar_->Release();
    langBar_ = nullptr;
  }
}

void TextService::UninitLangBar() {
  if (!langBar_) return;
  Com<ITfLangBarItemMgr> lbm;
  if (threadMgr_ && SUCCEEDED(threadMgr_->QueryInterface(IID_ITfLangBarItemMgr, lbm.putVoid()))) {
    lbm->RemoveItem(langBar_);
  }
  langBar_->Detach();
  langBar_->Release();
  langBar_ = nullptr;
}

// ---------------------------------------------------------------- 设置

void TextService::SyncConfig() {
  auto cfg = Engine::Get().config();
  DWORD now = GetTickCount();
  bool styleDirty = false;
  if (!cfg_ || now - darkCheckedAt_ > 3000) {
    darkCheckedAt_ = now;
    bool dark = AppsUseDarkTheme();
    styleDirty = dark != systemDark_;
    systemDark_ = dark;
  }
  if (cfg != cfg_) {
    cfg_ = cfg;
    session_.ApplyConfig(*cfg_);
    ParseHotkey(cfg_->aiHotkey, &hotkeyVk_, &hotkeyCtrl_, &hotkeyAlt_, &hotkeyShift_);
    styleDirty = true;
  }
  if (styleDirty) ApplyStyle();
}

void TextService::ApplyStyle() {
  if (candWnd_ && cfg_) candWnd_->SetStyle(cfg_->ResolveTheme(systemDark_), cfg_->fontFace, cfg_->fontSize);
}

// ---------------------------------------------------------------- 文档焦点

STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnPushContext(ITfContext*) { return S_OK; }
STDMETHODIMP TextService::OnPopContext(ITfContext*) { return S_OK; }

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* prevFocus) {
  Log(L"文档焦点 %p → %p", prevFocus, focus);
  // 焦点换到别的输入框：没打完的拼音原样留在原来那个框里，候选框收起来
  if (compositionContext_) {
    Com<ITfDocumentMgr> dm;
    bool same = SUCCEEDED(compositionContext_->GetDocumentMgr(dm.put())) && dm.get() == focus;
    if (!same) {
      FinishCompositionNow();
      session_.Clear();
    }
  } else if (session_.aiPanel()) {
    session_.Clear();
  }
  if (candWnd_ && !session_.panelVisible()) candWnd_->Hide();
  return S_OK;
}

STDMETHODIMP TextService::OnSetThreadFocus() {
  Log(L"线程得到焦点");
  threadFocused_ = true;
  if (session_.panelVisible()) RefreshCandidateWindow();
  return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
  Log(L"线程失去焦点");
  threadFocused_ = false;
  if (candWnd_) candWnd_->Hide();
  return S_OK;
}

// ---------------------------------------------------------------- 按键

KeyEvent TextService::MakeKeyEvent(WPARAM wParam) const {
  KeyEvent k;
  k.vk = UINT(wParam);
  k.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  k.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  k.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  k.caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
  k.ch = VkToChar(k.vk, k.shift, k.caps);
  return k;
}

bool TextService::IsKeyboardDisabled(ITfContext* pic) const {
  if (!pic) return true;
  Com<ITfCompartmentMgr> cm;
  if (FAILED(pic->QueryInterface(IID_ITfCompartmentMgr, cm.putVoid()))) return false;
  for (const GUID* g : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
    Com<ITfCompartment> c;
    if (FAILED(cm->GetCompartment(*g, c.put()))) continue;
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4 && v.lVal != 0) return true;
  }
  return false;
}

bool TextService::MatchHotkey(const KeyEvent& k) const {
  return hotkeyVk_ != 0 && k.vk == hotkeyVk_ && k.ctrl == hotkeyCtrl_ && k.alt == hotkeyAlt_ &&
         k.shift == hotkeyShift_ && cfg_ && cfg_->aiEnabled;
}

STDMETHODIMP TextService::OnSetFocus(BOOL) { return S_OK; }

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (wParam == VK_SHIFT) {
    // 第 30 位是“之前就按着”，按住不放时的自动重复不算新的一次
    if (!(lParam & (LPARAM(1) << 30))) {
      shiftAlone_ = true;
      shiftDownAt_ = GetTickCount();
    }
  } else {
    shiftAlone_ = false;
  }
  bool disabled = IsKeyboardDisabled(pic);
  if (!disabled) {
    SyncConfig();
    KeyEvent k = MakeKeyEvent(wParam);
    *eaten = (MatchHotkey(k) && !session_.composing()) || session_.TestKeyDown(k);
  }
  Log(L"TestKeyDown vk=0x%02x pic=%p 禁用=%d 中文=%d 打字中=%d 吃掉=%d", unsigned(wParam), pic, int(disabled),
      int(session_.chinese()), int(session_.composing()), int(*eaten));
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (wParam != VK_SHIFT) {
    shiftAlone_ = false;
  } else if (!(lParam & (LPARAM(1) << 30)) && !shiftAlone_) {
    shiftAlone_ = true;
    shiftDownAt_ = GetTickCount();
  }
  if (IsKeyboardDisabled(pic)) return S_OK;
  if (pic != lastContext_) {
    if (lastContext_) lastContext_->Release();
    lastContext_ = pic;
    lastContext_->AddRef();
  }
  KeyEvent k = MakeKeyEvent(wParam);
  if (MatchHotkey(k) && !session_.composing()) {
    *eaten = TRUE;
    StartAiPanel(pic);
    return S_OK;
  }
  if (!session_.TestKeyDown(k)) return S_OK;
  SessionResult r = session_.OnKeyDown(k);
  *eaten = r.eaten ? TRUE : FALSE;
  Log(L"KeyDown vk=0x%02x 吃掉=%d 上屏=[%ls] 预编辑=[%ls]", unsigned(wParam), int(*eaten), r.commit.c_str(),
      session_.Preedit().c_str());
  HandleResult(pic, r);
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (wParam != VK_SHIFT) return S_OK;
  // 能收到松开 Shift 的程序在这里切；收不到的靠下面登记的热键
  if (TakeShiftTap() && pic && !IsKeyboardDisabled(pic)) {
    Log(L"Shift 单按（按键通知）");
    SessionResult r = session_.OnShiftTap();
    HandleResult(pic, r);
  }
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) {
  return OnTestKeyUp(pic, wParam, lParam, eaten);
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (!IsEqualGUID(rguid, GUID_YYIME_KEY_SHIFT)) return S_OK;
  ITfContext* ctx = pic ? pic : (compositionContext_ ? compositionContext_ : lastContext_);
  if (TakeShiftTap() && !IsKeyboardDisabled(ctx)) {
    Log(L"Shift 单按（热键）");
    SessionResult r = session_.OnShiftTap();
    HandleResult(ctx, r);
    *eaten = r.modeChanged ? TRUE : FALSE;
  }
  return S_OK;
}

void TextService::HandleResult(ITfContext* pic, const SessionResult& r) {
  if (r.modeChanged) {
    SetOpenClose(session_.chinese());
    OnModeChanged();
  }
  if (r.aiRequested) {
    StartAiCompose(pic);
    return;
  }
  if (r.commit.empty() && !r.changed) return;
  if (!pic) {
    RefreshCandidateWindow();
    return;
  }
  std::wstring commit = r.commit;
  HRESULT hr = RequestEdit(pic, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, [this, commit, pic](TfEditCookie ec) {
    if (!commit.empty()) CommitText(ec, pic, commit);
    if (session_.composing()) UpdateComposition(ec, pic);
    else if (composition_) EndComposition(ec, true);
    UpdateCandidateWindow(ec, pic);
    Log(L"编辑会话完成 组字=%p", composition_);
    return S_OK;
  });
  if (FAILED(hr)) Log(L"编辑会话请求失败 hr=0x%08lx", hr);
}

// ---------------------------------------------------------------- 编辑文档

HRESULT TextService::RequestEdit(ITfContext* pic, DWORD flags, std::function<HRESULT(TfEditCookie)> fn) {
  if (!pic || clientId_ == TF_CLIENTID_NULL) return E_FAIL;
  auto* es = new (std::nothrow) EditSession(std::move(fn), static_cast<ITfTextInputProcessorEx*>(this));
  if (!es) return E_OUTOFMEMORY;
  HRESULT hrSession = E_FAIL;
  HRESULT hr = pic->RequestEditSession(clientId_, es, flags, &hrSession);
  es->Release();
  return FAILED(hr) ? hr : hrSession;
}

void TextService::StartComposition(TfEditCookie ec, ITfContext* pic) {
  Com<ITfInsertAtSelection> ias;
  if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, ias.putVoid()))) return;
  Com<ITfRange> range;
  if (FAILED(ias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, range.put())) || !range) return;
  Com<ITfContextComposition> cc;
  if (FAILED(pic->QueryInterface(IID_ITfContextComposition, cc.putVoid()))) return;
  ITfComposition* comp = nullptr;
  HRESULT hr = cc->StartComposition(ec, range.get(), static_cast<ITfCompositionSink*>(this), &comp);
  Log(L"开始组字 hr=0x%08lx comp=%p", hr, comp);
  if (SUCCEEDED(hr) && comp) {
    composition_ = comp;
    if (compositionContext_) compositionContext_->Release();
    compositionContext_ = pic;
    compositionContext_->AddRef();
    TF_SELECTION sel{};
    sel.range = range.get();
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    pic->SetSelection(ec, 1, &sel);
  }
}

void TextService::SetInputAttribute(TfEditCookie ec, ITfContext* pic, ITfRange* range, bool on) {
  Com<ITfProperty> prop;
  if (FAILED(pic->GetProperty(GUID_PROP_ATTRIBUTE, prop.put()))) return;
  if (on && inputAtom_ != TF_INVALID_GUIDATOM) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = LONG(inputAtom_);
    prop->SetValue(ec, range, &v);
  } else {
    prop->Clear(ec, range);
  }
}

static void MoveCaretToEnd(TfEditCookie ec, ITfContext* pic, ITfRange* range) {
  ITfRange* end = nullptr;
  if (FAILED(range->Clone(&end)) || !end) return;
  end->Collapse(ec, TF_ANCHOR_END);
  TF_SELECTION sel{};
  sel.range = end;
  sel.style.ase = TF_AE_NONE;
  sel.style.fInterimChar = FALSE;
  pic->SetSelection(ec, 1, &sel);
  end->Release();
}

void TextService::UpdateComposition(TfEditCookie ec, ITfContext* pic) {
  std::wstring text = session_.Preedit();
  if (!composition_) StartComposition(ec, pic);
  if (!composition_) return;
  Com<ITfRange> range;
  if (FAILED(composition_->GetRange(range.put())) || !range) return;
  range->SetText(ec, 0, text.c_str(), LONG(text.size()));
  SetInputAttribute(ec, pic, range.get(), true);
  MoveCaretToEnd(ec, pic, range.get());
}

void TextService::CommitText(TfEditCookie ec, ITfContext* pic, const std::wstring& text) {
  if (!composition_) StartComposition(ec, pic);
  if (!composition_) {
    // 这个程序不让开组字，那就直接插到光标处
    Com<ITfInsertAtSelection> ias;
    Com<ITfRange> range;
    if (SUCCEEDED(pic->QueryInterface(IID_ITfInsertAtSelection, ias.putVoid())) &&
        SUCCEEDED(ias->InsertTextAtSelection(ec, 0, text.c_str(), LONG(text.size()), range.put())) && range) {
      MoveCaretToEnd(ec, pic, range.get());
    }
    return;
  }
  Com<ITfRange> range;
  if (SUCCEEDED(composition_->GetRange(range.put())) && range) {
    range->SetText(ec, 0, text.c_str(), LONG(text.size()));
    SetInputAttribute(ec, pic, range.get(), false);
    MoveCaretToEnd(ec, pic, range.get());
  }
  composition_->EndComposition(ec);
  composition_->Release();
  composition_ = nullptr;
  if (compositionContext_) {
    compositionContext_->Release();
    compositionContext_ = nullptr;
  }
}

void TextService::EndComposition(TfEditCookie ec, bool clearText) {
  if (!composition_) return;
  if (clearText) {
    Com<ITfRange> range;
    if (SUCCEEDED(composition_->GetRange(range.put())) && range) range->SetText(ec, 0, L"", 0);
  }
  composition_->EndComposition(ec);
  composition_->Release();
  composition_ = nullptr;
  if (compositionContext_) {
    compositionContext_->Release();
    compositionContext_ = nullptr;
  }
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) {
  Log(L"程序结束了组字 comp=%p 我们的=%p", composition, composition_);
  // 程序那边强行结束了组字（比如鼠标点到别处）：拼音原样留下，不带分隔符
  if (composition_ && (composition == composition_ || !composition)) {
    std::wstring raw = session_.composing() ? session_.RawText() : std::wstring();
    Com<ITfRange> range;
    if (!raw.empty() && SUCCEEDED(composition_->GetRange(range.put())) && range) {
      range->SetText(ec, 0, raw.c_str(), LONG(raw.size()));
      if (compositionContext_) SetInputAttribute(ec, compositionContext_, range.get(), false);
    }
    composition_->Release();
    composition_ = nullptr;
    if (compositionContext_) {
      compositionContext_->Release();
      compositionContext_ = nullptr;
    }
  }
  session_.Clear();
  if (candWnd_) candWnd_->Hide();
  return S_OK;
}

// ---------------------------------------------------------------- 候选框

void TextService::UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic) {
  if (!candWnd_) return;
  if (!session_.panelVisible() || !threadFocused_) {
    candWnd_->Hide();
    return;
  }
  RECT rc{};
  bool got = false;
  Com<ITfContextView> view;
  if (SUCCEEDED(pic->GetActiveView(view.put())) && view) {
    HWND w = nullptr;
    if (SUCCEEDED(view->GetWnd(&w)) && w) contextWnd_ = w;
    Com<ITfRange> range;
    if (composition_) {
      composition_->GetRange(range.put());
    } else {
      TF_SELECTION sel{};
      ULONG n = 0;
      if (SUCCEEDED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) && n == 1) *range.put() = sel.range;
    }
    if (range) {
      BOOL clipped = FALSE;
      if (SUCCEEDED(view->GetTextExt(ec, range.get(), &rc, &clipped)) &&
          (rc.left || rc.top || rc.right || rc.bottom)) {
        got = true;
      }
    }
  }
  if (!got) {
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
      rc = gti.rcCaret;
      MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
      got = true;
    }
  }
  if (got) lastCaret_ = rc;
  RefreshCandidateWindow();
}

void TextService::RefreshCandidateWindow() {
  if (!candWnd_ || !cfg_) return;
  if (!session_.panelVisible() || !threadFocused_) {
    candWnd_->Hide();
    return;
  }
  CandidateView v;
  InputSession::View pv = session_.PageView();
  if (cfg_->preeditInWindow || session_.aiPanel()) v.aux = session_.AuxText();
  v.status = pv.status;
  for (const Candidate* c : pv.items) v.items.push_back(CandidateItem{c->text, c->comment, c->kind == CandKind::Ai});
  v.highlight = pv.highlight;
  v.hasPrev = pv.hasPrev;
  v.hasNext = pv.hasNext;
  v.vertical = cfg_->vertical;
  if (v.items.empty() && v.aux.empty() && v.status.empty()) {
    candWnd_->Hide();
    return;
  }
  candWnd_->Show(v, lastCaret_);
}

void TextService::OnCandidateClick(int indexOnPage) {
  ITfContext* pic = compositionContext_ ? compositionContext_ : lastContext_;
  SessionResult r = session_.ClickCandidate(indexOnPage);
  HandleResult(pic, r);
}

void TextService::OnCandidateWheel(int delta) {
  if (!session_.composing()) return;
  KeyEvent k;
  k.vk = delta > 0 ? VK_NEXT : VK_PRIOR;
  session_.OnKeyDown(k);
  RefreshCandidateWindow();
}

// ---------------------------------------------------------------- 中英状态

bool TextService::GetOpenClose() const {
  if (!threadMgr_) return true;
  Com<ITfCompartmentMgr> cm;
  Com<ITfCompartment> c;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfCompartmentMgr, cm.putVoid())) ||
      FAILED(cm->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, c.put()))) {
    return true;
  }
  VARIANT v;
  VariantInit(&v);
  if (SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4) return v.lVal != 0;
  return true;
}

void TextService::SetOpenClose(bool open) {
  if (!threadMgr_) return;
  Com<ITfCompartmentMgr> cm;
  Com<ITfCompartment> c;
  if (FAILED(threadMgr_->QueryInterface(IID_ITfCompartmentMgr, cm.putVoid())) ||
      FAILED(cm->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, c.put()))) {
    return;
  }
  VARIANT v;
  VariantInit(&v);
  v.vt = VT_I4;
  v.lVal = open ? 1 : 0;
  c->SetValue(clientId_, &v);
}

STDMETHODIMP TextService::OnChange(REFGUID rguid) {
  if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
    bool open = GetOpenClose();
    Log(L"输入法开关变了 → %d（现在中文=%d）", int(open), int(session_.chinese()));
    if (open != session_.chinese()) {
      // 别人（比如系统的输入指示器）切换了中英
      SessionResult r = session_.ToggleChinese();
      r.modeChanged = false;
      HandleResult(compositionContext_ ? compositionContext_ : lastContext_, r);
      OnModeChanged();
    }
  }
  return S_OK;
}

void TextService::OnModeChanged() {
  if (langBar_) langBar_->Update();
  if (candWnd_ && !session_.panelVisible()) candWnd_->Hide();
}

void TextService::ToggleChinese() {
  SessionResult r = session_.ToggleChinese();
  HandleResult(compositionContext_ ? compositionContext_ : lastContext_, r);
}

// ---------------------------------------------------------------- 显示属性

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
  if (!ppEnum) return E_INVALIDARG;
  *ppEnum = new (std::nothrow) yy::EnumDisplayAttributeInfo();
  return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
  if (!ppInfo) return E_INVALIDARG;
  *ppInfo = nullptr;
  if (!IsEqualGUID(guid, GUID_YYIME_ATTR_INPUT)) return E_INVALIDARG;
  *ppInfo = new (std::nothrow) DisplayAttributeInfo();
  return *ppInfo ? S_OK : E_OUTOFMEMORY;
}

// ---------------------------------------------------------------- AI

static AiRequest BaseAiRequest(const Config& cfg) {
  AiRequest req;
  req.baseUrl = cfg.aiBaseUrl;
  req.apiKey = cfg.aiKey;
  req.model = cfg.aiModel;
  req.proxy = cfg.aiProxy;
  req.timeoutMs = cfg.aiTimeoutMs;
  return req;
}

static const wchar_t kNoKey[] = L"AI：还没填 API key（右键任务栏的 中/英 → 打开设置文件）";

void TextService::StartAiCompose(ITfContext* pic) {
  if (!cfg_ || !candWnd_) return;
  int id = session_.BeginAiCompose();
  if (cfg_->aiKey.empty()) {
    session_.SetAiResult(id, {}, kNoKey);
    RefreshCandidateWindow();
    return;
  }
  RefreshCandidateWindow();
  AiRequest req = BaseAiRequest(*cfg_);
  req.mode = AiMode::Compose;
  req.pinyin = session_.AiPinyin();
  req.guess = session_.AiBestGuess();
  HWND notify = candWnd_->hwnd();
  HRESULT hr = RequestEdit(pic, TF_ES_READ | TF_ES_ASYNCDONTCARE, [this, req, id, pic, notify](TfEditCookie ec) mutable {
    req.context = ReadTextBefore(ec, pic, 80);
    StartAiRequest(req, notify, CandidateWindow::kMsgAiDone, id);
    return S_OK;
  });
  if (FAILED(hr)) StartAiRequest(req, notify, CandidateWindow::kMsgAiDone, id);
}

void TextService::StartAiPanel(ITfContext* pic) {
  if (!cfg_ || !candWnd_ || !pic) return;
  HWND notify = candWnd_->hwnd();
  RequestEdit(pic, TF_ES_READ | TF_ES_ASYNCDONTCARE, [this, pic, notify](TfEditCookie ec) {
    std::wstring sel = ReadSelection(ec, pic);
    AiMode mode = sel.empty() ? AiMode::Continue : AiMode::Polish;
    int id = session_.BeginAiPanel(mode);
    std::wstring ctx = ReadTextBefore(ec, pic, 200);
    if (cfg_->aiKey.empty()) {
      session_.SetAiResult(id, {}, kNoKey);
    } else if (mode == AiMode::Continue && ctx.empty()) {
      session_.SetAiResult(id, {}, L"AI：这里读不到前文，先选中一段文字再按");
    } else {
      AiRequest req = BaseAiRequest(*cfg_);
      req.mode = mode;
      req.selection = sel;
      req.context = ctx;
      StartAiRequest(req, notify, CandidateWindow::kMsgAiDone, id);
    }
    UpdateCandidateWindow(ec, pic);
    return S_OK;
  });
}

void TextService::OnAiDone(int requestId, void* response) {
  std::unique_ptr<AiResponse> resp(static_cast<AiResponse*>(response));
  if (!resp) return;
  std::wstring status;
  if (!resp->error.empty()) status = L"AI：" + resp->error;
  else if (resp->texts.empty()) status = L"AI：没给出结果，再按一次试试";
  else if (session_.aiPanel()) status = L"按数字选择，Esc 取消";
  session_.SetAiResult(requestId, resp->texts, status);
  RefreshCandidateWindow();
}

std::wstring TextService::ReadTextBefore(TfEditCookie ec, ITfContext* pic, int maxChars) {
  Com<ITfRange> start;
  if (composition_) {
    Com<ITfRange> r;
    if (FAILED(composition_->GetRange(r.put())) || !r || FAILED(r->Clone(start.put()))) return {};
  } else {
    TF_SELECTION sel{};
    ULONG n = 0;
    if (FAILED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) || n != 1) return {};
    *start.put() = sel.range;
  }
  start->Collapse(ec, TF_ANCHOR_START);
  LONG moved = 0;
  start->ShiftStart(ec, -maxChars, &moved, nullptr);
  std::wstring buf(size_t(maxChars) + 1, L'\0');
  ULONG got = 0;
  if (FAILED(start->GetText(ec, 0, buf.data(), ULONG(maxChars), &got))) return {};
  buf.resize(got);
  return buf;
}

std::wstring TextService::ReadSelection(TfEditCookie ec, ITfContext* pic) {
  TF_SELECTION sel{};
  ULONG n = 0;
  if (FAILED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) || n != 1) return {};
  Com<ITfRange> range;
  *range.put() = sel.range;
  std::wstring buf(2001, L'\0');
  ULONG got = 0;
  if (FAILED(range->GetText(ec, 0, buf.data(), 2000, &got))) return {};
  buf.resize(got);
  return buf;
}

// ---------------------------------------------------------------- 右键菜单

void TextService::ShowMenu(POINT pt) {
  SyncConfig();
  if (!cfg_) return;
  HMENU menu = CreatePopupMenu();
  HMENU themes = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | (session_.chinese() ? MF_CHECKED : 0), kMenuToggle, L"中文输入\tShift");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  for (Scheme s : {Scheme::Pinyin, Scheme::Xiaohe, Scheme::Ziranma, Scheme::Mspy, Scheme::Wubi86}) {
    AppendMenuW(menu, MF_STRING | (cfg_->scheme == s ? MF_CHECKED : 0), kMenuSchemeBase + UINT(s), SchemeLabel(s));
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(themes, MF_STRING | (cfg_->themeName == L"auto" ? MF_CHECKED : 0), kMenuThemeBase, L"跟随系统深浅色");
  for (size_t i = 0; i < cfg_->themes.size(); ++i) {
    const Theme& t = cfg_->themes[i];
    AppendMenuW(themes, MF_STRING | (cfg_->themeName == t.name ? MF_CHECKED : 0), kMenuThemeBase + 1 + UINT(i),
                t.label.c_str());
  }
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themes), L"皮肤");
  AppendMenuW(menu, MF_STRING | (cfg_->vertical ? MF_CHECKED : 0), kMenuVertical, L"竖排候选");
  AppendMenuW(menu, MF_STRING | (cfg_->fullwidthPunct ? MF_CHECKED : 0), kMenuPunct, L"中文标点");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuOpenConfig, L"打开设置文件…");
  AppendMenuW(menu, MF_STRING, kMenuOpenPhrases, L"编辑自定义短语…");
  AppendMenuW(menu, MF_STRING, kMenuOpenDir, L"打开用户文件夹");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"yy输入法 1.0");
  HWND owner = contextWnd_ ? contextWnd_ : (candWnd_ ? candWnd_->hwnd() : nullptr);
  UINT cmd = UINT(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                                   pt.x, pt.y, owner, nullptr));
  DestroyMenu(menu);
  if (cmd) OnMenuCommand(cmd);
}

void TextService::OnMenuCommand(UINT id) {
  Engine& eng = Engine::Get();
  const std::wstring& path = eng.configPath();
  auto save = [&](const char* section, const char* key, const std::string& value) {
    if (path.empty()) return;
    SaveConfigValue(path, section, key, value);
    eng.ReloadConfig();
    SyncConfig();
    RefreshCandidateWindow();
  };
  auto open = [](const std::wstring& file) {
    std::wstring arg = L"\"" + file + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
  };
  if (id == kMenuToggle) {
    ToggleChinese();
  } else if (id >= kMenuSchemeBase && id < kMenuSchemeBase + 5) {
    save("general", "scheme", WideToUtf8(SchemeName(Scheme(id - kMenuSchemeBase))));
  } else if (id == kMenuThemeBase) {
    save("ui", "theme", "auto");
  } else if (id > kMenuThemeBase && cfg_ && id - kMenuThemeBase - 1 < cfg_->themes.size()) {
    save("ui", "theme", WideToUtf8(cfg_->themes[id - kMenuThemeBase - 1].name));
  } else if (id == kMenuVertical && cfg_) {
    save("ui", "layout", cfg_->vertical ? "horizontal" : "vertical");
  } else if (id == kMenuPunct && cfg_) {
    save("general", "fullwidth_punct", cfg_->fullwidthPunct ? "false" : "true");
  } else if (id == kMenuOpenConfig) {
    open(path);
  } else if (id == kMenuOpenPhrases) {
    open(eng.userDir() + L"\\custom_phrases.txt");
  } else if (id == kMenuOpenDir) {
    ShellExecuteW(nullptr, L"open", eng.userDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}

}  // namespace yy
