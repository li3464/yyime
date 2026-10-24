// 冒烟测试：不注册，直接加载 DLL，创建 TextService，查各个接口再释放。
// 用法: com_smoke.exe <dll 路径>
#include <windows.h>
#include <msctf.h>

#include <cstdio>

#include "../src/tsf/globals.h"
#include "../src/tsf/tsf_compat.h"

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return 2;
  HMODULE dll = LoadLibraryW(argv[1]);
  if (!dll) {
    printf("LoadLibrary failed %lu\n", GetLastError());
    return 1;
  }
  using GetClassObject = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
  using CanUnload = HRESULT(WINAPI*)();
  auto getClass = reinterpret_cast<GetClassObject>(GetProcAddress(dll, "DllGetClassObject"));
  auto canUnload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
  if (!getClass || !canUnload) {
    printf("missing exports\n");
    return 1;
  }
  IClassFactory* cf = nullptr;
  HRESULT hr = getClass(yy::CLSID_YYIME, IID_IClassFactory, reinterpret_cast<void**>(&cf));
  printf("DllGetClassObject: 0x%08lx\n", hr);
  if (FAILED(hr)) return 1;
  ITfTextInputProcessor* tip = nullptr;
  hr = cf->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip));
  printf("CreateInstance: 0x%08lx\n", hr);
  if (FAILED(hr)) return 1;
  struct {
    const char* name;
    const IID* iid;
  } checks[] = {
      {"ITfTextInputProcessorEx", &tsfx::IID_ITfTextInputProcessorEx},
      {"ITfThreadMgrEventSink", &IID_ITfThreadMgrEventSink},
      {"ITfKeyEventSink", &IID_ITfKeyEventSink},
      {"ITfCompositionSink", &IID_ITfCompositionSink},
      {"ITfDisplayAttributeProvider", &tsfx::IID_ITfDisplayAttributeProvider},
      {"ITfCompartmentEventSink", &IID_ITfCompartmentEventSink},
      {"ITfThreadFocusSink", &IID_ITfThreadFocusSink},
  };
  for (auto& c : checks) {
    IUnknown* p = nullptr;
    hr = tip->QueryInterface(*c.iid, reinterpret_cast<void**>(&p));
    printf("  QI %-28s 0x%08lx\n", c.name, hr);
    if (p) p->Release();
  }
  // 显示属性
  ITfDisplayAttributeProvider* dap = nullptr;
  if (SUCCEEDED(tip->QueryInterface(tsfx::IID_ITfDisplayAttributeProvider, reinterpret_cast<void**>(&dap)))) {
    IEnumTfDisplayAttributeInfo* e = nullptr;
    if (SUCCEEDED(dap->EnumDisplayAttributeInfo(&e))) {
      ITfDisplayAttributeInfo* info = nullptr;
      ULONG n = 0;
      e->Next(1, &info, &n);
      if (info) {
        BSTR desc = nullptr;
        info->GetDescription(&desc);
        printf("  display attribute ok (%d)\n", desc ? 1 : 0);
        SysFreeString(desc);
        info->Release();
      }
      e->Release();
    }
    dap->Release();
  }
  tip->Release();
  cf->Release();
  printf("DllCanUnloadNow: 0x%08lx (0 = 可以卸载)\n", canUnload());
  FreeLibrary(dll);
  printf("done\n");
  return 0;
}
