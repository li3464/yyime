#pragma once
// 任务栏输入指示器上的“中/英”按钮：左键切换，右键出菜单。
#include "tsf_compat.h"

namespace yy {

class TextService;

class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit LangBarButton(TextService* owner);
  void Detach();
  void Update();  // 通知系统重新取图标和文字

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  // ITfLangBarItem
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
  STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
  STDMETHODIMP Show(BOOL fShow) override;
  STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;
  // ITfLangBarItemButton
  STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
  STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
  STDMETHODIMP OnMenuSelect(UINT wID) override;
  STDMETHODIMP GetIcon(HICON* phIcon) override;
  STDMETHODIMP GetText(BSTR* pbstrText) override;
  // ITfSource
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
  STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

 private:
  ~LangBarButton();
  static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  HWND menuWnd_ = nullptr;
  WNDPROC menuWndProc_ = nullptr;
  bool menuOpen_ = false;
  LONG ref_ = 1;
  TextService* owner_ = nullptr;
  ITfLangBarItemSink* sink_ = nullptr;
};

// 画一个“中”或“英”的小图标，颜色跟着任务栏深浅色走
HICON MakeModeIcon(bool chinese);

}  // namespace yy
