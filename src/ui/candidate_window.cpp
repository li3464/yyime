#include "candidate_window.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "../tsf/globals.h"

namespace yy {

namespace {

constexpr wchar_t kClassName[] = L"yyime.candidate";
ATOM g_classAtom = 0;
IDWriteFactory* g_dwrite = nullptr;
std::once_flag g_dwriteOnce;

IDWriteFactory* DWrite() {
  std::call_once(g_dwriteOnce, [] {
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dwrite));
  });
  return g_dwrite;
}

UINT WindowDpi(HWND h) {
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn && h) {
    UINT d = fn(h);
    if (d) return d;
  }
  HDC dc = GetDC(nullptr);
  int d = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  return d > 0 ? UINT(d) : 96;
}

D2D1_COLOR_F Color(uint32_t argb, float alpha = 1.0f) {
  return D2D1_COLOR_F{float((argb >> 16) & 255) / 255.0f, float((argb >> 8) & 255) / 255.0f,
                      float(argb & 255) / 255.0f, float((argb >> 24) & 255) / 255.0f * alpha};
}

struct Metrics {
  float w = 0, h = 0;
};

Metrics Measure(IDWriteTextLayout* l) {
  Metrics m;
  DWRITE_TEXT_METRICS tm{};
  if (l && SUCCEEDED(l->GetMetrics(&tm))) {
    m.w = tm.widthIncludingTrailingWhitespace;
    m.h = tm.height;
  }
  return m;
}

template <typename T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

constexpr auto kColorFont = D2D1_DRAW_TEXT_OPTIONS(4);  // D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT

}  // namespace

CandidateWindow::CandidateWindow() = default;

CandidateWindow::~CandidateWindow() { Destroy(); }

bool CandidateWindow::Create() {
  if (hwnd_) return true;
  if (!g_classAtom) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    g_classAtom = RegisterClassExW(&wc);
    if (!g_classAtom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) g_classAtom = 1;
  }
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
                          WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hInst, this);
  return hwnd_ != nullptr;
}

void CandidateWindow::Destroy() {
  if (hwnd_) {
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  visible_ = false;
  ReleaseFormats();
  SafeRelease(rt_);
  SafeRelease(characterBitmap_);
  SafeRelease(d2d_);
  if (memDC_) {
    if (oldBmp_) SelectObject(memDC_, oldBmp_);
    DeleteDC(memDC_);
    memDC_ = nullptr;
  }
  if (dib_) {
    DeleteObject(dib_);
    dib_ = nullptr;
  }
  oldBmp_ = nullptr;
  dibW_ = dibH_ = 0;
}

void CandidateWindow::UnregisterWindowClass() {
  if (g_classAtom) {
    UnregisterClassW(kClassName, g_hInst);
    g_classAtom = 0;
  }
}

void CandidateWindow::SetStyle(const Theme& theme, const std::wstring& font, float fontSize) {
  if (theme.artwork != theme_.artwork) {
    SafeRelease(characterBitmap_);
    artwork_.Load(ThemeAssetPath(theme), 192);
  }
  theme_ = theme;
  if (font != font_ || fontSize != fontSize_) {
    font_ = font;
    fontSize_ = fontSize;
    ReleaseFormats();
  }
}

void CandidateWindow::Show(const CandidateView& view, const RECT& caret) {
  if (!hwnd_ && !Create()) return;
  view_ = view;
  Render(caret);
}

void CandidateWindow::Hide() {
  if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
  visible_ = false;
}

// ---------------------------------------------------------------- 资源

bool CandidateWindow::EnsureDevice() {
  if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                        reinterpret_cast<void**>(&d2d_)))) {
    return false;
  }
  if (!rt_) {
    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = props.dpiY = 96.0f;
    if (FAILED(d2d_->CreateDCRenderTarget(&props, &rt_))) return false;
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  }
  return DWrite() != nullptr;
}

bool CandidateWindow::EnsureFormats(float scale) {
  if (fmtText_ && formatScale_ == scale) return true;
  ReleaseFormats();
  IDWriteFactory* f = DWrite();
  if (!f) return false;
  float size = fontSize_ * scale;
  auto make = [&](float sz, IDWriteTextFormat** out) {
    if (FAILED(f->CreateTextFormat(font_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL, sz, L"zh-cn", out))) {
      // 字体名写错了就退回系统默认
      if (FAILED(f->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz, L"zh-cn", out))) {
        return false;
      }
    }
    (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
  };
  if (!make(size, &fmtText_) || !make(size * 0.80f, &fmtSmall_) || !make(size * 0.74f, &fmtComment_)) {
    ReleaseFormats();
    return false;
  }
  formatScale_ = scale;
  return true;
}

void CandidateWindow::ReleaseFormats() {
  SafeRelease(fmtText_);
  SafeRelease(fmtSmall_);
  SafeRelease(fmtComment_);
  formatScale_ = 0.0f;
}

bool CandidateWindow::EnsureSurface(int w, int h) {
  if (!memDC_) memDC_ = CreateCompatibleDC(nullptr);
  if (!memDC_) return false;
  if (dib_ && w <= dibW_ && h <= dibH_) return true;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = std::max(w, dibW_);
  bi.bmiHeader.biHeight = -std::max(h, dibH_);
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP nb = CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!nb) return false;
  HGDIOBJ prev = SelectObject(memDC_, nb);
  if (!oldBmp_) oldBmp_ = prev;
  if (dib_) DeleteObject(dib_);
  dib_ = nb;
  dibW_ = bi.bmiHeader.biWidth;
  dibH_ = -bi.bmiHeader.biHeight;
  return true;
}

IDWriteTextLayout* CandidateWindow::MakeLayout(const std::wstring& s, IDWriteTextFormat* fmt, float maxWidth) {
  IDWriteTextLayout* l = nullptr;
  if (FAILED(DWrite()->CreateTextLayout(s.c_str(), UINT32(s.size()), fmt, maxWidth, 100000.0f, &l))) return nullptr;
  return l;
}

// ---------------------------------------------------------------- 绘制

void CandidateWindow::Render(const RECT& caret) {
  if (!EnsureDevice()) return;
  const float s = float(WindowDpi(hwnd_)) / 96.0f;
  if (!EnsureFormats(s)) return;

  bool anyAi = false;
  for (auto& it : view_.items) anyAi = anyAi || it.ai;
  const bool vertical = view_.vertical || anyAi;
  const float padX = 10 * s, padY = 7 * s, itemPadX = 6 * s, itemPadY = 3 * s;
  const float idxGap = 4 * s, commentGap = 5 * s, auxGap = 4 * s;
  const float margin = theme_.shadow ? 12 * s : 2 * s;
  const float radius = theme_.cornerRadius * s;
  const float itemRadius = std::min(radius * 0.7f, 7 * s);
  const float wrapWidth = 440 * s;

  // ---- 量尺寸
  IDWriteTextLayout* auxL = view_.aux.empty() ? nullptr : MakeLayout(view_.aux, fmtSmall_, 4000);
  IDWriteTextLayout* statusL = view_.status.empty() ? nullptr : MakeLayout(view_.status, fmtSmall_, 4000);
  Metrics am = Measure(auxL), sm = Measure(statusL);
  float auxW = am.w + (auxL && statusL ? 12 * s : 0) + sm.w;
  float auxH = std::max(am.h, sm.h);
  float y = padY + (auxH > 0 ? auxH + auxGap : 0);

  std::vector<ItemBox> boxes(view_.items.size());
  float x = padX, rowBottom = y, maxItemW = 0;
  for (size_t i = 0; i < boxes.size(); ++i) {
    const CandidateItem& it = view_.items[i];
    ItemBox& b = boxes[i];
    b.index = MakeLayout(std::to_wstring(i + 1), fmtSmall_, 4000);
    b.text = MakeLayout(it.text, fmtText_, anyAi ? wrapWidth : 4000);
    if (b.text && anyAi) b.text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (!it.comment.empty()) b.comment = MakeLayout(it.comment, fmtComment_, 4000);
    Metrics im = Measure(b.index), tm = Measure(b.text), cm = Measure(b.comment);
    float h = std::max({im.h, tm.h, cm.h});
    float w = itemPadX + im.w + idxGap + tm.w + (b.comment ? commentGap + cm.w : 0) + itemPadX;
    float left = vertical ? padX : x;
    float top = vertical ? rowBottom : y;
    b.rect = D2D1_RECT_F{left, top, left + w, top + h + 2 * itemPadY};
    // 各部分竖直居中；序号和备注字小，往下对齐一点让基线接近
    b.indexPos = {left + itemPadX, top + itemPadY + (vertical && anyAi ? (Measure(b.index).h * 0.1f) : (h - im.h) / 2)};
    b.textPos = {left + itemPadX + im.w + idxGap, top + itemPadY + (h - tm.h) / 2};
    b.commentPos = {b.textPos.x + tm.w + commentGap, top + itemPadY + (h - cm.h) / 2};
    if (vertical) rowBottom = b.rect.bottom + 1 * s;
    else x = b.rect.right + 2 * s;
    maxItemW = std::max(maxItemW, w);
    if (!vertical) rowBottom = std::max(rowBottom, b.rect.bottom);
  }
  if (vertical)
    for (auto& b : boxes) b.rect.right = b.rect.left + maxItemW;

  const bool paging = view_.hasPrev || view_.hasNext;
  IDWriteTextLayout* prevL = paging ? MakeLayout(L"‹", fmtText_, 4000) : nullptr;
  IDWriteTextLayout* nextL = paging ? MakeLayout(L"›", fmtText_, 4000) : nullptr;
  Metrics pm = Measure(prevL);
  float arrowsW = paging ? (pm.w * 2 + 10 * s) : 0;

  float contentW, contentH;
  if (vertical) {
    contentW = std::max(maxItemW + 2 * padX, auxW + 2 * padX);
    contentH = (boxes.empty() ? y : rowBottom) + padY + (paging ? pm.h : 0);
    if (paging) contentW = std::max(contentW, arrowsW + 2 * padX);
  } else {
    float itemsRight = boxes.empty() ? padX : x - 2 * s;
    contentW = std::max(itemsRight + (paging ? arrowsW : 0) + padX, auxW + 2 * padX);
    contentH = (boxes.empty() ? y - auxGap : rowBottom) + padY;
  }
  contentW = std::max(contentW, 60 * s);
  const float characterW = artwork_.pixels.empty() ? 0 : 78 * s;
  contentW += characterW;
  if (characterW > 0) contentH = std::max(contentH, 86 * s);
  const int W = int(std::ceil(contentW + 2 * margin));
  const int H = int(std::ceil(contentH + 2 * margin));

  // ---- 摆位置：光标下方，放不下就放上方
  HMONITOR mon = MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  const RECT& work = mi.rcWork;
  int gap = int(3 * s);
  int wx = caret.left - int(margin);
  int wy = caret.bottom + gap - int(margin);
  if (wy + H - margin > work.bottom) wy = caret.top - gap - H + int(margin);
  wx = std::max(int(work.left - margin), std::min(wx, int(work.right - W + margin)));
  wy = std::max(int(work.top - margin), wy);

  // ---- 画
  hitRects_.clear();
  if (EnsureSurface(W, H)) {
    RECT bind{0, 0, W, H};
    rt_->BindDC(memDC_, &bind);
    rt_->BeginDraw();
    rt_->Clear(D2D1_COLOR_F{0, 0, 0, 0});
    ID2D1SolidColorBrush* brush = nullptr;
    rt_->CreateSolidColorBrush(Color(0xFF000000), &brush);
    if (brush) {
      D2D1_RECT_F panel{margin, margin, margin + contentW, margin + contentH};
      if (theme_.shadow) {
        for (int k = 6; k >= 1; --k) {
          float g = float(k) * 1.6f * s;
          brush->SetColor(D2D1_COLOR_F{0, 0, 0, 0.018f * float(7 - k)});
          D2D1_ROUNDED_RECT rr{{panel.left - g, panel.top - g + 2 * s, panel.right + g, panel.bottom + g + 2 * s},
                               radius + g, radius + g};
          rt_->FillRoundedRectangle(&rr, brush);
        }
      }
      D2D1_ROUNDED_RECT pr{panel, radius, radius};
      brush->SetColor(Color(theme_.background));
      rt_->FillRoundedRectangle(&pr, brush);
      if (theme_.borderWidth > 0) {
        float bw = theme_.borderWidth * s;
        D2D1_ROUNDED_RECT br{{panel.left + bw / 2, panel.top + bw / 2, panel.right - bw / 2, panel.bottom - bw / 2},
                             radius, radius};
        brush->SetColor(Color(theme_.border));
        rt_->DrawRoundedRectangle(&br, brush, bw);
      }
      if (auxL) {
        brush->SetColor(Color(theme_.preedit));
        rt_->DrawTextLayout({panel.left + padX, panel.top + padY}, auxL, brush, kColorFont);
      }
      if (statusL) {
        brush->SetColor(Color(theme_.accent));
        float sx = panel.left + padX + (auxL ? am.w + 12 * s : 0);
        rt_->DrawTextLayout({sx, panel.top + padY}, statusL, brush, kColorFont);
      }
      for (size_t i = 0; i < boxes.size(); ++i) {
        ItemBox& b = boxes[i];
        const bool hl = int(i) == view_.highlight;
        const bool ai = view_.items[i].ai;
        D2D1_RECT_F r{b.rect.left + margin, b.rect.top + margin, b.rect.right + margin, b.rect.bottom + margin};
        hitRects_.push_back(r);
        if (hl) {
          D2D1_ROUNDED_RECT hr{r, itemRadius, itemRadius};
          brush->SetColor(Color(theme_.highlightBg));
          rt_->FillRoundedRectangle(&hr, brush);
        }
        if (b.index) {
          brush->SetColor(hl ? Color(theme_.highlightText, 0.75f) : Color(ai ? theme_.accent : theme_.index));
          rt_->DrawTextLayout({b.indexPos.x + margin, b.indexPos.y + margin}, b.index, brush);
        }
        if (b.text) {
          brush->SetColor(Color(hl ? theme_.highlightText : theme_.text));
          rt_->DrawTextLayout({b.textPos.x + margin, b.textPos.y + margin}, b.text, brush, kColorFont);
        }
        if (b.comment) {
          brush->SetColor(hl ? Color(theme_.highlightText, 0.7f) : Color(theme_.comment));
          rt_->DrawTextLayout({b.commentPos.x + margin, b.commentPos.y + margin}, b.comment, brush);
        }
      }
      if (paging && prevL && nextL) {
        float ax, ay;
        if (vertical) {
          ax = panel.right - characterW - padX - arrowsW + 10 * s;
          ay = panel.bottom - padY - pm.h;
        } else {
          ax = panel.right - characterW - padX - arrowsW + 10 * s;
          ay = boxes.empty() ? panel.top + padY : boxes[0].rect.top + margin + ((boxes[0].rect.bottom - boxes[0].rect.top) - pm.h) / 2;
        }
        brush->SetColor(Color(theme_.index, view_.hasPrev ? 1.0f : 0.35f));
        rt_->DrawTextLayout({ax, ay}, prevL, brush);
        brush->SetColor(Color(theme_.index, view_.hasNext ? 1.0f : 0.35f));
        rt_->DrawTextLayout({ax + pm.w + 6 * s, ay}, nextL, brush);
      }
      if (characterW > 0) {
        if (!characterBitmap_) {
          auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
          rt_->CreateBitmap(D2D1::SizeU(artwork_.width, artwork_.height), artwork_.pixels.data(), artwork_.width * 4,
                             properties, &characterBitmap_);
        }
        if (characterBitmap_) {
          float artH = std::min(contentH - 8 * s, 100 * s);
          float artW = artH * float(artwork_.width) / float(artwork_.height);
          if (artW > characterW - 2 * s) { artW = characterW - 2 * s; artH = artW * artwork_.height / artwork_.width; }
          float left = panel.right - characterW + (characterW - artW) / 2;
          rt_->DrawBitmap(characterBitmap_, D2D1::RectF(left, panel.bottom - artH - 4 * s, left + artW, panel.bottom - 4 * s),
                           1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
      }
      brush->Release();
    }
    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) { SafeRelease(characterBitmap_); SafeRelease(rt_); }
    POINT dst{wx, wy}, src{0, 0};
    SIZE size{W, H};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &dst, &size, memDC_, &src, 0, &bf, ULW_ALPHA);
    if (!visible_) {
      SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      visible_ = true;
    }
  }

  for (auto& b : boxes) {
    SafeRelease(b.index);
    SafeRelease(b.text);
    SafeRelease(b.comment);
  }
  SafeRelease(auxL);
  SafeRelease(statusL);
  SafeRelease(prevL);
  SafeRelease(nextL);
}

// ---------------------------------------------------------------- 消息

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    auto* created = static_cast<CandidateWindow*>(cs->lpCreateParams);
    if (created) created->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }
  auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->HandleMessage(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CandidateWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_LBUTTONUP: {
      float px = float(short(LOWORD(lp))), py = float(short(HIWORD(lp)));
      for (size_t i = 0; i < hitRects_.size(); ++i) {
        const D2D1_RECT_F& r = hitRects_[i];
        if (px >= r.left && px < r.right && py >= r.top && py < r.bottom) {
          if (onClick) onClick(int(i));
          break;
        }
      }
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (onWheel) onWheel(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
      return 0;
    case kMsgAiDone:
      if (onAiDone) onAiDone(wp, lp);
      return 0;
    default:
      return DefWindowProcW(hwnd_, msg, wp, lp);
  }
}

}  // namespace yy
