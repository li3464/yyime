#include "ai_client.h"

#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "../engine/util.h"

namespace yy {

namespace {

// ---------------------------------------------------------------- 迷你 JSON

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;

  const Json* get(std::string_view key) const {
    if (type != Object) return nullptr;
    for (auto& [k, v] : obj)
      if (k == key) return &v;
    return nullptr;
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view s) : s_(s) {}
  bool Parse(Json* out) {
    if (!Value(out, 0)) return false;
    Ws();
    return true;
  }

 private:
  void Ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
  }
  bool Lit(std::string_view w) {
    if (s_.substr(i_, w.size()) != w) return false;
    i_ += w.size();
    return true;
  }
  static void PutUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
      o += char(cp);
    } else if (cp < 0x800) {
      o += char(0xC0 | (cp >> 6));
      o += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      o += char(0xE0 | (cp >> 12));
      o += char(0x80 | ((cp >> 6) & 0x3F));
      o += char(0x80 | (cp & 0x3F));
    } else {
      o += char(0xF0 | (cp >> 18));
      o += char(0x80 | ((cp >> 12) & 0x3F));
      o += char(0x80 | ((cp >> 6) & 0x3F));
      o += char(0x80 | (cp & 0x3F));
    }
  }
  bool Hex4(uint32_t* v) {
    if (i_ + 4 > s_.size()) return false;
    *v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s_[i_++];
      *v <<= 4;
      if (c >= '0' && c <= '9') *v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') *v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') *v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool Str(std::string* out) {
    if (i_ >= s_.size() || s_[i_] != '"') return false;
    ++i_;
    while (i_ < s_.size()) {
      char c = s_[i_++];
      if (c == '"') return true;
      if (c != '\\') {
        *out += c;
        continue;
      }
      if (i_ >= s_.size()) return false;
      char e = s_[i_++];
      switch (e) {
        case '"': *out += '"'; break;
        case '\\': *out += '\\'; break;
        case '/': *out += '/'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'n': *out += '\n'; break;
        case 'r': *out += '\r'; break;
        case 't': *out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!Hex4(&cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
            i_ += 2;
            uint32_t lo;
            if (!Hex4(&lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          PutUtf8(*out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }
  bool Value(Json* v, int depth) {
    if (depth > 32) return false;
    Ws();
    if (i_ >= s_.size()) return false;
    char c = s_[i_];
    if (c == '{') {
      v->type = Json::Object;
      ++i_;
      Ws();
      if (i_ < s_.size() && s_[i_] == '}') {
        ++i_;
        return true;
      }
      while (true) {
        Ws();
        std::string key;
        if (!Str(&key)) return false;
        Ws();
        if (i_ >= s_.size() || s_[i_] != ':') return false;
        ++i_;
        Json child;
        if (!Value(&child, depth + 1)) return false;
        v->obj.emplace_back(std::move(key), std::move(child));
        Ws();
        if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
        return false;
      }
    }
    if (c == '[') {
      v->type = Json::Array;
      ++i_;
      Ws();
      if (i_ < s_.size() && s_[i_] == ']') {
        ++i_;
        return true;
      }
      while (true) {
        Json child;
        if (!Value(&child, depth + 1)) return false;
        v->arr.push_back(std::move(child));
        Ws();
        if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
        return false;
      }
    }
    if (c == '"') {
      v->type = Json::String;
      return Str(&v->str);
    }
    if (Lit("true")) { v->type = Json::Bool; v->b = true; return true; }
    if (Lit("false")) { v->type = Json::Bool; return true; }
    if (Lit("null")) return true;
    size_t start = i_;
    while (i_ < s_.size() && (strchr("+-0123456789.eE", s_[i_]) != nullptr)) ++i_;
    if (i_ == start) return false;
    v->type = Json::Number;
    v->num = atof(std::string(s_.substr(start, i_ - start)).c_str());
    return true;
  }

  std::string_view s_;
  size_t i_ = 0;
};

void JsonEscape(std::string& out, std::string_view s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += char(c);
        }
    }
  }
  out += '"';
}

std::string Trim(std::string s) {
  auto sp = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
  while (!s.empty() && sp(s.back())) s.pop_back();
  size_t b = 0;
  while (b < s.size() && sp(s[b])) ++b;
  return s.substr(b);
}

// ---------------------------------------------------------------- 提示词

const char* kComposePrompt =
    "你是中文输入法里的 AI 联想功能。用户正在用拼音打字，你要猜出他想打的中文。\n"
    "规则：\n"
    "1. 第一个候选必须是这串拼音最可能对应的中文：字数和音节对得上，结合前文挑最合理的词。\n"
    "2. 再给 2 到 3 个候选：可以是同一串拼音的其他合理写法，也可以在第一个候选后面自然地多接几个字。\n"
    "3. 每个候选不超过 30 个字，不要编号，不要解释。\n"
    "只输出 JSON，格式：{\"candidates\": [\"...\", \"...\"]}";

const char* kComposeNoPinyinPrompt =
    "你是中文输入法里的 AI 联想功能。用户已经打出了一段字，请在它后面自然地接着写。\n"
    "给 3 个候选，每个都以用户已经打出的字开头，再往后多写几个字到一句话，不超过 30 个字，不要解释。\n"
    "只输出 JSON，格式：{\"candidates\": [\"...\", \"...\", \"...\"]}";

const char* kPolishPrompt =
    "你是中文写作助手。用户选中了一段文字，请帮他改一改。\n"
    "给 3 个版本：\n"
    "1. 润色：改正错别字和病句，让表达更通顺，尽量贴近原文；\n"
    "2. 更正式、书面一些；\n"
    "3. 更口语、更简洁一些。\n"
    "保持原意，不要加原文没有的信息，长度和原文差不多。原文是英文就用英文改。\n"
    "只输出 JSON，格式：{\"candidates\": [\"版本1\", \"版本2\", \"版本3\"]}";

const char* kContinuePrompt =
    "你是中文写作助手。根据用户光标前面的文字，帮他接着往下写。\n"
    "给 3 个不同方向的续写，每个一两句话、不超过 50 个字，语气和前文一致，直接接在前文后面能读通。\n"
    "只写续写的部分，不要重复前文，不要解释。\n"
    "只输出 JSON，格式：{\"candidates\": [\"...\", \"...\", \"...\"]}";

}  // namespace

std::string BuildAiBody(const AiRequest& req) {
  std::string system, user;
  double temperature = 0.3;
  int maxTokens = 300;
  std::string ctx = WideToUtf8(req.context);
  switch (req.mode) {
    case AiMode::Polish:
      system = kPolishPrompt;
      user = WideToUtf8(req.selection);
      if (!ctx.empty()) user = "（前文供参考：「" + ctx + "」）\n要改的文字：\n" + user;
      temperature = 0.7;
      maxTokens = 1500;
      break;
    case AiMode::Continue:
      system = kContinuePrompt;
      user = "前文：「" + ctx + "」";
      temperature = 0.9;
      maxTokens = 500;
      break;
    default:
      if (req.pinyin.empty()) {
        system = kComposeNoPinyinPrompt;
        user = "前文：「" + ctx + "」\n已经打出的：" + WideToUtf8(req.guess);
      } else {
        system = kComposePrompt;
        user = "前文：" + (ctx.empty() ? std::string("（没有）") : "「" + ctx + "」") + "\n拼音：" + req.pinyin +
               "\n输入法自己的猜测：" + WideToUtf8(req.guess);
      }
      break;
  }
  std::string body = "{\"model\":";
  JsonEscape(body, WideToUtf8(req.model));
  body += ",\"messages\":[{\"role\":\"system\",\"content\":";
  JsonEscape(body, system);
  body += "},{\"role\":\"user\",\"content\":";
  JsonEscape(body, user);
  char tail[160];
  snprintf(tail, sizeof(tail),
           "}],\"temperature\":%.1f,\"max_tokens\":%d,\"stream\":false,\"response_format\":{\"type\":\"json_object\"}}",
           temperature, maxTokens);
  body += tail;
  return body;
}

bool ParseAiReply(const std::string& httpBody, AiResponse* out) {
  Json root;
  if (!JsonParser(httpBody).Parse(&root)) {
    out->error = L"返回的内容看不懂";
    return false;
  }
  if (const Json* err = root.get("error")) {
    const Json* msg = err->get("message");
    out->error = Utf8ToWide(msg && msg->type == Json::String ? msg->str : std::string("接口报错"));
    return false;
  }
  const Json* choices = root.get("choices");
  if (!choices || choices->type != Json::Array || choices->arr.empty()) {
    out->error = L"返回里没有结果";
    return false;
  }
  const Json* message = choices->arr[0].get("message");
  const Json* content = message ? message->get("content") : nullptr;
  if (!content || content->type != Json::String) {
    out->error = L"返回里没有结果";
    return false;
  }
  std::string text = content->str;
  std::vector<std::string> items;
  size_t l = text.find('{'), r = text.rfind('}');
  Json inner;
  if (l != std::string::npos && r != std::string::npos && r > l &&
      JsonParser(std::string_view(text).substr(l, r - l + 1)).Parse(&inner)) {
    const Json* c = inner.get("candidates");
    if (c && c->type == Json::Array) {
      for (auto& x : c->arr)
        if (x.type == Json::String) items.push_back(x.str);
    }
  }
  if (items.empty()) {
    // 不是 JSON：按行拆，去掉编号和引号
    size_t pos = 0;
    while (pos < text.size()) {
      size_t nl = text.find('\n', pos);
      if (nl == std::string::npos) nl = text.size();
      std::string line = Trim(text.substr(pos, nl - pos));
      pos = nl + 1;
      size_t k = 0;
      while (k < line.size() && ((line[k] >= '0' && line[k] <= '9') || line[k] == '.' || line[k] == ')' ||
                                 line[k] == '-' || line[k] == ' '))
        ++k;
      line = Trim(line.substr(k));
      if (line.size() >= 2 && line.front() == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
      if (!line.empty() && line[0] != '{' && line[0] != '}' && line[0] != '`') items.push_back(line);
    }
  }
  for (auto& s : items) {
    std::wstring w = Utf8ToWide(Trim(s));
    if (w.empty()) continue;
    bool dup = false;
    for (auto& t : out->texts) dup = dup || t == w;
    if (!dup) out->texts.push_back(std::move(w));
    if (out->texts.size() >= 5) break;
  }
  return !out->texts.empty();
}

static std::wstring WinHttpErrorText(DWORD e) {
  switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return L"等太久没回应（超时）";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return L"连不上（找不到服务器地址，看看网络）";
    case ERROR_WINHTTP_CANNOT_CONNECT: return L"连不上服务器（如果设了代理，看看代理开着没有）";
    case ERROR_WINHTTP_CONNECTION_ERROR: return L"连接断了";
    case ERROR_WINHTTP_SECURE_FAILURE: return L"安全连接失败";
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return L"base_url 写得不对";
    default: {
      wchar_t buf[48];
      _snwprintf(buf, 47, L"网络错误 %lu", e);
      buf[47] = 0;
      return buf;
    }
  }
}

AiResponse RunAiRequestBlocking(const AiRequest& req) {
  AiResponse resp;
  std::wstring url = req.baseUrl;
  while (!url.empty() && (url.back() == L'/' || url.back() == L' ')) url.pop_back();
  if (url.size() < 17 || url.compare(url.size() - 17, 17, L"/chat/completions") != 0) url += L"/chat/completions";

  wchar_t host[256] = {}, path[2048] = {};
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;
  uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2047;
  uc.dwExtraInfoLength = 1;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
    resp.error = L"base_url 写得不对";
    return resp;
  }

  DWORD access = WINHTTP_ACCESS_TYPE_NO_PROXY;
  std::wstring proxy = req.proxy;
  if (proxy == L"system") {
    access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    proxy.clear();
  } else if (!proxy.empty()) {
    access = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    for (const wchar_t* p : {L"http://", L"https://"})
      if (proxy.rfind(p, 0) == 0) proxy = proxy.substr(wcslen(p));
    while (!proxy.empty() && proxy.back() == L'/') proxy.pop_back();
  }
  HINTERNET session = WinHttpOpen(L"yyime/1.0", access, proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxy.c_str(),
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session && access == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY) {  // 老系统没有 AUTOMATIC
    session = WinHttpOpen(L"yyime/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session) {
    resp.error = WinHttpErrorText(GetLastError());
    return resp;
  }
  int t = req.timeoutMs;
  WinHttpSetTimeouts(session, t, t, t, t);
  HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
  HINTERNET request = conn ? WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                           : nullptr;
  std::string body = BuildAiBody(req);
  std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + req.apiKey + L"\r\n";
  std::string reply;
  DWORD status = 0;
  bool ok = request && WinHttpSendRequest(request, headers.c_str(), DWORD(-1), body.data(), DWORD(body.size()),
                                          DWORD(body.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr);
  if (ok) {
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    while (true) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
      size_t old = reply.size();
      reply.resize(old + avail);
      DWORD got = 0;
      if (!WinHttpReadData(request, reply.data() + old, avail, &got)) break;
      reply.resize(old + got);
      if (reply.size() > (4u << 20)) break;
    }
  } else {
    resp.error = WinHttpErrorText(GetLastError());
  }
  if (request) WinHttpCloseHandle(request);
  if (conn) WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  if (!ok) return resp;

  if (status == 200) {
    ParseAiReply(reply, &resp);
    return resp;
  }
  AiResponse err;
  ParseAiReply(reply, &err);
  wchar_t code[32];
  _snwprintf(code, 31, L"HTTP %lu", status);
  code[31] = 0;
  if (status == 401) resp.error = L"API key 不对（401）";
  else if (status == 402) resp.error = L"账户余额不足（402）";
  else if (status == 429) resp.error = L"请求太频繁或额度用完了（429）";
  else resp.error = std::wstring(code) + (err.error.empty() ? L"" : L"：" + err.error);
  return resp;
}

namespace {

struct Job {
  AiRequest req;
  HWND notify;
  UINT msg;
  int id;
  HMODULE module;
};

DWORD WINAPI AiThread(LPVOID p) {
  Job* job = static_cast<Job*>(p);
  HMODULE module = job->module;
  {
    auto* resp = new AiResponse(RunAiRequestBlocking(job->req));
    if (!PostMessageW(job->notify, job->msg, WPARAM(job->id), reinterpret_cast<LPARAM>(resp))) delete resp;
    delete job;
  }
  // 线程拿着一份 DLL 引用，跑完再放，这样中途 DLL 不会被卸载
  FreeLibraryAndExitThread(module, 0);
}

}  // namespace

bool StartAiRequest(const AiRequest& req, HWND notify, UINT msg, int requestId) {
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&AiThread), &self)) {
    return false;
  }
  auto* job = new Job{req, notify, msg, requestId, self};
  HANDLE t = CreateThread(nullptr, 0, AiThread, job, 0, nullptr);
  if (!t) {
    delete job;
    FreeLibrary(self);
    return false;
  }
  CloseHandle(t);
  return true;
}

}  // namespace yy
