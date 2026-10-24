# yy输入法

从零写的 Windows 中文输入法（基于 TSF），C++ 编写，用 llvm-mingw 编译。

- **打字方式**：全拼（简拼、隔音符、可选模糊音）、小鹤双拼、自然码双拼、微软双拼、五笔86（z 键拼音反查）
- **中英混打**：直接打英文单词也出候选
- **自己的词**：会记住你选过的词和分几次选出来的词组；支持自定义短语；rq / sj / xq 出日期、时间、星期
- **AI（DeepSeek，可换成别的 OpenAI 兼容接口）**：打拼音时按 Tab 让 AI 组句；选中文字按 Ctrl+Alt+空格 润色；没选中时按同一个键续写
- **皮肤**：7 套内置皮肤，可跟随系统深浅色，也能在设置文件里自己配；横排/竖排

使用说明见 [scripts/README.txt](scripts/README.txt)（会一起放进安装包）。

## 目录

| 目录 | 内容 |
|---|---|
| `src/engine` | 打字引擎：词库读取、拼音/双拼切分、组句、用户词库、设置。和 Windows 输入法框架无关，可以单独测试 |
| `src/tsf` | 输入法本体：COM 注册、TSF 文本服务、组字、语言栏按钮 |
| `src/ui` | 候选框（Direct2D/DirectWrite 画在分层窗口上） |
| `src/ai` | 调 AI 接口（WinHTTP，后台线程） |
| `tools` | 词库下载和编译、图标生成、端到端测试脚本 |
| `scripts` | 安装、卸载、打包脚本 |
| `test` | 测试程序 |

## 从源码编译

需要：[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)（默认放在 `C:\Users\17821\tools\llvm-mingw`，放别处就设环境变量 `LLVM_MINGW`）、Python 3。

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch_data.ps1     # 下载并编译词库到 data\
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1                # 编出 32/64 位 DLL 和引擎测试程序
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1      # 打包到 dist\yyime
```

然后双击 `dist\yyime\安装.bat`（要管理员确认）。

## 测试

- 引擎测试：`build\engine_test.exe <data目录> <临时用户目录> <输出文件>`，看候选和模拟打字的结果
- 端到端测试：`tools\run_host_test.ps1 [-Arch x64|x86]`，不用重装，直接测 `build` 里刚编好的 DLL（会有个小窗口在右下角拿走一两秒键盘焦点）
- 冒烟测试：`test\com_smoke.cpp`，不注册直接加载 DLL，检查 COM 接口和引用计数

两个环境变量方便测试：`YYIME_USER_DIR` 指定用户目录（不碰真正的设置和词库），`YYIME_DATA_DIR` 指定词库目录。

## 词库来源

词库文件不在这个仓库里，由 `tools\fetch_data.ps1` 下载：

- 拼音、英文：[雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice)（GPL-3.0）
- 五笔86：[rime-wubi](https://github.com/rime/rime-wubi)（LGPL-3.0）

## 注意

- 输入法会被加载进你打字的每个程序里。带反作弊的游戏（比如英雄联盟）里别用，打游戏时切回微软拼音。
- AI 功能要在 `%APPDATA%\yyime\yyime.ini` 里填自己的 API key；只有按 AI 键时才会把光标附近的文字发给 AI 服务。
