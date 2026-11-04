# Moonlight V+ ESP32 v1.0

基于 Moonlight V+ 6.4.1 的串流客户端，加入按主机配置的 ESP32 USB 键鼠输入。正式版的界面、更新检查和安装包文件名使用 `1.0.0`。Windows EXE 文件版本及 MSI 安装版本采用“应用主版本号 + 6”的规则，例如 `1.0.0` 对应 `7.0.0.0`、`1.1.0` 对应 `7.1.0.0`、`2.0.0` 对应 `8.0.0.0`，以支持升级已有的 6.x 安装版并保留原 UpgradeCode。macOS 的版本规则保持不变。

本仓库保存完整客户端源码，直接从当前提交编译。`app/` 是客户端，`moonlight-common-c/` 包含已修改的公共库源码，`tests/` 保存输入与网络回归测试；无需再下载上游主项目并应用补丁。

## 功能

- ESP32 协议 2，支持 16 个普通键和 8 个独立左右修饰键。
- 按主机保存设置，键盘和五个鼠标按钮可分别接管，支持排除键与本地快捷键。
- ESP32 状态显示在原性能统计栏；鼠标移动、滚轮和未接管按钮保留 Moonlight 路径。
- FEC 设置、兼容主机协商和丢帧恢复的选择性回移植。
- 加入 macOS 新 SDK 的输入事件同步修复，具体来源见 [更新记录](CHANGELOG.md)。

使用方法见 [ESP32 输入说明](docs/ESP32.md)。固件是单独的项目；已有兼容协议 2 固件可以继续使用。

## 获取与构建源码

```bash
git clone --recurse-submodules https://github.com/worldline233/Moonlight_Vplus_esp32.git
cd Moonlight_Vplus_esp32
```

已经克隆的仓库运行 `git submodule update --init --recursive`。未修改的 SDL 手柄数据库、qmdnsengine 和 AMF 保留为固定提交的子模块；修改后的公共库及 enet、nanors 已随源码入库。来源版本记录在 [SOURCE-PROVENANCE.json](SOURCE-PROVENANCE.json)。

### GitHub 云端构建

推送源码后，在 Actions 选择 **Build macOS v1.0** 或 **Build Windows v1.0**，点击 **Run workflow**。macOS 可选 `arm64`（M 系列）或 `x86_64`（Intel）；Windows 生成 x64 便携包。

成功后在运行页面的 **Artifacts** 下载。工作流仅手动触发，产物保留 7 天；不自动创建 GitHub Release。macOS 包未配置 Developer ID 签名与公证。

### macOS 本机

准备 Xcode、Python 3、Qt 6.11.1（包含 Multimedia、Image Formats、WebSockets）及 `create-dmg`，将 Qt 的 `bin` 加入 PATH。

```bash
npm install --global create-dmg@8.1.0
export CI_VERSION=1.0.0
bash scripts/test-input.sh .
lrelease app/languages/qml_zh_CN.ts -qm app/languages/qml_zh_CN.qm
lrelease app/languages/qml_zh_TW.ts -qm app/languages/qml_zh_TW.qm
bash scripts/generate-dmg.sh Release
```

DMG 输出在 `build/installer-Release/`，默认编译本机架构；可通过 `MOONLIGHT_ARCH=arm64` 或 `x86_64` 指定。打包脚本会下载固定版本的 SDL、FFmpeg 等预编译依赖。

### Windows 本机

准备 Python 3、Qt 6.11.1 MSVC 2022 x64、Visual Studio 2022 C++ 工具（含 ATL）、.NET/MSBuild 与 7-Zip。把 Qt、Python 和 7-Zip 加入 PATH，在已初始化的 MSVC x64 命令行运行：

```cmd
set CI_VERSION=1.0.0
powershell -NoProfile -ExecutionPolicy Bypass -File setup-deps.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test-input.ps1
lrelease app\languages\qml_zh_CN.ts -qm app\languages\qml_zh_CN.qm
lrelease app\languages\qml_zh_TW.ts -qm app\languages\qml_zh_TW.qm
scripts\build-arch.bat Release x64
```

便携 ZIP 输出在 `build/installer-x64-release/`。Qt SDK、编译工具、预编译依赖及个人配置均不随源码提交。

## 验证

```bash
python3 -m unittest discover -s tests/derive_version -p 'test_*.py'
```

macOS 的 `scripts/test-input.sh` 和 Windows 的 `scripts/test-input.ps1` 覆盖鼠标路由、键盘路由、性能栏文字和真实本地 WebSocket 传输。Linux 可运行 `python3 scripts/check-video-network.py`，使用 GCC、ASan 和 UBSan 检查 FEC 与丢帧恢复公共库代码。

macOS 修复在 `app/Info.plist` 设置 `NSApplicationUpdateCycleEnabled=false`，取消 macOS 26+ SDK 默认启用的输入事件与显示刷新同步。云端打包后再次检查应用内该设置。实际鼠标延迟还受屏幕刷新率、V-Sync、网络与视频处理影响，需要实机比较。

## 来源与许可证

上游为 [qiin2333/moonlight-qt](https://github.com/qiin2333/moonlight-qt)，保留的产品说明见 [README.upstream.md](README.upstream.md)。本项目依照 GPLv3 分发，见 [LICENSE](LICENSE)；依赖保留各自许可证。

本分支更新检查指向 `worldline233/Moonlight_Vplus_esp32` 的 Releases。
