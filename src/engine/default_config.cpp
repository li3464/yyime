#include "config.h"

namespace yy {

std::string DefaultConfigText() {
  return R"INI(; ============================================================
;  yy输入法 设置文件
;  改完保存就行，下次打字自动生效，不用重启。
;  以 ; 开头的行是注释。
; ============================================================

[general]
; 输入方案：pinyin 全拼 / xiaohe 小鹤双拼 / ziranma 自然码双拼 / mspy 微软双拼 / wubi86 五笔86
scheme = pinyin
; 每页几个候选（3~9）
page_size = 7
; 中英混打：打英文单词也出候选
english_mixed = true
; 单按 Shift 切换中/英文
shift_toggle = true
; 中文状态下用全角标点（，。？！）
fullwidth_punct = true
; 切到这个输入法时默认中文
start_chinese = true
; 记住你选过的词，下次排前面
learn = true
; rq 出日期、sj 出时间、xq 出星期
dynamic_phrases = true
; 五笔：四码只有一个候选时自动上屏
wubi_auto_commit = true
; 模糊音，不需要就留空。例：z=zh c=ch s=sh n=l an=ang en=eng in=ing
fuzzy =
; 调试日志（写到 %TEMP%\yyime.log），出问题时再打开
debug_log = false
; 使用本地语言模型优化整句，不联网
sentence_model = true
; 打“开心”时后面跟着出 😄 这样的 emoji
emoji = true

[ui]
; 皮肤：auto 跟随系统深浅色 / light 晴空 / dark 夜幕 / sakura 樱花 / mint 薄荷 / ink 水墨 / violet 星紫 / amber 琥珀
theme = auto
; 候选排列：horizontal 横排 / vertical 竖排
layout = horizontal
font = Microsoft YaHei UI
font_size = 17
; 候选框上方显示正在打的拼音
preedit_in_window = true
; AI 娘主题的角色装饰；关闭后只保留配色
character_art = true

[ai]
; AI 补全/润色，默认接 DeepSeek（别家兼容 OpenAI 格式的接口也行，改 base_url 和 model）
enabled = true
; 在 https://platform.deepseek.com 申请 API key，填在等号后面
api_key =
base_url = https://api.deepseek.com
model = deepseek-chat
; 一般不用填；要走代理就写成 http://127.0.0.1:7890 这样
proxy =
timeout_ms = 20000
; 打拼音时按这个键：AI 把整串拼音转成句子，再给几种接着写的说法
compose_key = Tab
; 没在打字时按这个键：选中了文字就润色，没选中就照着前文往下写
hotkey = Ctrl+Alt+Space
; 选中文字后翻译；auto 为中文译英语、其他语言译简体中文
translate_hotkey = Ctrl+Alt+T
translate_target = auto

[hotkeys]
settings = Ctrl+Alt+O
punctuation = Ctrl+.

; ---- 自己配皮肤：去掉下面各行开头的 ; ，再把上面 [ui] 里的 theme 改成 mine ----
;[theme.mine]
;label = 我的皮肤
;background = #FFFFFF
;border = #E4E4E7
;text = #18181B
;index = #A1A1AA
;preedit = #2563EB
;highlight_bg = #2563EB
;highlight_text = #FFFFFF
;corner_radius = 10
)INI";
}

}  // namespace yy
