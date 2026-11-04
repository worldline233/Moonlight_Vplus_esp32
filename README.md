# Moonlight V+ 6.4.1 + ESP32 test6 — macOS 云端构建

这是构建配方仓库。GitHub Actions 会下载锁定版本的完整上游源码及子模块，再应用与 Windows test6 一致的补丁，运行输入测试并生成 Mac 安装包。

## 上传（Windows 网页操作）

1. 在 GitHub 新建 **Public** 仓库，例如 `moonlight-esp32-macos`。可以勾选生成 README，方便进入仓库文件页面。
2. 解压上传包。打开 `cloud-upload` 文件夹，进入到能看到 `README.md`、`baseline.json`、`.github` 的这一层。
3. 仓库页面选择 **Add file → Upload files**，把这一层的全部文件和文件夹拖进去，然后 **Commit changes**。上传的是解压后的内容，不是 ZIP，也不要把外层 `cloud-upload` 目录一起套进去。
4. 确认仓库中存在 `.github/workflows/build-macos.yml`。如果网页没有上传 `.github`，用 **Add file → Create new file**，文件名填写 `.github/workflows/build-macos.yml`，粘贴本地同名文件内容后提交。

## 启动构建

1. 打开仓库 **Actions** 页面；如果出现启用工作流提示，先启用。
2. 选择 **Build macOS ESP32 test6 → Run workflow**。
3. `architecture`：M 系列 Mac 选择 `arm64`；Intel Mac 选择 `x86_64`。
4. 点击 **Run workflow**，等待任务完成。首次耗时尚未实测；失败时进入红色步骤查看日志。
5. 进入该次运行页面，下方 **Artifacts** 下载 `Moonlight-ESP32-test6-macOS-...`。下载的是 ZIP，解压后得到 `.dmg`、SHA-256 校验和及源码版本记录。产物保留 7 天。

工作流仅手动触发，每次只编译选择的一个架构。使用公开仓库的标准 macOS 机器，构建计算免费。当前没有配置 Apple Developer 签名或公证，首次打开可能需要通过 macOS 的“系统设置 → 隐私与安全性”允许打开。

## 版本与范围

- 基线：`qiin2333/moonlight-qt`，提交 `31d1d0033b425d31fd24a2c94e87e256619e4f4e`（V+ 6.4.1）。
- 补丁包括 ESP32 键鼠设置、原性能栏状态显示及 test6 FEC/丢帧恢复回移植。补丁与本地成功构建的 Windows test6 相同。
- `baseline.json` 固定子模块提交、补丁 SHA-256 和所有 49 个修改后文件的 SHA-256。校验失败时停止构建。
- 本地已用关闭自动换行转换的干净 Git 检出验证补丁重建、全部 49 个修改后文件哈希及键盘路由测试。准备脚本会处理公共库补丁的 Windows CRLF 换行，以兼容 macOS 检出的 LF 文件。完整 macOS 编译、打包和实际键鼠行为需由云端运行及 Mac 实机确认。
- 不包含本地 ESP32 密钥、个人连接配置、Windows 安装包或工具链。ESP32 地址和密钥在客户端每台主机的设置中填写。
- 本仓库只保存构建配方与补丁；完整基线与子模块会在每次构建时下载。遵循随附 GPLv3 许可证及上游依赖各自许可证。

官方参考：[GitHub Actions 计费](https://docs.github.com/en/billing/concepts/product-billing/github-actions)、[GitHub 标准运行器](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、[Moonlight 构建说明](https://github.com/moonlight-stream/moonlight-qt#building-from-source)。
