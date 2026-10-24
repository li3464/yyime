#include "engine.h"

#include "util.h"

namespace yy {

Engine& Engine::Get() {
  static Engine e;
  return e;
}

Engine::Engine() { InitializeSRWLock(&lock_); }

static std::wstring ModuleDir() {
  HMODULE m = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ModuleDir), &m);
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(m, path, MAX_PATH);
  return DirName(path);
}

void Engine::SetDirs(const std::wstring& dataDir, const std::wstring& userDir) {
  AcquireSRWLockExclusive(&lock_);
  dataDir_ = dataDir;
  userDir_ = userDir;
  ReleaseSRWLockExclusive(&lock_);
}

void Engine::EnsureInit() {
  AcquireSRWLockExclusive(&lock_);
  if (!inited_) {
    inited_ = true;
    if (dataDir_.empty()) {
      wchar_t buf[MAX_PATH];
      DWORD n = GetEnvironmentVariableW(L"YYIME_DATA_DIR", buf, MAX_PATH);
      if (n > 0 && n < MAX_PATH) dataDir_ = buf;
    }
    if (dataDir_.empty()) {
      std::wstring m = ModuleDir();
      for (const std::wstring& dir : {m + L"\\data", DirName(m) + L"\\data"}) {
        if (FileExists(dir + L"\\pinyin.bin")) {
          dataDir_ = dir;
          break;
        }
      }
      if (dataDir_.empty()) dataDir_ = m + L"\\data";
    }
    if (userDir_.empty()) {
      // 测试或便携使用：用环境变量 YYIME_USER_DIR 指定用户目录，不动真正的设置和词库
      wchar_t buf[MAX_PATH];
      DWORD n = GetEnvironmentVariableW(L"YYIME_USER_DIR", buf, MAX_PATH);
      if (n > 0 && n < MAX_PATH) userDir_ = buf;
    }
    if (userDir_.empty()) userDir_ = UserDataDir();
    if (!userDir_.empty()) {
      CreateDirectoryW(userDir_.c_str(), nullptr);
      configPath_ = userDir_ + L"\\yyime.ini";
      if (!FileExists(configPath_)) WriteTextFile(configPath_, DefaultConfigText());
    }
    LoadConfigLocked();
    dicts_.pinyin.Load(dataDir_ + L"\\pinyin.bin");
    dicts_.wubi.Load(dataDir_ + L"\\wubi86.bin");
    dicts_.english.Load(dataDir_ + L"\\english.bin");
    dicts_.gram.Load(dataDir_ + L"\\zh-hans-t-essay-bgw.gram");
    dicts_.table.Build(dicts_.pinyin);
    dicts_.xiaohe.Build(Scheme::Xiaohe, dicts_.table);
    dicts_.ziranma.Build(Scheme::Ziranma, dicts_.table);
    dicts_.mspy.Build(Scheme::Mspy, dicts_.table);
    Log(L"引擎初始化：词库 %ls，拼音 %d 音节，五笔 %d，英文 %d", dataDir_.c_str(), dicts_.table.size(),
        int(dicts_.wubi.loaded()), int(dicts_.english.loaded()));
  }
  ReleaseSRWLockExclusive(&lock_);
  // 下面两个有自己的锁，放在外面初始化
  std::call_once(userOnce_, [this] {
    user_.Init(userDir_.empty() ? std::wstring() : userDir_ + L"\\user_dict.txt", &dicts_.pinyin, &dicts_.table);
    phrases_.Init(userDir_.empty() ? std::wstring() : userDir_ + L"\\custom_phrases.txt");
  });
}

void Engine::LoadConfigLocked() {
  auto cfg = std::make_shared<Config>();
  if (!configPath_.empty()) LoadConfig(configPath_, cfg.get());
  else cfg->themes = BuiltinThemes();
  configStamp_ = configPath_.empty() ? 0 : FileStamp(configPath_);
  configCheckedAt_ = GetTickCount();
  SetLogEnabled(cfg->debugLog);
  config_ = std::move(cfg);
}

std::shared_ptr<const Config> Engine::config() {
  AcquireSRWLockExclusive(&lock_);
  if (!config_) LoadConfigLocked();
  DWORD now = GetTickCount();
  if (now - configCheckedAt_ > 1000) {
    configCheckedAt_ = now;
    if (!configPath_.empty() && FileStamp(configPath_) != configStamp_) LoadConfigLocked();
  }
  auto c = config_;
  ReleaseSRWLockExclusive(&lock_);
  return c;
}

void Engine::ReloadConfig() {
  AcquireSRWLockExclusive(&lock_);
  LoadConfigLocked();
  ReleaseSRWLockExclusive(&lock_);
}

std::string Engine::WubiCodeOf(std::wstring_view text) {
  AcquireSRWLockExclusive(&lock_);
  if (!reverseBuilt_ && dicts_.wubi.loaded()) {
    reverseBuilt_ = true;
    auto [b, e] = dicts_.wubi.PrefixRange("");
    for (auto p = b; p != e; ++p) {
      std::string code(WubiDict::codeOf(*p));
      std::string& cur = wubiReverse_[std::wstring(dicts_.wubi.textOf(*p))];
      if (code.size() > cur.size()) cur = code;  // 留全码
    }
  }
  std::string out;
  auto it = wubiReverse_.find(std::wstring(text));
  if (it != wubiReverse_.end()) out = it->second;
  ReleaseSRWLockExclusive(&lock_);
  return out;
}

}  // namespace yy
