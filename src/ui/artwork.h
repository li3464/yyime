#pragma once
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace yy {
// 解码后释放文件和 COM 句柄。缓存只保存缩小后的预乘 BGRA 像素。
struct Artwork {
  UINT width = 0, height = 0;
  std::vector<uint8_t> pixels;
  bool Load(const std::wstring& path, UINT limit = 512) {
    width = height = 0; pixels.clear();
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT sourceW = 0, sourceH = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&sourceW, &sourceH);
    if (SUCCEEDED(hr) && (!sourceW || !sourceH || sourceW > 8192 || sourceH > 8192)) hr = E_INVALIDARG;
    IWICBitmapSource* source = frame;
    if (SUCCEEDED(hr)) {
      double scale = std::min(1.0, double(limit) / std::max(sourceW, sourceH));
      width = std::max(1u, UINT(sourceW * scale)); height = std::max(1u, UINT(sourceH * scale));
      hr = factory->CreateBitmapScaler(&scaler);
      if (SUCCEEDED(hr)) hr = scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant);
      source = scaler;
    }
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
      pixels.resize(size_t(width) * height * 4);
      hr = converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data());
    }
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (FAILED(hr)) { pixels.clear(); width = height = 0; }
    return !pixels.empty();
  }
};
}  // namespace yy
