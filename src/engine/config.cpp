#include "config.h"

#include <cstdlib>
#include <cstring>

#include "util.h"

namespace yy {

namespace {

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s) c = AsciiLower(c);
  return s;
}

bool ParseBool(const std::string& v, bool def) {
  std::string l = Lower(v);
  if (l == "true" || l == "1" || l == "yes" || l == "on" || v == "是" || v == "开") return true;
  if (l == "false" || l == "0" || l == "no" || l == "off" || v == "否" || v == "关") return false;
  return def;
}

bool ParseColor(const std::string& v, uint32_t* out) {
  std::string s = v;
  if (!s.empty() && s[0] == '#') s.erase(0, 1);
  if (s.size() != 6 && s.size() != 8) return false;
  for (char c : s)
    if (!isxdigit((unsigned char)c)) return false;
  uint32_t x = uint32_t(strtoul(s.c_str(), nullptr, 16));
  *out = s.size() == 6 ? (0xFF000000u | x) : x;
  return true;
}

Theme MakeTheme(const wchar_t* name, const wchar_t* label, uint32_t bg, uint32_t border, uint32_t text,
                uint32_t index, uint32_t preedit, uint32_t hlBg, uint32_t hlText, uint32_t accent) {
  Theme t;
  t.name = name;
  t.label = label;
  t.background = bg;
  t.border = border;
  t.text = text;
  t.index = index;
  t.comment = index;
  t.preedit = preedit;
  t.highlightBg = hlBg;
  t.highlightText = hlText;
  t.accent = accent;
  return t;
}

void ApplyThemeKey(Theme* t, const std::string& key, const std::string& value) {
  uint32_t c;
  if (key == "label") t->label = Utf8ToWide(value);
  else if (key == "background" && ParseColor(value, &c)) t->background = c;
  else if (key == "border" && ParseColor(value, &c)) t->border = c;
  else if (key == "text" && ParseColor(value, &c)) t->text = c;
  else if (key == "index" && ParseColor(value, &c)) t->index = c;
  else if (key == "comment" && ParseColor(value, &c)) t->comment = c;
  else if (key == "preedit" && ParseColor(value, &c)) t->preedit = c;
  else if (key == "highlight_bg" && ParseColor(value, &c)) t->highlightBg = c;
  else if (key == "highlight_text" && ParseColor(value, &c)) t->highlightText = c;
  else if (key == "accent" && ParseColor(value, &c)) t->accent = c;
  else if (key == "corner_radius") t->cornerRadius = float(atof(value.c_str()));
  else if (key == "border_width") t->borderWidth = float(atof(value.c_str()));
  else if (key == "shadow") t->shadow = ParseBool(value, t->shadow);
}

// 去掉值后面用空白隔开的 ; 或 # 注释
std::string StripComment(const std::string& v) {
  for (size_t i = 1; i < v.size(); ++i) {
    if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) return Trim(v.substr(0, i));
  }
  return Trim(v);
}

}  // namespace

std::vector<Theme> BuiltinThemes() {
  std::vector<Theme> v;
  v.push_back(MakeTheme(L"light", L"晴空", 0xFFFFFFFF, 0xFFE4E4E7, 0xFF18181B, 0xFFA1A1AA, 0xFF2563EB, 0xFF2563EB,
                        0xFFFFFFFF, 0xFF7C3AED));
  v.push_back(MakeTheme(L"dark", L"夜幕", 0xFF202024, 0xFF3F3F46, 0xFFF4F4F5, 0xFF71717A, 0xFF60A5FA, 0xFF3B82F6,
                        0xFFFFFFFF, 0xFFA78BFA));
  v.push_back(MakeTheme(L"sakura", L"樱花", 0xFFFFF5F7, 0xFFFBCFE8, 0xFF4A1D2F, 0xFFD08BA5, 0xFFDB2777, 0xFFEC4899,
                        0xFFFFFFFF, 0xFF9333EA));
  v.push_back(MakeTheme(L"mint", L"薄荷", 0xFFF0FDF9, 0xFFA7F3D0, 0xFF064E3B, 0xFF5FB89A, 0xFF059669, 0xFF10B981,
                        0xFFFFFFFF, 0xFF0D9488));
  v.push_back(MakeTheme(L"ink", L"水墨", 0xFFFAF8F3, 0xFFD6D3D1, 0xFF292524, 0xFFA8A29E, 0xFF57534E, 0xFF44403C,
                        0xFFFAFAF9, 0xFF92400E));
  v.push_back(MakeTheme(L"violet", L"星紫", 0xFF1E1B2E, 0xFF3B3558, 0xFFEDE9FE, 0xFF8B85B0, 0xFFC4B5FD, 0xFF7C3AED,
                        0xFFFFFFFF, 0xFFF0ABFC));
  v.push_back(MakeTheme(L"amber", L"琥珀", 0xFFFFFBEB, 0xFFFDE68A, 0xFF451A03, 0xFFB45309, 0xFFD97706, 0xFFF59E0B,
                        0xFF451A03, 0xFFB91C1C));
  return v;
}

const Theme& Config::ResolveTheme(bool systemDark) const {
  std::wstring want = themeName;
  if (want == L"auto") want = systemDark ? L"dark" : L"light";
  for (const Theme& t : themes)
    if (t.name == want) return t;
  static const Theme fallback;
  return themes.empty() ? fallback : themes.front();
}

bool LoadConfig(const std::wstring& path, Config* cfg) {
  cfg->themes = BuiltinThemes();
  std::string data;
  if (!ReadTextFile(path, &data)) return !FileExists(path);
  std::string section;
  size_t pos = 0;
  while (pos <= data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) nl = data.size();
    std::string line = Trim(data.substr(pos, nl - pos));
    pos = nl + 1;
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line[0] == '[') {
      size_t close = line.find(']');
      section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = Lower(Trim(line.substr(0, eq)));
    std::string value = StripComment(line.substr(eq + 1));
    std::wstring wv = Utf8ToWide(value);
    if (section == "general") {
      if (key == "scheme") cfg->scheme = SchemeFromName(Utf8ToWide(Lower(value)));
      else if (key == "page_size") cfg->pageSize = std::max(3, std::min(9, atoi(value.c_str())));
      else if (key == "english_mixed") cfg->englishMixed = ParseBool(value, cfg->englishMixed);
      else if (key == "shift_toggle") cfg->shiftToggle = ParseBool(value, cfg->shiftToggle);
      else if (key == "fullwidth_punct") cfg->fullwidthPunct = ParseBool(value, cfg->fullwidthPunct);
      else if (key == "start_chinese") cfg->startChinese = ParseBool(value, cfg->startChinese);
      else if (key == "learn") cfg->learn = ParseBool(value, cfg->learn);
      else if (key == "dynamic_phrases") cfg->dynamicPhrases = ParseBool(value, cfg->dynamicPhrases);
      else if (key == "wubi_auto_commit") cfg->wubiAutoCommit = ParseBool(value, cfg->wubiAutoCommit);
      else if (key == "fuzzy") cfg->fuzzy = wv;
      else if (key == "debug_log") cfg->debugLog = ParseBool(value, cfg->debugLog);
    } else if (section == "ui") {
      if (key == "theme") cfg->themeName = Utf8ToWide(Lower(value));
      else if (key == "layout") cfg->vertical = Lower(value) == "vertical" || value == "竖排";
      else if (key == "font") cfg->fontFace = wv;
      else if (key == "font_size") cfg->fontSize = std::max(10.0f, std::min(40.0f, float(atof(value.c_str()))));
      else if (key == "preedit_in_window") cfg->preeditInWindow = ParseBool(value, cfg->preeditInWindow);
    } else if (section == "ai") {
      if (key == "enabled") cfg->aiEnabled = ParseBool(value, cfg->aiEnabled);
      else if (key == "base_url") cfg->aiBaseUrl = wv;
      else if (key == "api_key") cfg->aiKey = wv;
      else if (key == "model") cfg->aiModel = wv;
      else if (key == "proxy") cfg->aiProxy = wv;
      else if (key == "timeout_ms") cfg->aiTimeoutMs = std::max(3000, atoi(value.c_str()));
      else if (key == "compose_key") cfg->aiComposeKey = wv;
      else if (key == "hotkey") cfg->aiHotkey = wv;
    } else if (section.rfind("theme.", 0) == 0 && section.size() > 6) {
      std::wstring name = Utf8ToWide(section.substr(6));
      Theme* t = nullptr;
      for (Theme& x : cfg->themes)
        if (x.name == name) t = &x;
      if (!t) {
        cfg->themes.push_back(cfg->themes.front());
        t = &cfg->themes.back();
        t->name = name;
        t->label = name;
      }
      ApplyThemeKey(t, key, value);
    }
  }
  return true;
}

bool SaveConfigValue(const std::wstring& path, const std::string& section, const std::string& key,
                     const std::string& value) {
  std::string data;
  if (!ReadTextFile(path, &data)) data = DefaultConfigText();
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) nl = data.size();
    std::string l = data.substr(pos, nl - pos);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lines.push_back(l);
    pos = nl + 1;
  }
  std::string cur;
  int sectionEnd = -1;  // 这一节最后一个非空行
  bool inSection = false, done = false;
  for (size_t i = 0; i < lines.size() && !done; ++i) {
    std::string t = Trim(lines[i]);
    if (!t.empty() && t[0] == '[') {
      size_t close = t.find(']');
      cur = Lower(Trim(t.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
      inSection = cur == section;
      if (inSection) sectionEnd = int(i);
      continue;
    }
    if (!inSection) continue;
    if (!t.empty()) sectionEnd = int(i);
    size_t eq = t.find('=');
    if (eq != std::string::npos && t[0] != ';' && t[0] != '#' && Lower(Trim(t.substr(0, eq))) == key) {
      lines[i] = key + " = " + value;
      done = true;
    }
  }
  if (!done) {
    if (sectionEnd >= 0) {
      lines.insert(lines.begin() + sectionEnd + 1, key + " = " + value);
    } else {
      lines.push_back("");
      lines.push_back("[" + section + "]");
      lines.push_back(key + " = " + value);
    }
  }
  std::string out;
  for (auto& l : lines) out += l + "\r\n";
  return WriteTextFile(path, out);
}

}  // namespace yy
