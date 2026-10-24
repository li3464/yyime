// 注册：COM 组件（HKLM\Software\Classes\CLSID）+ TSF 输入法配置 + 能力分类。
// 由 regsvr32 调用，需要管理员权限。
#include <windows.h>
#include <olectl.h>

#include <string>

#include "globals.h"
#include "tsf_compat.h"

namespace yy {

namespace {

const GUID* const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &tsfx::GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &tsfx::GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &tsfx::GUID_TFCAT_TIPCAP_COMLESS,
};

std::wstring GuidString(const GUID& g) {
  wchar_t buf[64] = {};
  StringFromGUID2(g, buf, 64);
  return buf;
}

std::wstring ModulePath() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(g_hInst, path, MAX_PATH);
  return path;
}

bool SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
  return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

HRESULT RegisterComServer() {
  std::wstring key = L"Software\\Classes\\CLSID\\" + GuidString(CLSID_YYIME);
  HKEY hk = nullptr, inproc = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr) !=
      ERROR_SUCCESS) {
    return SELFREG_E_CLASS;
  }
  bool ok = SetString(hk, nullptr, kImeName);
  if (RegCreateKeyExW(hk, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, nullptr) == ERROR_SUCCESS) {
    ok = ok && SetString(inproc, nullptr, ModulePath()) && SetString(inproc, L"ThreadingModel", L"Apartment");
    RegCloseKey(inproc);
  } else {
    ok = false;
  }
  RegCloseKey(hk);
  return ok ? S_OK : SELFREG_E_CLASS;
}

void UnregisterComServer() {
  std::wstring key = L"Software\\Classes\\CLSID\\" + GuidString(CLSID_YYIME);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
}

}  // namespace

HRESULT RegisterServer() {
  HRESULT hr = RegisterComServer();
  if (FAILED(hr)) return hr;

  ITfInputProcessorProfileMgr* mgr = nullptr;
  hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                        reinterpret_cast<void**>(&mgr));
  if (FAILED(hr)) return hr;
  std::wstring icon = ModulePath();
  hr = mgr->RegisterProfile(CLSID_YYIME, kLangId, GUID_YYIME_PROFILE, kImeName, ULONG(wcslen(kImeName)),
                            icon.c_str(), ULONG(icon.size()), ULONG(-IDI_YYIME), nullptr, 0, TRUE, 0);
  mgr->Release();
  if (FAILED(hr)) return hr;

  ITfCategoryMgr* cat = nullptr;
  hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                        reinterpret_cast<void**>(&cat));
  if (FAILED(hr)) return hr;
  for (const GUID* g : kCategories) {
    hr = cat->RegisterCategory(CLSID_YYIME, *g, CLSID_YYIME);
    if (FAILED(hr)) break;
  }
  cat->Release();
  return hr;
}

HRESULT UnregisterServer() {
  ITfInputProcessorProfileMgr* mgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr)))) {
    mgr->UnregisterProfile(CLSID_YYIME, kLangId, GUID_YYIME_PROFILE, 0);
    mgr->Release();
  }
  ITfCategoryMgr* cat = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 reinterpret_cast<void**>(&cat)))) {
    for (const GUID* g : kCategories) cat->UnregisterCategory(CLSID_YYIME, *g, CLSID_YYIME);
    cat->Release();
  }
  UnregisterComServer();
  return S_OK;
}

}  // namespace yy
