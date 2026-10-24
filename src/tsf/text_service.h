#pragma once
// 输入法本体：TSF 在每个用到它的线程里建一个 TextService。
// 按键交给 InputSession 处理，这里负责把结果写进文档、摆候选框、管中英状态。
#include <functional>
#include <memory>
#include <string>

#include "../engine/session.h"
#include "tsf_compat.h"

namespace yy {

class CandidateWindow;
class LangBarButton;
struct Config;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfDisplayAttributeProvider,
                          public ITfCompartmentEventSink,
                          public ITfThreadFocusSink {
 public:
  TextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor(Ex)
  STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
  STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* prevFocus) override;
  STDMETHODIMP OnPushContext(ITfContext* pic) override;
  STDMETHODIMP OnPopContext(ITfContext* pic) override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* eaten) override;

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* composition) override;

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

  // ITfCompartmentEventSink
  STDMETHODIMP OnChange(REFGUID rguid) override;

  // ITfThreadFocusSink
  STDMETHODIMP OnSetThreadFocus() override;
  STDMETHODIMP OnKillThreadFocus() override;

  // ---- 给语言栏按钮、候选窗口用
  bool chinese() const { return session_.chinese(); }
  Scheme scheme() const { return session_.scheme(); }
  void ToggleChinese();
  void ShowMenu(POINT pt);

 private:
  ~TextService();

  // 激活/停用的各个步骤
  bool InitThreadMgrSinks();
  void UninitThreadMgrSinks();
  bool InitKeySink();
  void UninitKeySink();
  void PreserveShiftKey(bool on);
  bool TakeShiftTap();  // 刚才是不是单按了一下 Shift（看完就清掉，防止重复切换）
  void InitCompartment();
  void UninitCompartment();
  void InitDisplayAttributeAtom();
  void InitLangBar();
  void UninitLangBar();

  // 设置
  void SyncConfig();
  void ApplyStyle();

  // 按键
  KeyEvent MakeKeyEvent(WPARAM wParam) const;
  bool IsKeyboardDisabled(ITfContext* pic) const;
  bool MatchHotkey(const KeyEvent& k) const;
  void HandleResult(ITfContext* pic, const SessionResult& r);

  // 改文档（都在编辑会话里调用）
  HRESULT RequestEdit(ITfContext* pic, DWORD flags, std::function<HRESULT(TfEditCookie)> fn);
  void StartComposition(TfEditCookie ec, ITfContext* pic);
  void UpdateComposition(TfEditCookie ec, ITfContext* pic);
  void CommitText(TfEditCookie ec, ITfContext* pic, const std::wstring& text);
  void EndComposition(TfEditCookie ec, bool clearText);
  void FinishCompositionNow();  // 同步把没打完的拼音原样留下并结束组字
  void SetInputAttribute(TfEditCookie ec, ITfContext* pic, ITfRange* range, bool on);
  void UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic);
  void RefreshCandidateWindow();  // 不改文档，只按上次的位置重画
  std::wstring ReadTextBefore(TfEditCookie ec, ITfContext* pic, int maxChars);
  std::wstring ReadSelection(TfEditCookie ec, ITfContext* pic);

  // 中英状态（系统的 OPENCLOSE 开关）
  void SetOpenClose(bool open);
  bool GetOpenClose() const;
  void OnModeChanged();

  // AI
  void StartAiCompose(ITfContext* pic);
  void StartAiPanel(ITfContext* pic);
  void OnAiDone(int requestId, void* response);

  // 鼠标点候选
  void OnCandidateClick(int indexOnPage);
  void OnCandidateWheel(int delta);

  // 菜单
  void OnMenuCommand(UINT id);

  LONG ref_ = 1;
  ITfThreadMgr* threadMgr_ = nullptr;
  TfClientId clientId_ = TF_CLIENTID_NULL;
  DWORD activateFlags_ = 0;
  DWORD threadMgrSinkCookie_ = TF_INVALID_COOKIE;
  DWORD threadFocusSinkCookie_ = TF_INVALID_COOKIE;
  DWORD openCloseSinkCookie_ = TF_INVALID_COOKIE;
  bool keySinkAdvised_ = false;
  TfGuidAtom inputAtom_ = TF_INVALID_GUIDATOM;

  ITfComposition* composition_ = nullptr;
  ITfContext* compositionContext_ = nullptr;
  ITfContext* lastContext_ = nullptr;
  HWND contextWnd_ = nullptr;

  InputSession session_;
  std::shared_ptr<const Config> cfg_;
  bool systemDark_ = false;
  DWORD darkCheckedAt_ = 0;
  UINT hotkeyVk_ = 0;
  bool hotkeyCtrl_ = false, hotkeyAlt_ = false, hotkeyShift_ = false;

  CandidateWindow* candWnd_ = nullptr;
  LangBarButton* langBar_ = nullptr;
  RECT lastCaret_ = {};
  bool threadFocused_ = true;

  // Shift 单击检测
  bool shiftAlone_ = false;
  DWORD shiftDownAt_ = 0;
  bool shiftPreserved_ = false;
};

}  // namespace yy
