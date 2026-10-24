#include "session.h"

#include <algorithm>
#include <cwchar>

#include "engine.h"
#include "util.h"

namespace yy {

namespace {

constexpr size_t kMaxInput = 60;

bool IsModifierVk(unsigned vk) {
  switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
      return true;
    default:
      return false;
  }
}

bool IsLowerLetter(const KeyEvent& k) { return k.ch >= L'a' && k.ch <= L'z' && !k.ctrl && !k.alt; }

bool IsSymbol(wchar_t ch) {
  return ch > 0x20 && ch < 0x7F && !(ch >= L'0' && ch <= L'9') && !(ch >= L'a' && ch <= L'z') &&
         !(ch >= L'A' && ch <= L'Z');
}

}  // namespace

InputSession::InputSession() = default;

void InputSession::ApplyConfig(const Config& c) {
  if (c.scheme != scheme_) {
    Clear();
    scheme_ = c.scheme;
  }
  pageSize_ = c.pageSize;
  englishMixed_ = c.englishMixed;
  shiftToggle_ = c.shiftToggle;
  fullwidthPunct_ = c.fullwidthPunct;
  learn_ = c.learn;
  dynamic_ = c.dynamicPhrases;
  wubiAutoCommit_ = c.wubiAutoCommit;
  aiEnabled_ = c.aiEnabled;
  sentenceModel_ = c.sentenceModel;
  composeKey_ = ParseKeyChord(c.aiComposeKey);
  fuzzy_ = FuzzyRules::Parse(c.fuzzy);
}

void InputSession::SetChinese(bool on) { chinese_ = on; }

void InputSession::Clear() {
  input_.clear();
  consumed_ = 0;
  selected_.clear();
  conv_ = Conversion{};
  cands_.clear();
  highlight_ = 0;
  aiCands_.clear();
  aiStatus_.clear();
  aiKey_.clear();
  aiPanel_ = false;
  aiMode_ = AiMode::None;
  ++aiSerial_;  // 还没回来的 AI 结果作废
}

std::string InputSession::StateKey() const { return input_ + '\x01' + std::to_string(consumed_); }

std::wstring InputSession::SelectedText() const {
  std::wstring s;
  for (auto& seg : selected_) s += seg.text;
  return s;
}

// ---------------------------------------------------------------- 按键

bool InputSession::TestKeyDown(const KeyEvent& k) {
  if (IsModifierVk(k.vk)) return false;
  if (aiPanel_) return true;
  if (!chinese_) return false;
  if (composing()) return true;
  if (k.ctrl || k.alt) return false;
  if (IsLowerLetter(k)) return true;
  if (fullwidthPunct_ && IsPunctKey(k.ch)) return true;
  lastDigit_ = k.ch >= L'0' && k.ch <= L'9';
  return false;
}

SessionResult InputSession::OnKeyDown(const KeyEvent& k) {
  SessionResult r;
  if (!TestKeyDown(k)) return r;
  r.eaten = true;
  if (aiPanel_) return OnAiPanelKey(k);
  if (!composing()) {
    if (IsLowerLetter(k)) {
      StartWith(char(k.ch));
      r.changed = true;
      return r;
    }
    r.commit = MapPunct(k.ch);
    lastDigit_ = false;
    return r;
  }
  if (aiEnabled_ && composeKey_.Matches(k.vk, k.ctrl, k.alt, k.shift)) {
    r.aiRequested = true;
    return r;
  }
  if (k.ctrl || k.alt) return r;

  switch (k.vk) {
    case VK_SPACE:
      return cands_.empty() ? CommitRaw() : Select(highlight_);
    case VK_RETURN:
      return CommitRaw();
    case VK_BACK:
      Backspace();
      r.changed = true;
      return r;
    case VK_ESCAPE:
      Clear();
      r.changed = true;
      return r;
    case VK_PRIOR:
      PageBy(-1);
      r.changed = true;
      return r;
    case VK_NEXT:
      PageBy(1);
      r.changed = true;
      return r;
    case VK_UP:
    case VK_LEFT:
      MoveHighlight(-1);
      r.changed = true;
      return r;
    case VK_DOWN:
    case VK_RIGHT:
      MoveHighlight(1);
      r.changed = true;
      return r;
    case VK_TAB:
      MoveHighlight(k.shift ? -1 : 1);
      r.changed = true;
      return r;
    case VK_HOME:
    case VK_END:
    case VK_DELETE:
    case VK_INSERT:
      return r;
    default:
      break;
  }

  const wchar_t ch = k.ch;
  const bool upper = ch >= L'A' && ch <= L'Z';
  if ((ch >= L'a' && ch <= L'z') || upper) {
    if (scheme_ == Scheme::Wubi86) {
      if (upper) return r;
      std::string rest = Rest();
      if (rest.size() >= 4 && rest[0] != 'z') {  // 第五码：把前四码的第一个候选顶上去
        int idx = FullCandidateIndex();
        r.commit = SelectedText() + (idx >= 0 ? cands_[size_t(idx)].text : std::wstring());
        StartWith(char(ch));
        r.changed = true;
        return r;
      }
    }
    if (input_.size() < kMaxInput) {
      input_ += char(ch);
      Refresh();
    }
    r.changed = true;
    if (scheme_ == Scheme::Wubi86 && wubiAutoCommit_) {
      std::string rest = Rest();
      if (rest.size() == 4 && rest[0] != 'z' && cands_.size() == 1) return Select(0);
    }
    return r;
  }
  if (ch == L'\'' && scheme_ != Scheme::Wubi86) {
    if (!input_.empty() && input_.back() != '\'' && input_.size() < kMaxInput) {
      input_ += '\'';
      Refresh();
    }
    r.changed = true;
    return r;
  }
  if (ch == L';' && scheme_ == Scheme::Mspy) {
    // 微软双拼的 ; 是 ing 的韵母键：前面单着一个声母时才算
    std::string rest = Rest();
    size_t run = 0;
    for (size_t i = rest.size(); i > 0 && rest[i - 1] != '\''; --i) ++run;
    if (run % 2 == 1 && input_.size() < kMaxInput) {
      input_ += ';';
      Refresh();
      r.changed = true;
      return r;
    }
  }
  if (ch >= L'1' && ch <= L'9') {
    size_t start = (highlight_ / size_t(pageSize_)) * size_t(pageSize_);
    size_t idx = start + size_t(ch - L'1');
    if (idx < cands_.size() && idx < start + size_t(pageSize_)) return Select(idx);
    return r;
  }
  if (ch == L'-' || ch == L'[') {
    PageBy(-1);
    r.changed = true;
    return r;
  }
  if (ch == L'=' || ch == L']') {
    PageBy(1);
    r.changed = true;
    return r;
  }
  if (IsSymbol(ch)) {
    // 标点：先把第一个整串候选上屏，再接标点
    int idx = FullCandidateIndex();
    std::wstring text;
    if (idx >= 0) {
      Candidate c = cands_[size_t(idx)];
      Learn(c);
      text = SelectedText() + c.text;
    } else {
      std::string rest = Rest();
      rest.erase(std::remove(rest.begin(), rest.end(), '\''), rest.end());
      text = SelectedText() + Utf8ToWide(rest);
    }
    Clear();
    r.commit = text + (IsPunctKey(ch) ? MapPunct(ch) : std::wstring(1, ch));
    r.changed = true;
    return r;
  }
  return r;
}

SessionResult InputSession::OnShiftTap() {
  if (!shiftToggle_) return SessionResult{};
  return ToggleChinese();
}

SessionResult InputSession::ToggleChinese() {
  SessionResult r;
  if (aiPanel_) Clear();
  if (composing()) r = CommitRaw();
  chinese_ = !chinese_;
  r.modeChanged = true;
  r.changed = true;
  r.eaten = false;
  return r;
}

std::wstring InputSession::RawText() const {
  std::string rest = Rest();
  rest.erase(std::remove(rest.begin(), rest.end(), '\''), rest.end());
  return SelectedText() + Utf8ToWide(rest);
}

SessionResult InputSession::ClickCandidate(int indexOnPage) {
  size_t start = (highlight_ / size_t(pageSize_)) * size_t(pageSize_);
  size_t idx = start + size_t(std::max(indexOnPage, 0));
  if (aiPanel_) {
    SessionResult r;
    r.eaten = true;
    r.changed = true;
    if (idx < cands_.size()) r.commit = cands_[idx].text;
    Clear();
    return r;
  }
  return Select(idx);
}

// ---------------------------------------------------------------- 状态变化

void InputSession::StartWith(char c) {
  Clear();
  input_ = std::string(1, c);
  Refresh();
}

void InputSession::Backspace() {
  if (int(input_.size()) > consumed_) {
    input_.pop_back();
  } else if (!selected_.empty()) {
    consumed_ -= selected_.back().inputLen;
    selected_.pop_back();
  }
  if (input_.empty()) Clear();
  else Refresh();
}

SessionResult InputSession::Select(size_t i) {
  SessionResult r;
  r.eaten = true;
  r.changed = true;
  if (i >= cands_.size()) return r;
  Candidate c = cands_[i];
  int rest = int(Rest().size());
  if (c.consumed >= rest) {
    Learn(c);
    r.commit = SelectedText() + c.text;
    Clear();
    return r;
  }
  selected_.push_back(Segment{c.text, c.consumed, c.syls, c.kind});
  consumed_ += c.consumed;
  while (consumed_ < int(input_.size()) && input_[size_t(consumed_)] == '\'') {
    ++consumed_;
    ++selected_.back().inputLen;
  }
  if (consumed_ >= int(input_.size())) {
    r.commit = SelectedText();
    Clear();
    return r;
  }
  Refresh();
  return r;
}

SessionResult InputSession::CommitRaw() {
  SessionResult r;
  r.eaten = true;
  r.changed = true;
  std::string rest = Rest();
  rest.erase(std::remove(rest.begin(), rest.end(), '\''), rest.end());
  r.commit = SelectedText() + Utf8ToWide(rest);
  Clear();
  return r;
}

void InputSession::Learn(const Candidate& last) {
  if (!learn_ || scheme_ == Scheme::Wubi86) return;
  UserDict& user = Engine::Get().user();
  auto isPinyinWord = [](CandKind k) { return k == CandKind::Word || k == CandKind::Sentence; };
  bool allPinyin = !last.syls.empty() && isPinyinWord(last.kind);
  for (auto& s : selected_)
    if (s.syls.empty() || !isPinyinWord(s.kind)) allPinyin = false;
  for (auto& s : selected_)
    if (s.kind == CandKind::Word && !s.syls.empty()) user.Learn(s.text, s.syls);
  if (last.kind == CandKind::Word && !last.syls.empty()) user.Learn(last.text, last.syls);
  // 分几次选出来的词、或者整句，合起来当一个新词记下
  if (allPinyin && (!selected_.empty() || last.kind == CandKind::Sentence)) {
    std::wstring text;
    std::vector<uint16_t> syls;
    for (auto& s : selected_) {
      text += s.text;
      syls.insert(syls.end(), s.syls.begin(), s.syls.end());
    }
    text += last.text;
    syls.insert(syls.end(), last.syls.begin(), last.syls.end());
    if (syls.size() >= 2 && syls.size() <= 10) user.Learn(text, syls);
  }
}

void InputSession::Refresh() {
  if (StateKey() != aiKey_) {
    aiCands_.clear();
    aiStatus_.clear();
    aiKey_.clear();
  }
  cands_.clear();
  highlight_ = 0;
  conv_ = Conversion{};
  std::string rest = Rest();
  if (rest.empty()) return;
  Engine& eng = Engine::Get();
  const Dictionaries& d = eng.dicts();
  if (scheme_ == Scheme::Wubi86) {
    if (rest[0] == 'z') {
      // 五笔里打 z 开头：用拼音找字，后面标上五笔编码
      if (rest.size() > 1) {
        ConvertOptions opt;
        opt.englishMixed = false;
        ConvertPinyin(d, std::string_view(rest).substr(1), opt, &conv_);
        for (auto& c : conv_.cands) {
          c.consumed += 1;
          c.syls.clear();
          c.comment = Utf8ToWide(eng.WubiCodeOf(c.text));
        }
      }
      conv_.display = "z" + conv_.display;
    } else {
      ConvertWubi(d, rest, &conv_);
    }
  } else {
    auto user = eng.user().Snapshot();
    ConvertOptions opt;
    opt.scheme = scheme_;
    opt.fuzzy = &fuzzy_;
    opt.englishMixed = englishMixed_;
    opt.user = user.get();
    opt.useGrammar = sentenceModel_;
    std::wstring selectedContext = SelectedText();
    opt.context = selectedContext;
    ConvertPinyin(d, rest, opt, &conv_);
  }
  cands_ = conv_.cands;

  if (selected_.empty()) {
    std::vector<Phrase> ph = eng.phrases().Lookup(rest);
    if (dynamic_) {
      auto dyn = DynamicPhrases(rest);
      ph.insert(ph.end(), dyn.begin(), dyn.end());
    }
    std::stable_sort(ph.begin(), ph.end(), [](const Phrase& a, const Phrase& b) { return a.position < b.position; });
    size_t lastAt = 0;
    int lastPos = -1;
    for (const Phrase& p : ph) {
      cands_.erase(std::remove_if(cands_.begin(), cands_.end(),
                                  [&](const Candidate& c) { return c.text == p.text; }),
                   cands_.end());
      size_t at = size_t(p.position - 1);
      if (p.position == lastPos) at = lastAt + 1;
      at = std::min(at, cands_.size());
      Candidate c;
      c.text = p.text;
      c.consumed = int(rest.size());
      c.kind = CandKind::Custom;
      cands_.insert(cands_.begin() + long(at), std::move(c));
      lastAt = at;
      lastPos = p.position;
    }
  }
  if (!aiCands_.empty()) cands_.insert(cands_.begin(), aiCands_.begin(), aiCands_.end());
}

int InputSession::FullCandidateIndex() const {
  int rest = int(Rest().size());
  if (highlight_ < cands_.size() && cands_[highlight_].consumed >= rest) return int(highlight_);
  for (size_t i = 0; i < cands_.size(); ++i)
    if (cands_[i].consumed >= rest) return int(i);
  return -1;
}

void InputSession::MoveHighlight(int delta) {
  if (cands_.empty()) return;
  long h = long(highlight_) + delta;
  h = std::max(0L, std::min(h, long(cands_.size()) - 1));
  highlight_ = size_t(h);
}

void InputSession::PageBy(int delta) {
  size_t ps = size_t(pageSize_);
  size_t start = (highlight_ / ps) * ps;
  if (delta > 0 && start + ps < cands_.size()) highlight_ = start + ps;
  if (delta < 0 && start >= ps) highlight_ = start - ps;
}

// ---------------------------------------------------------------- 标点

bool InputSession::IsPunctKey(wchar_t ch) const {
  return ch != 0 && std::wcschr(L",.?!;:()[]{}<>\\^_$~`\"'", ch) != nullptr;
}

std::wstring InputSession::MapPunct(wchar_t ch) {
  if (!fullwidthPunct_) return std::wstring(1, ch);
  switch (ch) {
    case L',': return L"，";
    case L'.': return lastDigit_ ? L"." : L"。";
    case L'?': return L"？";
    case L'!': return L"！";
    case L';': return L"；";
    case L':': return lastDigit_ ? L":" : L"：";
    case L'(': return L"（";
    case L')': return L"）";
    case L'[': return L"【";
    case L']': return L"】";
    case L'{': return L"｛";
    case L'}': return L"｝";
    case L'<': return L"《";
    case L'>': return L"》";
    case L'\\': return L"、";
    case L'^': return L"……";
    case L'_': return L"——";
    case L'$': return L"￥";
    case L'~': return L"～";
    case L'`': return L"·";
    case L'"':
      dquoteOpen_ = !dquoteOpen_;
      return dquoteOpen_ ? L"“" : L"”";
    case L'\'':
      squoteOpen_ = !squoteOpen_;
      return squoteOpen_ ? L"‘" : L"’";
    default:
      return std::wstring(1, ch);
  }
}

// ---------------------------------------------------------------- 给界面

std::wstring InputSession::Preedit() const {
  if (aiPanel_) return {};
  return SelectedText() + Utf8ToWide(conv_.display);
}

std::wstring InputSession::AuxText() const {
  if (aiPanel_) return aiMode_ == AiMode::Translate ? L"AI 翻译" : aiMode_ == AiMode::Polish ? L"AI 润色" : L"AI 续写";
  return Preedit();
}

InputSession::View InputSession::PageView() const {
  View v;
  size_t ps = size_t(pageSize_);
  size_t start = (highlight_ / ps) * ps;
  for (size_t i = start; i < cands_.size() && i < start + ps; ++i) v.items.push_back(&cands_[i]);
  v.highlight = int(highlight_ - start);
  v.hasPrev = start > 0;
  v.hasNext = start + ps < cands_.size();
  v.status = aiStatus_;
  return v;
}

// ---------------------------------------------------------------- AI

int InputSession::BeginAiCompose() {
  ++aiSerial_;
  aiKey_ = StateKey();
  aiCands_.clear();
  aiStatus_ = L"✨ AI 思考中…";
  aiMode_ = AiMode::Compose;
  return aiSerial_;
}

int InputSession::BeginAiPanel(AiMode mode) {
  Clear();
  aiPanel_ = true;
  aiMode_ = mode;
  aiStatus_ = mode == AiMode::Translate ? L"AI 正在翻译…" : mode == AiMode::Polish ? L"AI 正在润色…" : L"AI 正在往下写…";
  return aiSerial_;
}

void InputSession::SetAiResult(int requestId, const std::vector<std::wstring>& texts, const std::wstring& status) {
  if (requestId != aiSerial_) return;
  aiStatus_ = status;
  if (aiPanel_) {
    cands_.clear();
    for (auto& t : texts) {
      Candidate c;
      c.text = t;
      c.kind = CandKind::Ai;
      cands_.push_back(std::move(c));
    }
    highlight_ = 0;
    return;
  }
  if (aiKey_ != StateKey()) return;
  aiCands_.clear();
  int rest = int(Rest().size());
  for (auto& t : texts) {
    Candidate c;
    c.text = t;
    c.consumed = rest;
    c.kind = CandKind::Ai;
    aiCands_.push_back(std::move(c));
  }
  cands_.erase(std::remove_if(cands_.begin(), cands_.end(), [](const Candidate& c) { return c.kind == CandKind::Ai; }),
               cands_.end());
  cands_.insert(cands_.begin(), aiCands_.begin(), aiCands_.end());
  highlight_ = 0;
}

std::string InputSession::AiPinyin() const {
  if (scheme_ == Scheme::Wubi86) return {};
  return conv_.display;
}

std::wstring InputSession::AiBestGuess() const {
  int rest = int(Rest().size());
  for (auto& c : cands_)
    if (c.kind != CandKind::Ai && c.consumed >= rest) return SelectedText() + c.text;
  return SelectedText();
}

SessionResult InputSession::OnAiPanelKey(const KeyEvent& k) {
  SessionResult r;
  r.eaten = true;
  r.changed = true;
  if (k.vk == VK_ESCAPE) {
    Clear();
    return r;
  }
  if (IsLowerLetter(k) || (k.ch >= L'A' && k.ch <= L'Z')) {
    // 关掉面板，接着正常打字
    Clear();
    if (chinese_ && IsLowerLetter(k)) StartWith(char(k.ch));
    else r.commit = std::wstring(1, k.ch);
    return r;
  }
  if (cands_.empty()) return r;  // 还在等结果
  switch (k.vk) {
    case VK_SPACE:
    case VK_RETURN:
      return ClickCandidate(int(highlight_ % size_t(pageSize_)));
    case VK_UP:
    case VK_LEFT:
      MoveHighlight(-1);
      return r;
    case VK_DOWN:
    case VK_RIGHT:
    case VK_TAB:
      MoveHighlight(1);
      return r;
    default:
      break;
  }
  if (k.ch >= L'1' && k.ch <= L'9') {
    size_t idx = size_t(k.ch - L'1');
    if (idx < cands_.size()) return ClickCandidate(int(idx));
    return r;
  }
  Clear();
  return r;
}

}  // namespace yy
