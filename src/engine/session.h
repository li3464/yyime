#pragma once
// 一次“打字过程”的状态机：收按键，决定要不要吃掉这个键、上屏什么字、候选怎么变。
// 跟 TSF 无关，测试程序直接喂按键就能测。
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "hotkey.h"
#include "converter.h"

namespace yy {

struct KeyEvent {
  unsigned vk = 0;
  wchar_t ch = 0;  // 按美式键盘算这个键打出来的字符，没有就是 0
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool caps = false;
};

struct SessionResult {
  bool eaten = false;
  std::wstring commit;       // 要上屏的字
  bool changed = false;      // 预编辑或候选变了
  bool modeChanged = false;  // 中英切换了
  bool aiRequested = false;  // 要发 AI 请求
};

enum class AiMode { None, Compose, Polish, Continue, Translate };

class InputSession {
 public:
  InputSession();
  void ApplyConfig(const Config& cfg);
  bool chinese() const { return chinese_; }
  void SetChinese(bool on);
  Scheme scheme() const { return scheme_; }

  bool TestKeyDown(const KeyEvent& k);
  SessionResult OnKeyDown(const KeyEvent& k);
  SessionResult OnShiftTap();       // 受 shift_toggle 设置控制
  SessionResult ToggleChinese();    // 点按钮切换：没打完的字母原样上屏
  SessionResult ClickCandidate(int indexOnPage);
  void Clear();
  std::wstring RawText() const;     // 已选的字 + 剩下的原样字母（不带分隔符）

  bool composing() const { return !input_.empty(); }
  // 候选框有东西可显示（打字中，或者 AI 面板开着）
  bool panelVisible() const { return composing() || aiPanel_; }
  bool aiPanel() const { return aiPanel_; }

  std::wstring Preedit() const;  // 写进文档里的那串
  std::wstring AuxText() const;  // 候选框顶上那一行
  struct View {
    std::vector<const Candidate*> items;
    int highlight = 0;
    bool hasPrev = false;
    bool hasNext = false;
    std::wstring status;  // AI 状态或者提示
  };
  View PageView() const;

  // ---- AI
  // 打字中按了 AI 键：返回请求编号，调用方拿 AiPinyin()/上下文 去请求
  int BeginAiCompose();
  // 没打字时按了 AI 热键：开一个 AI 面板（润色或续写）
  int BeginAiPanel(AiMode mode);
  void SetAiResult(int requestId, const std::vector<std::wstring>& texts, const std::wstring& status);
  AiMode aiMode() const { return aiMode_; }
  std::string AiPinyin() const;           // 剩下没选的部分转成全拼
  std::wstring AiBestGuess() const;       // 引擎自己给的第一个候选
  std::wstring SelectedText() const;      // 已经选好的部分

 private:
  struct Segment {
    std::wstring text;
    int inputLen = 0;
    std::vector<uint16_t> syls;
    CandKind kind = CandKind::Word;
  };

  void StartWith(char c);
  void Refresh();
  std::string Rest() const { return input_.substr(size_t(consumed_)); }
  std::string StateKey() const;
  SessionResult Select(size_t absIndex);
  SessionResult PickChar(bool last);  // 以词定字：取高亮词的第一个/最后一个字
  bool VMode() const;                 // 全拼下 v 开头：数字、计算、符号
  void FillVMode(const std::string& rest);
  bool UMode() const;                 // 全拼下 u 开头：生僻字（拆字、笔画）
  void FillUMode(const std::string& rest);
  void AddEmoji(size_t restLen);
  SessionResult CommitRaw();
  void Backspace();
  void Learn(const Candidate& last);
  void MoveHighlight(int delta);
  void PageBy(int delta);
  std::wstring MapPunct(wchar_t ch);
  bool IsPunctKey(wchar_t ch) const;
  int FullCandidateIndex() const;
  SessionResult OnAiPanelKey(const KeyEvent& k);

  // 设置
  Scheme scheme_ = Scheme::Pinyin;
  int pageSize_ = 7;
  bool englishMixed_ = true, shiftToggle_ = true, fullwidthPunct_ = true, learn_ = true, dynamic_ = true;
  bool wubiAutoCommit_ = true, aiEnabled_ = true;
  bool sentenceModel_ = true;
  bool emoji_ = true;
  bool rare_ = true;
  bool correction_ = true;
  std::wstring hint_;  // v 模式之类的提示，显示在候选框顶上那行右边
  KeyChord composeKey_ = ParseKeyChord(L"Tab");
  FuzzyRules fuzzy_;

  // 状态
  bool chinese_ = true;
  std::string input_;
  int consumed_ = 0;
  std::vector<Segment> selected_;
  Conversion conv_;
  std::vector<Candidate> cands_;
  size_t highlight_ = 0;
  bool lastDigit_ = false;
  bool dquoteOpen_ = false, squoteOpen_ = false;

  // AI
  int aiSerial_ = 0;
  std::string aiKey_;
  std::vector<Candidate> aiCands_;
  std::wstring aiStatus_;
  bool aiPanel_ = false;
  AiMode aiMode_ = AiMode::None;
};

}  // namespace yy
