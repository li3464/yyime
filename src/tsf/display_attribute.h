#pragma once
// 正在打的拼音在文档里显示成什么样（这里是点状下划线）。
#include "tsf_compat.h"

namespace yy {

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
 public:
  DisplayAttributeInfo();
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP GetGUID(GUID* pguid) override;
  STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP Reset() override;

 private:
  ~DisplayAttributeInfo();
  LONG ref_ = 1;
};

class EnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
 public:
  EnumDisplayAttributeInfo();
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
  STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
  STDMETHODIMP Reset() override;
  STDMETHODIMP Skip(ULONG ulCount) override;

 private:
  ~EnumDisplayAttributeInfo();
  LONG ref_ = 1;
  ULONG index_ = 0;
};

}  // namespace yy
