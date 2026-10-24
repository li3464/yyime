#pragma once
// 设置侧栏使用程序内嵌的同一份 DeepSeek 头像图标，无需读取外部图片。
#include <windows.h>
#include <gdiplus.h>
namespace yy {
inline int LogoDetail(float pixels) { return pixels >= 46 ? 2 : pixels >= 24 ? 1 : 0; }
inline void DrawLogo(Gdiplus::Graphics& g, const Gdiplus::RectF& r, Gdiplus::Color,
                     Gdiplus::Color, int) {
  static HICON icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),
                                                 IMAGE_ICON,256,256,LR_SHARED));
  if(!icon) return;
  Gdiplus::Bitmap bitmap(icon);
  const auto saved=g.Save();
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.DrawImage(&bitmap,r);
  g.Restore(saved);
}
} // namespace yy
