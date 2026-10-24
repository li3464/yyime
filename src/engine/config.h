#pragma once
// 设置文件 %APPDATA%\yyime\yyime.ini（UTF-8）。改完保存，下次打字就生效。
#include <cstdint>
#include <string>
#include <vector>

#include "pinyin.h"

namespace yy {

struct Theme {
  std::wstring name;
  std::wstring label;
  std::wstring artwork;
  std::wstring description;
  uint32_t background = 0xFFFFFFFF;  // ARGB
  uint32_t border = 0xFFE4E4E7;
  uint32_t text = 0xFF18181B;
  uint32_t index = 0xFFA1A1AA;
  uint32_t comment = 0xFFA1A1AA;
  uint32_t preedit = 0xFF2563EB;
  uint32_t highlightBg = 0xFF2563EB;
  uint32_t highlightText = 0xFFFFFFFF;
  uint32_t accent = 0xFF7C3AED;  // AI 候选的标记色
  float cornerRadius = 10.0f;
  float borderWidth = 1.0f;
  bool shadow = true;
};

struct Config {
  // [general]
  Scheme scheme = Scheme::Pinyin;
  int pageSize = 7;
  bool englishMixed = true;
  bool shiftToggle = true;
  bool fullwidthPunct = true;
  bool startChinese = true;
  bool learn = true;
  bool dynamicPhrases = true;  // rq 日期、sj 时间、xq 星期
  bool wubiAutoCommit = true;  // 五笔四码唯一自动上屏
  std::wstring fuzzy;          // 模糊音，比如 "z=zh c=ch s=sh n=l an=ang en=eng in=ing"
  bool debugLog = false;
  bool sentenceModel = true;
  bool emoji = true;  // 词后面跟着出 emoji
  bool rareChars = true;  // 生僻字：u 模式拆字/笔画，全拼也能打出大字表里的字
  bool correction = true;  // 全拼纠错：nihoa 也能打出“你好”
  // [ui]
  std::wstring themeName = L"auto";
  bool vertical = false;
  std::wstring fontFace = L"Microsoft YaHei UI";
  float fontSize = 17.0f;
  bool preeditInWindow = true;
  bool characterArt = true;
  // [ai]
  bool aiEnabled = true;
  std::wstring aiBaseUrl = L"https://api.deepseek.com";
  std::wstring aiKey;
  std::wstring aiModel = L"deepseek-chat";
  std::wstring aiProxy;
  int aiTimeoutMs = 20000;
  std::wstring aiComposeKey = L"Tab";
  std::wstring aiHotkey = L"Ctrl+Alt+Space";
  std::wstring translateHotkey = L"Ctrl+Alt+T";
  std::wstring settingsHotkey = L"Ctrl+Alt+O";
  std::wstring punctHotkey = L"Ctrl+.";
  std::wstring translateTarget = L"auto";

  std::vector<Theme> themes;  // 内置皮肤 + 用户自己在 [theme.xxx] 里写的

  const Theme& ResolveTheme(bool systemDark) const;
};

std::vector<Theme> BuiltinThemes();
std::wstring ThemeAssetPath(const Theme& theme);
// 在默认值上叠加文件里的设置；文件不存在也返回 true（全用默认值）
bool LoadConfig(const std::wstring& path, Config* cfg);
// 第一次运行时写出去的带注释的设置文件
std::string DefaultConfigText();
// 改一个设置（右键菜单用），保留文件里其他内容和注释
bool SaveConfigValue(const std::wstring& path, const std::string& section, const std::string& key,
                     const std::string& value);
struct ConfigChange { std::string section, key, value; };
bool SaveConfigValues(const std::wstring& path, const std::vector<ConfigChange>& changes);

}  // namespace yy
