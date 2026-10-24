#include "langbar.h"

#include <olectl.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "globals.h"
#include "text_service.h"
#include "../engine/util.h"

namespace yy {

namespace {

bool TaskbarIsLight() {
  DWORD v = 0, size = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) {
    return false;
  }
  return v != 0;
}

int SystemIconSize() {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  using SetCtx = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
  using SysDpi = UINT(WINAPI*)();
  auto set = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
  auto get = reinterpret_cast<SysDpi>(GetProcAddress(user32, "GetDpiForSystem"));
  UINT dpi = 96;
  if (set && get) {
    DPI_AWARENESS_CONTEXT old = set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpi = get();
    if (old) set(old);
  }
  return std::max(16, MulDiv(16, int(dpi), 96));
}

HICON MakeTextIcon(const wchar_t* text, COLORREF color, int size) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = CreateCompatibleDC(nullptr);
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) {
    DeleteDC(dc);
    return nullptr;
  }
  HGDIOBJ oldBmp = SelectObject(dc, bmp);
  memset(bits, 0, size_t(size) * size_t(size) * 4);
  HFONT font = CreateFontW(-MulDiv(size, 7, 8), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                           L"Microsoft YaHei UI");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetTextColor(dc, RGB(255, 255, 255));
  SetBkMode(dc, TRANSPARENT);
  RECT rc{0, 0, size, size};
  DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  GdiFlush();
  // 白字黑底 → 用亮度当透明度，再填上想要的颜色
  auto* px = static_cast<uint32_t*>(bits);
  for (int i = 0; i < size * size; ++i) {
    uint32_t a = px[i] & 0xFF;
    px[i] = (a << 24) | (uint32_t(GetRValue(color)) << 16) | (uint32_t(GetGValue(color)) << 8) | GetBValue(color);
  }
  SelectObject(dc, oldFont);
  DeleteObject(font);
  SelectObject(dc, oldBmp);
  DeleteDC(dc);
  std::vector<BYTE> maskBits(size_t((size + 15) / 16 * 2) * size_t(size), 0);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
  ICONINFO ii{TRUE, 0, 0, mask, bmp};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(bmp);
  return icon;
}

}  // namespace

HICON MakeModeIcon(bool chinese) {
  COLORREF color = TaskbarIsLight() ? RGB(0x1F, 0x1F, 0x1F) : RGB(0xFF, 0xFF, 0xFF);
  return MakeTextIcon(chinese ? L"中" : L"英", color, SystemIconSize());
}

LangBarButton::LangBarButton(TextService* owner) : owner_(owner) { DllAddRef(); }

LangBarButton::~LangBarButton() {
  Detach();
  if (sink_) sink_->Release();
  DllRelease();
}

void LangBarButton::Detach() {
  owner_ = nullptr;
  if (menuWnd_) {
    // 停用可能发生在菜单的嵌套消息循环里。
    PostMessageW(menuWnd_, WM_CANCELMODE, 0, 0);
    SetWindowLongPtrW(menuWnd_, GWLP_USERDATA, 0);
    if (menuWndProc_) SetWindowLongPtrW(menuWnd_, GWLP_WNDPROC, LONG_PTR(menuWndProc_));
    DestroyWindow(menuWnd_);
    menuWnd_ = nullptr;
  }
}

LRESULT CALLBACK LangBarButton::MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<LangBarButton*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_APP + 0x42 && self) {
    if (!self->owner_ || self->menuOpen_) return 0;
    self->AddRef();
    auto* owner = self->owner_;
    owner->AddRef();
    self->menuOpen_ = true;
    POINT pt{LONG(INT_PTR(wp)), LONG(INT_PTR(lp))};
    SetWindowPos(hwnd, nullptr, pt.x, pt.y, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
    // 使用可激活的独立窗口，不能使用 NOACTIVATE 的候选框或失焦的输入控件。
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    owner->ShowMenu(pt, hwnd);
    if (IsWindow(hwnd)) ShowWindow(hwnd, SW_HIDE);
    self->menuOpen_ = false;
    owner->Release();
    self->Release();
    return 0;
  }
  return self && self->menuWndProc_ ? CallWindowProcW(self->menuWndProc_, hwnd, msg, wp, lp)
                                   : DefWindowProcW(hwnd, msg, wp, lp);
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, tsfx::IID_ITfLangBarItemButton)) {
    *ppv = static_cast<ITfLangBarItemButton*>(this);
  } else if (IsEqualIID(riid, IID_ITfSource)) {
    *ppv = static_cast<ITfSource*>(this);
  }
  if (!*ppv) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }

STDMETHODIMP_(ULONG) LangBarButton::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return ULONG(r);
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
  if (!pInfo) return E_INVALIDARG;
  pInfo->clsidService = CLSID_YYIME;
  pInfo->guidItem = tsfx::GUID_LBI_INPUTMODE;
  pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  pInfo->ulSort = 0;
  lstrcpynW(pInfo->szDescription, L"yy输入法 中/英", TF_LBI_DESC_MAXLEN);
  return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* pdwStatus) {
  if (!pdwStatus) return E_INVALIDARG;
  *pdwStatus = 0;
  return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* pbstrToolTip) {
  if (!pbstrToolTip) return E_INVALIDARG;
  bool cn = owner_ && owner_->chinese();
  *pbstrToolTip = SysAllocString(cn ? L"yy输入法：中文（Shift 切换，右键设置）" : L"yy输入法：英文（Shift 切换，右键设置）");
  return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT*) {
  if (!owner_) return S_OK;
  if (click == TF_LBI_CLK_LEFT) owner_->ToggleChinese();
  else if (click == TF_LBI_CLK_RIGHT) {
    if (!menuWnd_) {
      menuWnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"yy输入法菜单", WS_POPUP,
                                pt.x, pt.y, 1, 1, nullptr, nullptr, g_hInst, nullptr);
      if (!menuWnd_) return HRESULT_FROM_WIN32(GetLastError());
      SetWindowLongPtrW(menuWnd_, GWLP_USERDATA, LONG_PTR(this));
      menuWndProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(menuWnd_, GWLP_WNDPROC, LONG_PTR(MenuWndProc)));
    }
    // 先结束托盘的 COM 回调，让系统释放点击/焦点处理，再进入菜单消息循环。
    if (!PostMessageW(menuWnd_, WM_APP + 0x42, WPARAM(INT_PTR(pt.x)), LPARAM(INT_PTR(pt.y))))
      return HRESULT_FROM_WIN32(GetLastError());
  }
  return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu*) { return S_OK; }
STDMETHODIMP LangBarButton::OnMenuSelect(UINT) { return S_OK; }

STDMETHODIMP LangBarButton::GetIcon(HICON* phIcon) {
  if (!phIcon) return E_INVALIDARG;
  *phIcon = MakeModeIcon(!owner_ || owner_->chinese());
  return *phIcon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* pbstrText) {
  if (!pbstrText) return E_INVALIDARG;
  *pbstrText = SysAllocString((!owner_ || owner_->chinese()) ? L"中" : L"英");
  return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
  if (!punk || !pdwCookie) return E_INVALIDARG;
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&sink_)))) {
    sink_ = nullptr;
    return E_NOINTERFACE;
  }
  *pdwCookie = 1;
  return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) {
  if (dwCookie != 1 || !sink_) return CONNECT_E_NOCONNECTION;
  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

void LangBarButton::Update() {
  if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

}  // namespace yy
