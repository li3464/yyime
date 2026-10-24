# yy输入法

从零写的 Windows 中文输入法（基于 TSF），C++ 编写，用 llvm-mingw 编译。

- **打字方式**：全拼（简拼、隔音符、可选模糊音）、小鹤双拼、自然码双拼、微软双拼、五笔86（z 键拼音反查）
- **中英混打**：直接打英文单词也出候选
- **自己的词**：会记住你选过的词和分几次选出来的词组；支持自定义短语；rq / sj / xq 出日期、时间、星期
- **小功能**：`[` `]` 以词定字（xianzai 按 `[` 出「现」，按 `]` 出「在」）；v 模式（`v123` 数字转大写/金额，`v1+2*3` 计算，`vjt` `vsx` 等出特殊符号）；词后面跟 emoji（kaixin → 😄，可关）
- **生僻字**：全拼下 `u` 开头按偏旁拼音拆字（`uhuohuohuo` → 焱、`umumumu` → 森）或按笔画找字（`u` + h 横 s 竖 p 撇 n 点 z 折，`uhh` → 二），候选旁边标读音；全拼也能直接打出四万多个生僻字，排在常用字后面。设置里可关
- **AI（DeepSeek，可换成别的 OpenAI 兼容接口）**：打拼音时按 Tab 让 AI 组句；选中文字按 Ctrl+Alt+空格 润色；没选中时按同一个键续写
- **皮肤**：10 款 AI 娘角色皮肤与 7 套经典配色，可跟随系统深浅色，也能在设置文件里自己配；横排/竖排

## 小白安装

**1.** 打开 [Releases 下载页](https://github.com/li3464/yyime/releases/latest)，下载 `yyime-版本号.zip`

**2.** 解压，双击里面的 `安装.bat`，弹出管理员确认窗口时点「是」

**3.** 按 Win+空格 切换到「yy输入法」，就能打字了。设置：右键任务栏上的「中/英」→「设置…」

- **以后更新**：下载新版的 zip，再双击 `安装.bat` 就行，你的设置和自造词会保留
- **卸载**：双击 `卸载.bat`
- 带反作弊的游戏（比如英雄联盟）里别用，打游戏时切回微软拼音

## 用最新代码安装（克隆）

想用还没发版的最新代码，就自己克隆、编译。只要先装好 [Git](https://git-scm.com/download/win)（一路「下一步」就行）；编译器、词库都由 `setup.bat` 自己下载，不用再装别的。

**1.** 按 Win 键，搜「cmd」，打开「命令提示符」，把下面三行复制进去，按回车：

```
git clone https://github.com/li3464/yyime.git
cd yyime
setup.bat
```

**2.** 等它跑完。第一次要下载编译器和词库（一共约 260 MB，开着代理的话会自动走代理），再编译；中途断了再运行一次 `setup.bat`，会接着下。最后弹出管理员确认窗口时点「是」。

**3.** 按 Win+空格 切换到「yy输入法」。

- **没装 Git**：在本页点绿色的 **Code** → **Download ZIP**，解压后双击里面的 `setup.bat`
- **以后更新**：打开 cmd，`cd yyime` 进到文件夹，运行 `git pull`，再运行 `setup.bat`
- **代理**：开着系统代理（比如 Clash 的「系统代理」）会自动用上；没开系统代理的话手动指定：`setup.bat -Proxy http://127.0.0.1:7890`（端口换成你代理软件的）
- **卸载**：双击 `dist\yyime\卸载.bat`，你的设置和自造词会保留

## 本次更新

- 本地整句优化：接入八股文搭配模型，用有界束搜索保留同音路径，并结合已选文字排序。无需联网；设置里可以关闭。模型缺失时自动使用词频组句。
- 设置窗口：右键任务栏「中/英」→「设置…」，或按 `Ctrl+Alt+O`。输入选项、皮肤预览、AI 接口、翻译目标和快捷键集中配置。
- 新外观：侧栏导航、角色皮肤卡片与实时预览。DeepSeek、GPT、Claude、Gemini、Qwen、GLM、MiniMax、Zcode、Kimi、Grok 十款配色与插画；可单独关闭候选框角色装饰。插画保存在 `themes/`，通过 WIC 解码并缓存缩略图，文件缺失时保留配色和正常输入。
- 翻译：选中文字后按 `Ctrl+Alt+T`，选择译文替换选中内容，`Esc` 取消。默认中文译英语，其他语言译简体中文，也可指定目标语言。使用现有 AI 接口，仅主动触发时发送文本。
- 快捷键：设置里可修改 AI 整句、润色/续写、翻译、打开设置、标点切换；支持 Ctrl/Alt/Shift 组合和 F1–F24，空值停用。检查重复和无效按键。单按 Shift 的开关在输入页。
- 小功能：设置里管理日期/时间/星期、中英混打、记词、emoji、五笔自动上屏和自定义短语。以词定字用 `[` `]`（原来的 `[` `]` 翻页改为只用 `-` `=`）；全拼下 v 开头进 v 模式：数字转大写和金额、四则运算（支持括号、`%`、`^`）、十类特殊符号。
- 保存设置保留注释、未知配置和自定义皮肤，使用一次性替换写入；修复自定义皮肤 `#颜色值` 被当作注释的问题。

默认快捷键：AI 整句 `Tab`；润色/续写 `Ctrl+Alt+Space`；翻译 `Ctrl+Alt+T`；设置 `Ctrl+Alt+O`；标点 `Ctrl+.`。除 AI 整句外，其余功能在未组字时触发；需先切到 yy输入法。系统 Win+空格由 Windows 管理。

升级后重新打开需要打字的应用即可加载新版 DLL，无需清空配置或用户词库。

自动回归测试：`tools/run_regressions.ps1`。本机模拟接口与 32/64 位 TSF 翻译上屏测试：`python tools/mock_translation_test.py --host`，不使用真实 API 或用户配置。

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

最省事的是直接运行 `setup.bat`（见上面「小白安装」），它会把 llvm-mingw 下到 `.tools\` 里并按下面的步骤做完。想手动编译的话：

需要：[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)（设环境变量 `LLVM_MINGW` 指向它的文件夹）、Python 3（llvm-mingw 自带的 `python\bin\python3.exe` 也行）。

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

- 拼音、英文、emoji：[雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice)（GPL-3.0）
- 五笔86：[rime-wubi](https://github.com/rime/rime-wubi)（LGPL-3.0）
- 整句模型：[rime-octagram-data](https://github.com/lotem/rime-octagram-data)（LGPL-3.0），读取方法参考 [librime-octagram](https://github.com/lotem/librime-octagram)（BSD-3-Clause）
- 生僻字：大字表来自雾凇拼音；拆字表 [rime-radical-pinyin](https://github.com/mirtlecn/rime-radical-pinyin)（GPL-3.0，经雾凇拼音收录）；笔画表 [rime-stroke](https://github.com/rime/rime-stroke)（LGPL-3.0）；带声调的读音 [pinyin-data](https://github.com/mozillazg/pinyin-data)（MIT）

## 致谢

感谢这些开源项目，没有它们就没有 yy输入法：

- [雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice)（iDvel 和贡献者们）：拼音、英文词库和 emoji 对照表
- [rime-wubi](https://github.com/rime/rime-wubi)（RIME 社区）：五笔86 码表
- [rime-octagram-data](https://github.com/lotem/rime-octagram-data) 和 [librime-octagram](https://github.com/lotem/librime-octagram)（lotem 佛振）：整句语言模型和读取方法
- [rime-radical-pinyin](https://github.com/mirtlecn/rime-radical-pinyin)（Mirtle）：部件拆字表
- [rime-stroke](https://github.com/rime/rime-stroke)（RIME 社区）：五笔画码表
- [pinyin-data](https://github.com/mozillazg/pinyin-data)（mozillazg）：汉字的带声调读音

AI 娘皮肤和输入法图标里的角色形象来自 B 站画师 **ZipZipPipe**（[B 站主页](https://space.bilibili.com/4168597)）。

## 注意

- 输入法会被加载进你打字的每个程序里。带反作弊的游戏（比如英雄联盟）里别用，打游戏时切回微软拼音。
- AI 功能要在 `%APPDATA%\yyime\yyime.ini` 里填自己的 API key；只有按 AI 键时才会把光标附近的文字发给 AI 服务。
