#include "util.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace yy {

std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}

std::string WideToUtf8(std::wstring_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

bool ReadTextFile(const std::wstring& path, std::string* out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (64 << 20);
  if (ok) {
    out->resize(size_t(size.QuadPart));
    DWORD got = 0;
    ok = out->empty() || (ReadFile(f, out->data(), DWORD(out->size()), &got, nullptr) && got == out->size());
  }
  CloseHandle(f);
  if (ok && out->size() >= 3 && (unsigned char)(*out)[0] == 0xEF && (unsigned char)(*out)[1] == 0xBB &&
      (unsigned char)(*out)[2] == 0xBF) {
    out->erase(0, 3);
  }
  return ok;
}

static bool WriteWithMode(const std::wstring& path, std::string_view data, DWORD access, DWORD disposition) {
  HANDLE f = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = data.empty() || (WriteFile(f, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size());
  CloseHandle(f);
  return ok;
}

bool AppendTextFile(const std::wstring& path, std::string_view data) {
  return WriteWithMode(path, data, FILE_APPEND_DATA, OPEN_ALWAYS);
}

bool WriteTextFile(const std::wstring& path, std::string_view data) {
  return WriteWithMode(path, data, GENERIC_WRITE, CREATE_ALWAYS);
}

uint64_t FileStamp(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA fad{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
  uint64_t t = (uint64_t(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
  uint64_t size = (uint64_t(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
  return (t ^ (size * 0x9E3779B97F4A7C15ull)) | 1;
}

bool FileExists(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring DirName(const std::wstring& path) {
  size_t p = path.find_last_of(L"\\/");
  return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring UserDataDir() {
  static std::wstring dir = [] {
    std::wstring d;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) {
      d = p;
      CoTaskMemFree(p);
    } else {
      wchar_t buf[MAX_PATH];
      DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
      if (n > 0 && n < MAX_PATH) d = buf;
    }
    if (!d.empty()) {
      d += L"\\yyime";
      CreateDirectoryW(d.c_str(), nullptr);
    }
    return d;
  }();
  return dir;
}

static bool g_logEnabled = false;
static std::mutex g_logMutex;

void SetLogEnabled(bool on) { g_logEnabled = on; }

void Log(const wchar_t* fmt, ...) {
  if (!g_logEnabled) return;
  wchar_t msg[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(msg, 1023, fmt, ap);
  va_end(ap);
  msg[1023] = 0;
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t line[1200];
  _snwprintf(line, 1199, L"%02d:%02d:%02d.%03d [%lu] %ls\r\n", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds, GetCurrentProcessId(), msg);
  line[1199] = 0;
  OutputDebugStringW(line);
  wchar_t tmp[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, tmp);
  if (n == 0 || n >= MAX_PATH) return;
  std::lock_guard<std::mutex> lock(g_logMutex);
  AppendTextFile(std::wstring(tmp) + L"yyime.log", WideToUtf8(line));
}

}  // namespace yy
