#pragma once
// mingw 的 msctf.h / ctfutb.h 缺了一些 TSF 接口和常量，这里按 Windows SDK 的定义补上。
#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>

// ---------------------------------------------------------------- 接口

struct ITfTextInputProcessorEx : public ITfTextInputProcessor {
  virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) = 0;
};

struct ITfDisplayAttributeProvider : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) = 0;
};

#ifndef TF_LBI_CLK_RIGHT
typedef enum { TF_LBI_CLK_RIGHT = 1, TF_LBI_CLK_LEFT = 2 } TfLBIClick;
#endif

struct ITfMenu;

struct ITfLangBarItemButton : public ITfLangBarItem {
  virtual HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) = 0;
  virtual HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu* pMenu) = 0;
  virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT wID) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON* phIcon) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetText(BSTR* pbstrText) = 0;
};

// ---------------------------------------------------------------- 接口 ID 和分类 GUID

namespace tsfx {

constexpr GUID IID_ITfTextInputProcessorEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
constexpr GUID IID_ITfDisplayAttributeProvider = {0xfee47777, 0x163c, 0x4769, {0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54}};
constexpr GUID IID_ITfLangBarItemButton = {0x28c7f1d0, 0xde25, 0x11d2, {0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};

constexpr GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT = {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}};
constexpr GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT = {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}};
constexpr GUID GUID_TFCAT_TIPCAP_COMLESS = {0x364215d9, 0x75bc, 0x11d7, {0xa6, 0xef, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID GUID_TFCAT_TIPCAP_UIELEMENTENABLED = {0x49d2f9cf, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT = {0xccf05dd7, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION = {0xccf05dd8, 0x4a87, 0x11d7, {0xa6, 0xe2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
constexpr GUID GUID_LBI_INPUTMODE = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};

}  // namespace tsfx

// ---------------------------------------------------------------- 常量

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_INVALID_GUIDATOM
#define TF_INVALID_GUIDATOM ((TfGuidAtom)0)
#endif

#ifndef TF_LBI_STYLE_SHOWNINTRAY
#define TF_LBI_STYLE_HIDDENSTATUSCONTROL 0x00000001
#define TF_LBI_STYLE_SHOWNINTRAY 0x00000002
#define TF_LBI_STYLE_HIDEONNOOTHERITEMS 0x00000004
#define TF_LBI_STYLE_SHOWNINTRAYONLY 0x00000008
#define TF_LBI_STYLE_HIDDENBYDEFAULT 0x00000010
#define TF_LBI_STYLE_TEXTCOLORICON 0x00000020
#define TF_LBI_STYLE_BTN_BUTTON 0x00010000
#define TF_LBI_STYLE_BTN_MENU 0x00020000
#define TF_LBI_STYLE_BTN_TOGGLE 0x00040000
#endif

#ifndef TF_LBI_ICON
#define TF_LBI_ICON 0x00000001
#define TF_LBI_TEXT 0x00000002
#define TF_LBI_TOOLTIP 0x00000004
#define TF_LBI_BITMAP 0x00000008
#define TF_LBI_BALLOON 0x00000010
#define TF_LBI_CUSTOMUI 0x00000020
#define TF_LBI_STATUS 0x00010000
#endif

#ifndef TF_LBI_STATUS_HIDDEN
#define TF_LBI_STATUS_HIDDEN 0x00000001
#define TF_LBI_STATUS_DISABLED 0x00000002
#define TF_LBI_STATUS_BTN_TOGGLED 0x00010000
#endif

#ifndef TF_CONVERSIONMODE_NATIVE
#define TF_CONVERSIONMODE_ALPHANUMERIC 0x0000
#define TF_CONVERSIONMODE_NATIVE 0x0001
#define TF_CONVERSIONMODE_FULLSHAPE 0x0008
#define TF_CONVERSIONMODE_SYMBOL 0x0400
#endif
