#pragma once
// 每个进程一份的全局数据：词库、用户词库、自定义短语、设置。
#include <windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "converter.h"
#include "extras.h"
#include "phrases.h"
#include "userdict.h"

namespace yy {

class Engine {
 public:
  static Engine& Get();

  // 测试程序用：指定词库目录和用户目录（不调用就按 DLL 所在位置找）
  void SetDirs(const std::wstring& dataDir, const std::wstring& userDir);
  // 加载词库和设置，只会真正做一次
  void EnsureInit();

  const Dictionaries& dicts() const { return dicts_; }
  UserDict& user() { return user_; }
  CustomPhrases& phrases() { return phrases_; }

  // 当前设置；离上次检查超过 1 秒就看一眼文件改没改
  std::shared_ptr<const Config> config();
  void ReloadConfig();
  const std::wstring& configPath() const { return configPath_; }
  const std::wstring& userDir() const { return userDir_; }
  const std::wstring& dataDir() const { return dataDir_; }

  // 五笔反查：词 → 五笔编码（第一次用时才建表）
  std::string WubiCodeOf(std::wstring_view text);
  // 词 → emoji（data\emoji.txt，第一次用时才读）；没有返回 nullptr
  const std::vector<std::wstring>* EmojiFor(const std::wstring& word);
  // 生僻字的拆字表、笔画表（data\chaizi.txt、stroke.txt，第一次进 u 模式时才读）；没有文件返回 nullptr
  const CodeTable* ChaiziTable();
  const CodeTable* StrokeTable();

 private:
  void LoadRareTables();
  Engine();
  void LoadConfigLocked();

  SRWLOCK lock_;
  std::once_flag userOnce_;
  bool inited_ = false;
  std::wstring dataDir_, userDir_, configPath_;
  Dictionaries dicts_;
  UserDict user_;
  CustomPhrases phrases_;
  std::shared_ptr<const Config> config_;
  uint64_t configStamp_ = 0;
  DWORD configCheckedAt_ = 0;
  bool reverseBuilt_ = false;
  std::unordered_map<std::wstring, std::string> wubiReverse_;
  std::once_flag emojiOnce_;
  std::unordered_map<std::wstring, std::vector<std::wstring>> emoji_;
  std::once_flag rareOnce_;
  CodeTable chaizi_, stroke_;
};

}  // namespace yy
