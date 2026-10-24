#pragma once
// AI 补全/润色：在后台线程调 OpenAI 兼容的 chat/completions 接口（默认 DeepSeek）。
#include <windows.h>

#include <string>
#include <vector>

#include "../engine/session.h"

namespace yy {

struct AiRequest {
  AiMode mode = AiMode::Compose;
  std::string pinyin;      // 打字中：剩下的拼音（全拼，音节用 ' 隔开）
  std::wstring guess;      // 打字中：输入法自己给的第一个候选
  std::wstring context;    // 光标前面的文字
  std::wstring selection;  // 选中的文字（润色）
  std::wstring targetLanguage = L"auto";
  std::wstring baseUrl, apiKey, model, proxy;
  int timeoutMs = 20000;
};

struct AiResponse {
  std::vector<std::wstring> texts;
  std::wstring error;
};

// 开一个后台线程发请求，做完以后 PostMessage(notify, msg, requestId, (LPARAM)new AiResponse)。
// 收消息的一方负责 delete。
bool StartAiRequest(const AiRequest& req, HWND notify, UINT msg, int requestId);

// 下面几个给测试程序用
std::string BuildAiBody(const AiRequest& req);
bool ParseAiReply(const std::string& httpBody, AiResponse* out);
AiResponse RunAiRequestBlocking(const AiRequest& req);

}  // namespace yy
