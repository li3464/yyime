#pragma once
#include <windows.h>

namespace yy {

extern HINSTANCE g_hInst;

void DllAddRef();
void DllRelease();
LONG DllRefCount();

// {73F0FA18-2039-4C68-A952-D14AD44F3C9B}
constexpr GUID CLSID_YYIME = {0x73f0fa18, 0x2039, 0x4c68, {0xa9, 0x52, 0xd1, 0x4a, 0xd4, 0x4f, 0x3c, 0x9b}};
// {78C27759-B3C0-443B-A0BF-1AC02088F263}
constexpr GUID GUID_YYIME_PROFILE = {0x78c27759, 0xb3c0, 0x443b, {0xa0, 0xbf, 0x1a, 0xc0, 0x20, 0x88, 0xf2, 0x63}};
// {E5529C64-9821-4A5D-A665-7E33E47CF49F} 正在输入的拼音下面的虚线
constexpr GUID GUID_YYIME_ATTR_INPUT = {0xe5529c64, 0x9821, 0x4a5d, {0xa6, 0x65, 0x7e, 0x33, 0xe4, 0x7c, 0xf4, 0x9f}};

constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
constexpr wchar_t kImeName[] = L"yy输入法";
constexpr int IDI_YYIME = 101;

HRESULT RegisterServer();
HRESULT UnregisterServer();

}  // namespace yy
