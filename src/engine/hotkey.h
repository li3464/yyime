#pragma once
#include <windows.h>
#include <cwctype>
#include <string>
#include <string_view>

namespace yy {
struct KeyChord {
  UINT vk = 0;
  bool ctrl = false, alt = false, shift = false;
  bool Matches(UINT key, bool c, bool a, bool s) const {
    return vk && vk == key && ctrl == c && alt == a && shift == s;
  }
  bool operator==(const KeyChord&) const = default;
};

inline KeyChord ParseKeyChord(std::wstring_view spec) {
  KeyChord out;
  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t end = spec.find(L'+', pos);
    if (end == std::wstring_view::npos) end = spec.size();
    std::wstring part(spec.substr(pos, end - pos));
    while (!part.empty() && iswspace(part.front())) part.erase(0, 1);
    while (!part.empty() && iswspace(part.back())) part.pop_back();
    for (auto& c : part) c = wchar_t(towlower(c));
    if (part == L"ctrl" || part == L"control") { if (out.ctrl) return {}; out.ctrl = true; }
    else if (part == L"alt") { if (out.alt) return {}; out.alt = true; }
    else if (part == L"shift") { if (out.shift) return {}; out.shift = true; }
    else {
      if (out.vk) return {};
      if (part == L"space") out.vk = VK_SPACE;
      else if (part == L"tab") out.vk = VK_TAB;
      else if (part == L"enter") out.vk = VK_RETURN;
      else if (part == L"pageup") out.vk = VK_PRIOR;
      else if (part == L"pagedown") out.vk = VK_NEXT;
      else if (part.size() == 1 && ((part[0] >= L'a' && part[0] <= L'z') || (part[0] >= L'0' && part[0] <= L'9')))
        out.vk = UINT(towupper(part[0]));
      else if (part == L".") out.vk = VK_OEM_PERIOD;
      else if (part == L"/") out.vk = VK_OEM_2;
      else if (part == L";") out.vk = VK_OEM_1;
      else if (part == L"`") out.vk = VK_OEM_3;
      else if (part.size() >= 2 && part[0] == L'f') {
        int number = 0;
        for (size_t i = 1; i < part.size(); ++i) {
          if (part[i] < L'0' || part[i] > L'9') return {};
          number = number * 10 + int(part[i] - L'0');
          if (number > 24) return {};
        }
        if (number < 1) return {};
        out.vk = VK_F1 + UINT(number - 1);
      } else return {};
    }
    if (end == spec.size()) break;
    pos = end + 1;
  }
  if (!out.vk) return {};
  return out;
}

// 和 ParseKeyChord 反过来：把按下的组合键写成配置里的样子；不支持的键返回空串
inline std::wstring FormatKeyChord(const KeyChord& key) {
  std::wstring name;
  UINT vk = key.vk;
  if (vk == VK_SPACE) name = L"Space";
  else if (vk == VK_TAB) name = L"Tab";
  else if (vk == VK_RETURN) name = L"Enter";
  else if (vk == VK_PRIOR) name = L"PageUp";
  else if (vk == VK_NEXT) name = L"PageDown";
  else if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) name = std::wstring(1, wchar_t(vk));
  else if (vk == VK_OEM_PERIOD) name = L".";
  else if (vk == VK_OEM_2) name = L"/";
  else if (vk == VK_OEM_1) name = L";";
  else if (vk == VK_OEM_3) name = L"`";
  else if (vk >= VK_F1 && vk <= VK_F24) name = L"F" + std::to_wstring(vk - VK_F1 + 1);
  else return {};
  std::wstring out;
  if (key.ctrl) out += L"Ctrl+";
  if (key.alt) out += L"Alt+";
  if (key.shift) out += L"Shift+";
  return out + name;
}

inline bool SafeShortcut(const KeyChord& key, bool compose = false) {
  if (!key.vk) return false;
  if (key.alt && !key.ctrl && key.vk == VK_F4) return false;
  if (key.alt && !key.ctrl && key.vk == VK_TAB) return false;
  if (key.ctrl && key.alt && key.vk == VK_DELETE) return false;
  if (key.ctrl && key.vk == VK_SPACE && !key.alt) return false;
  if (key.ctrl || key.alt || (key.vk >= VK_F1 && key.vk <= VK_F24)) return true;
  return compose && key.vk == VK_TAB && !key.shift;
}
}  // namespace yy
