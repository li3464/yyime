yy输入法 1.0

【安装】双击「安装.bat」，弹出管理员确认时点「是」。
        装好后按 Win+空格 切换到「yy输入法」。
【卸载】双击「卸载.bat」。你的设置和自造词会保留。

【怎么打字】
  打拼音，空格选第一个，数字键选别的；- = 或 [ ] 翻页；回车把字母原样上屏；Esc 取消
  单按 Shift：中/英切换（拼音打到一半按 Shift，字母原样上屏并切到英文）
  简拼：zg → 中国、这个；bjdx → 北京大学
  隔音符：xi'an → 西安
  英文单词直接打：hello、wifi
  rq 出日期，sj 出时间，xq 出星期
  鼠标点候选也能选，滚轮翻页

【AI】（先在设置文件里填 DeepSeek 的 api_key）
  打拼音时按 Tab：AI 把整串拼音转成句子，再给几种接着写的说法
  选中一段文字按 Ctrl+Alt+空格：AI 给出三种改法（润色 / 更正式 / 更口语）
  没选中文字时按 Ctrl+Alt+空格：AI 照着前文往下写

【右键任务栏上的「中/英」图标】
  换方案：全拼 / 小鹤双拼 / 自然码双拼 / 微软双拼 / 五笔86
  换皮肤、竖排候选、中英文标点、打开设置文件、编辑自定义短语
  五笔方案里 z 开头打拼音，可以反查字的五笔编码

【你的文件】都在 %APPDATA%\yyime\ 里
  yyime.ini            所有设置（改完保存就生效）
  custom_phrases.txt   自定义短语
  user_dict.txt        输入法学到的词

【词库来源】
  拼音、英文：雾凇拼音 rime-ice（GPL-3.0）https://github.com/iDvel/rime-ice
  五笔86：rime-wubi（LGPL-3.0）https://github.com/rime/rime-wubi
