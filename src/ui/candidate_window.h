#pragma once
// 候选框：分层窗口 + Direct2D/DirectWrite 绘制，跟着光标走，不抢焦点。
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <functional>
#include <string>
#include <vector>

#include "../engine/config.h"

namespace yy {

struct CandidateItem {
  std::wstring text;
  std::wstring comment;
  bool ai = false;
};

struct CandidateView {
  std::wstring aux;     // 顶上一行：正在打的拼音，或者 AI 面板的标题
  std::wstring status;  // AI 状态，接在顶上一行右边
  std::vector<CandidateItem> items;
  int highlight = 0;
  bool hasPrev = false;
  bool hasNext = false;
  bool vertical = false;
};

class CandidateWindow {
 public:
  CandidateWindow();
  ~CandidateWindow();
  bool Create();
  void Destroy();
  void SetStyle(const Theme& theme, const std::wstring& font, float fontSize);
  void Show(const CandidateView& view, const RECT& caret);
  void Hide();
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }

  std::function<void(int)> onClick;  // 第几个候选（页内）
  std::function<void(int)> onWheel;  // -1 上一页 / +1 下一页
  std::function<void(WPARAM, LPARAM)> onAiDone;
  static constexpr UINT kMsgAiDone = WM_APP + 0x31;

  static void UnregisterWindowClass();

 private:
  struct ItemBox {
    D2D1_RECT_F rect;
    IDWriteTextLayout* index = nullptr;
    IDWriteTextLayout* text = nullptr;
    IDWriteTextLayout* comment = nullptr;
    D2D1_POINT_2F indexPos{}, textPos{}, commentPos{};
  };

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  bool EnsureDevice();
  bool EnsureFormats(float scale);
  void ReleaseFormats();
  bool EnsureSurface(int w, int h);
  IDWriteTextLayout* MakeLayout(const std::wstring& s, IDWriteTextFormat* fmt, float maxWidth);
  void Render(const RECT& caret);

  HWND hwnd_ = nullptr;
  bool visible_ = false;
  Theme theme_;
  std::wstring font_ = L"Microsoft YaHei UI";
  float fontSize_ = 17.0f;
  float formatScale_ = 0.0f;
  CandidateView view_;
  std::vector<D2D1_RECT_F> hitRects_;  // 窗口坐标里每个候选的位置

  ID2D1Factory* d2d_ = nullptr;
  ID2D1DCRenderTarget* rt_ = nullptr;
  IDWriteTextFormat* fmtText_ = nullptr;
  IDWriteTextFormat* fmtSmall_ = nullptr;
  IDWriteTextFormat* fmtComment_ = nullptr;
  HDC memDC_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ oldBmp_ = nullptr;
  int dibW_ = 0, dibH_ = 0;
};

}  // namespace yy
