// 将 DeepSeek 头像 PNG 缩放并打包成多尺寸 .ico，保留透明通道。
// 编译：x86_64-w64-mingw32-clang++ -std=c++20 -O2 -municode -DUNICODE -D_UNICODE -static
//       tools/make_icon.cpp -lgdiplus -lole32 -o build/make_icon.exe
// 用法：make_icon.exe <输出.ico> <头像.png> [预览.png] [256像素.png]
#include <windows.h>
#include <gdiplus.h>
#include <objidl.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Gdiplus;

namespace {

std::unique_ptr<Bitmap> sourceArt;

std::unique_ptr<Bitmap> Render(int size) {
  auto bmp = std::make_unique<Bitmap>(size, size, PixelFormat32bppARGB);
  Graphics g(bmp.get());
  g.Clear(Color(0, 0, 0, 0));
  g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  ImageAttributes attributes;
  attributes.SetWrapMode(WrapModeTileFlipXY);
  g.DrawImage(sourceArt.get(),RectF(0,0,float(size),float(size)),0,0,float(sourceArt->GetWidth()),
              float(sourceArt->GetHeight()),UnitPixel,&attributes);
  return bmp;
}

bool PngEncoder(CLSID* clsid) {
  UINT count = 0, bytes = 0;
  GetImageEncodersSize(&count, &bytes);
  if (!bytes) return false;
  std::vector<BYTE> buf(bytes);
  auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
  GetImageEncoders(count, bytes, info);
  for (UINT i = 0; i < count; ++i)
    if (wcscmp(info[i].MimeType, L"image/png") == 0) {
      *clsid = info[i].Clsid;
      return true;
    }
  return false;
}

std::vector<BYTE> PngBytes(Bitmap& bmp) {
  CLSID png;
  IStream* stream = nullptr;
  std::vector<BYTE> out;
  if (!PngEncoder(&png) || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return out;
  if (bmp.Save(stream, &png, nullptr) == Ok) {
    HGLOBAL mem = nullptr;
    GetHGlobalFromStream(stream, &mem);
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    auto* p = static_cast<BYTE*>(GlobalLock(mem));
    out.assign(p, p + st.cbSize.QuadPart);
    GlobalUnlock(mem);
  }
  stream->Release();
  return out;
}

// 经典图标帧：BITMAPINFOHEADER（高度写两倍）+ 自下而上的 BGRA + 透明 AND 掩码
std::vector<BYTE> BmpFrame(Bitmap& bmp, int size) {
  std::vector<BYTE> out(sizeof(BITMAPINFOHEADER));
  auto* h = reinterpret_cast<BITMAPINFOHEADER*>(out.data());
  h->biSize = sizeof(BITMAPINFOHEADER);
  h->biWidth = size;
  h->biHeight = size * 2;
  h->biPlanes = 1;
  h->biBitCount = 32;
  h->biSizeImage = DWORD(size * size * 4);
  BitmapData data;
  Rect all(0, 0, size, size);
  bmp.LockBits(&all, ImageLockModeRead, PixelFormat32bppARGB, &data);
  for (int y = size - 1; y >= 0; --y) {
    auto* row = static_cast<BYTE*>(data.Scan0) + y * data.Stride;
    out.insert(out.end(), row, row + size * 4);
  }
  size_t maskRow = size_t((size + 31) / 32 * 4);
  size_t maskStart = out.size();
  out.resize(out.size() + maskRow * size_t(size), 0);
  for (int y = 0; y < size; ++y) {
    const auto* row = static_cast<const BYTE*>(data.Scan0) + (size - 1 - y) * data.Stride;
    for (int x = 0; x < size; ++x)
      if (row[x * 4 + 3] < 128) out[maskStart + size_t(y) * maskRow + size_t(x / 8)] |= BYTE(128 >> (x % 8));
  }
  bmp.UnlockBits(&data);
  return out;
}

// 预览：左边 256，右边小尺寸按 1:1 和放大 4 倍各摆一份，浅色和深色底各一行
void SavePreview(const wchar_t* path) {
  const int sizes[] = {16, 24, 32, 40, 48};
  Bitmap sheet(760, 560, PixelFormat32bppARGB);
  Graphics g(&sheet);
  SolidBrush light(Color(255, 0xF4, 0xF4, 0xF5)), dark(Color(255, 0x1F, 0x20, 0x24));
  g.FillRectangle(&light, 0, 0, 760, 280);
  g.FillRectangle(&dark, 0, 280, 760, 280);
  g.SetInterpolationMode(InterpolationModeNearestNeighbor);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  for (int band = 0; band < 2; ++band) {
    int y0 = band * 280;
    auto big = Render(256);
    g.DrawImage(big.get(), 12, y0 + 12, 256, 256);
    int x = 290;
    for (int s : sizes) {
      auto small = Render(s);
      g.DrawImage(small.get(), x, y0 + 20, s, s);
      g.DrawImage(small.get(), x, y0 + 80, s * 4 > 192 ? 192 : s * 4, s * 4 > 192 ? 192 : s * 4);
      x += (s * 4 > 192 ? 192 : s * 4) + 12;
      if (x > 700) break;
    }
  }
  CLSID png;
  if (PngEncoder(&png)) sheet.Save(path, &png, nullptr);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    fwprintf(stderr, L"usage: make_icon <out.ico> <source.png> [preview.png] [icon256.png]\n");
    return 2;
  }
  GdiplusStartupInput in;
  ULONG_PTR token;
  GdiplusStartup(&token, &in, nullptr);
  {
    sourceArt=std::make_unique<Bitmap>(argv[2]);
    if(sourceArt->GetLastStatus()!=Ok || !sourceArt->GetWidth() || !sourceArt->GetHeight()) return 3;
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    std::vector<std::vector<BYTE>> frames;
    for (int s : sizes) {
      auto bmp = Render(s);
      frames.push_back(s >= 256 ? PngBytes(*bmp) : BmpFrame(*bmp, s));
      if (frames.back().empty()) return 3;
    }
    FILE* f = _wfopen(argv[1], L"wb");
    if (!f) return 4;
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    u16(0);
    u16(1);
    u16(uint16_t(frames.size()));
    uint32_t offset = uint32_t(6 + 16 * frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
      BYTE dim = sizes[i] >= 256 ? 0 : BYTE(sizes[i]);
      fputc(dim, f);
      fputc(dim, f);
      fputc(0, f);
      fputc(0, f);
      u16(1);
      u16(32);
      u32(uint32_t(frames[i].size()));
      u32(offset);
      offset += uint32_t(frames[i].size());
    }
    for (auto& fr : frames) fwrite(fr.data(), 1, fr.size(), f);
    fclose(f);
    if (argc > 3) SavePreview(argv[3]);
    if (argc > 4) {
      auto bmp = Render(256);
      CLSID png;
      if (!PngEncoder(&png) || bmp->Save(argv[4], &png, nullptr) != Ok) return 5;
    }
    wprintf(L"wrote %ls\n", argv[1]);
  }
  sourceArt.reset();
  GdiplusShutdown(token);
  return 0;
}
