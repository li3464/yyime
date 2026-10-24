// DLL 入口和 COM 导出函数。
#include <windows.h>
#include <ole2.h>

#include <new>

#include "../ui/candidate_window.h"
#include "globals.h"
#include "text_service.h"

namespace yy {

HINSTANCE g_hInst = nullptr;
static LONG g_refCount = 0;

void DllAddRef() { InterlockedIncrement(&g_refCount); }
void DllRelease() { InterlockedDecrement(&g_refCount); }
LONG DllRefCount() { return g_refCount; }

namespace {

class ClassFactory final : public IClassFactory {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override {
    DllAddRef();
    return 2;
  }
  STDMETHODIMP_(ULONG) Release() override {
    DllRelease();
    return 1;
  }
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (outer) return CLASS_E_NOAGGREGATION;
    TextService* ts = new (std::nothrow) TextService();
    if (!ts) return E_OUTOFMEMORY;
    HRESULT hr = ts->QueryInterface(riid, ppv);
    ts->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) DllAddRef();
    else DllRelease();
    return S_OK;
  }
};

ClassFactory g_factory;

}  // namespace
}  // namespace yy

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    yy::g_hInst = hinst;
    DisableThreadLibraryCalls(hinst);
  } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
    yy::CandidateWindow::UnregisterWindowClass();
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (!IsEqualCLSID(rclsid, yy::CLSID_YYIME)) return CLASS_E_CLASSNOTAVAILABLE;
  return yy::g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return yy::DllRefCount() <= 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
  HRESULT init = CoInitialize(nullptr);
  HRESULT hr = yy::RegisterServer();
  if (FAILED(hr)) yy::UnregisterServer();
  if (SUCCEEDED(init)) CoUninitialize();
  return hr;
}

STDAPI DllUnregisterServer() {
  HRESULT init = CoInitialize(nullptr);
  HRESULT hr = yy::UnregisterServer();
  if (SUCCEEDED(init)) CoUninitialize();
  return hr;
}
