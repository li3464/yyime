#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include "../src/engine/config.h"
#include "../src/ui/artwork.h"
#include "../src/ui/candidate_window.h"

namespace yy { HINSTANCE g_hInst = nullptr; }

int wmain() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  yy::g_hInst = GetModuleHandleW(nullptr);
  yy::Config config;
  config.themes = yy::BuiltinThemes();
  yy::CandidateWindow candidate;
  if (!candidate.Create()) return 2;
  yy::CandidateView view;
  view.aux = L"ni'hao'shi'jie";
  view.items = {{L"你好，世界"}, {L"你好"}, {L"世界"}};
  view.hasNext = true;
  RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  RECT caret{work.right-650, work.bottom-240, work.right-648, work.bottom-220};
  int checked = 0, failures = 0;
  for (const auto& theme : config.themes) {
    if (theme.artwork.empty()) continue;
    yy::Artwork image;
    bool ok = image.Load(yy::ThemeAssetPath(theme), 192);
    ok = ok && image.width <= 192 && image.height <= 192 &&
         image.pixels.size() == size_t(image.width) * image.height * 4;
    bool transparent = false, visible = false;
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
      auto alpha = image.pixels[i + 3];
      transparent = transparent || alpha == 0;
      visible = visible || alpha > 200;
      ok = ok && image.pixels[i] <= alpha && image.pixels[i+1] <= alpha && image.pixels[i+2] <= alpha;
    }
    ok = ok && transparent && visible;
    for (bool vertical : {false, true}) {
      view.vertical = vertical;
      candidate.SetStyle(theme, L"Microsoft YaHei UI", 17);
      candidate.Show(view, caret);
      RECT decorated{}; GetWindowRect(candidate.hwnd(), &decorated);
      auto plain = theme; plain.artwork.clear();
      candidate.SetStyle(plain, L"Microsoft YaHei UI", 17);
      candidate.Show(view, caret);
      RECT undecorated{}; GetWindowRect(candidate.hwnd(), &undecorated);
      int addedWidth = (decorated.right-decorated.left)-(undecorated.right-undecorated.left);
      int expected = MulDiv(78, GetDpiForWindow(candidate.hwnd()), 96);
      ok = ok && IsWindowVisible(candidate.hwnd()) && std::abs(addedWidth-expected) <= 1;
    }
    printf("%s artwork %ls\n", ok ? "PASS" : "FAIL", theme.name.c_str());
    ++checked; if (!ok) ++failures;
    if (image.Load(L"missing-artwork.png") || image.width || image.height || !image.pixels.empty()) ++failures;
  }
  if (checked != 10) ++failures;
  printf("%d character assets, %d failures\n", checked, failures);
  candidate.Destroy(); yy::CandidateWindow::UnregisterWindowClass();
  CoUninitialize();
  return failures ? 1 : 0;
}
