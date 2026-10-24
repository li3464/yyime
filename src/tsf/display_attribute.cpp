#include "display_attribute.h"

#include <new>

#include "globals.h"

namespace yy {

DisplayAttributeInfo::DisplayAttributeInfo() { DllAddRef(); }
DisplayAttributeInfo::~DisplayAttributeInfo() { DllRelease(); }

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
    *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }

STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return ULONG(r);
}

STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID* pguid) {
  if (!pguid) return E_INVALIDARG;
  *pguid = GUID_YYIME_ATTR_INPUT;
  return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
  if (!pbstrDesc) return E_INVALIDARG;
  *pbstrDesc = SysAllocString(L"yy输入法 正在输入");
  return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
  if (!pda) return E_INVALIDARG;
  ZeroMemory(pda, sizeof(*pda));
  pda->crText.type = TF_CT_NONE;
  pda->crBk.type = TF_CT_NONE;
  pda->lsStyle = TF_LS_DOT;
  pda->fBoldLine = FALSE;
  pda->crLine.type = TF_CT_NONE;
  pda->bAttr = TF_ATTR_INPUT;
  return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) { return E_NOTIMPL; }
STDMETHODIMP DisplayAttributeInfo::Reset() { return S_OK; }

// ---------------------------------------------------------------- 枚举（只有一个）

EnumDisplayAttributeInfo::EnumDisplayAttributeInfo() { DllAddRef(); }
EnumDisplayAttributeInfo::~EnumDisplayAttributeInfo() { DllRelease(); }

STDMETHODIMP EnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
    *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::AddRef() { return ULONG(InterlockedIncrement(&ref_)); }

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return ULONG(r);
}

STDMETHODIMP EnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
  if (!ppEnum) return E_INVALIDARG;
  auto* e = new (std::nothrow) EnumDisplayAttributeInfo();
  if (!e) return E_OUTOFMEMORY;
  e->index_ = index_;
  *ppEnum = e;
  return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) {
  if (!rgInfo) return E_INVALIDARG;
  ULONG fetched = 0;
  while (fetched < ulCount && index_ < 1) {
    auto* info = new (std::nothrow) DisplayAttributeInfo();
    if (!info) break;
    rgInfo[fetched++] = info;
    ++index_;
  }
  if (pcFetched) *pcFetched = fetched;
  return fetched == ulCount ? S_OK : S_FALSE;
}

STDMETHODIMP EnumDisplayAttributeInfo::Reset() {
  index_ = 0;
  return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Skip(ULONG ulCount) {
  index_ += ulCount;
  return index_ <= 1 ? S_OK : S_FALSE;
}

}  // namespace yy
