#pragma once
// 引擎和 TSF 两边都用的小工具：编码转换、文件、日志。
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace yy {

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);

// 读整个文件（UTF-8 文本），失败返回 false。会去掉开头的 BOM。
bool ReadTextFile(const std::wstring& path, std::string* out);
// 追加写（多个进程同时追加也安全：每次一个 WriteFile）。
bool AppendTextFile(const std::wstring& path, std::string_view data);
bool WriteTextFile(const std::wstring& path, std::string_view data);

// 文件的“版本”：大小和修改时间拼起来，用来判断要不要重新加载。不存在返回 0。
uint64_t FileStamp(const std::wstring& path);
bool FileExists(const std::wstring& path);
std::wstring DirName(const std::wstring& path);

// %APPDATA%\yyime（不存在会建）。
std::wstring UserDataDir();

// 调试日志，写到 %TEMP%\yyime.log；只有开了才写。
void SetLogEnabled(bool on);
void Log(const wchar_t* fmt, ...);

inline bool IsAsciiLower(char c) { return c >= 'a' && c <= 'z'; }
inline bool IsAsciiUpper(char c) { return c >= 'A' && c <= 'Z'; }
inline bool IsAsciiAlpha(char c) { return IsAsciiLower(c) || IsAsciiUpper(c); }
inline char AsciiLower(char c) { return IsAsciiUpper(c) ? char(c - 'A' + 'a') : c; }

}  // namespace yy
