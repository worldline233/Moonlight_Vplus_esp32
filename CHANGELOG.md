# 更新记录

## v1.0 / 1.0.0

- 将完整客户端源码迁入本仓库，可直接查看、修改和构建；已修改的公共库及其 enet、nanors 依赖随源码保存。
- 包含按主机配置的 ESP32 协议 2 键盘和鼠标按钮接管、NKRO16、快捷键、排除名单和原性能统计栏状态显示。
- 包含 FEC 设置、协商、统计反馈与丢帧恢复的选择性回移植，基础仍为 Moonlight V+ 6.4.1。
- 加入 Moonlight 上游提交 `f018a71a8a2a63377acde15366625e8effeae05b`：在 macOS Info.plist 中关闭 NSApplicationUpdateCycleEnabled，处理新 SDK 导致输入事件随显示刷新同步的问题。
- 网络测试检查本次新增的释放消息，允许正常心跳随后到达。
- 更新检查使用本仓库的 GitHub Releases；macOS、Windows 工作流直接编译当前源码，发布版本号统一为 1.0.0。

上游源码和依赖版本见 SOURCE-PROVENANCE.json。鼠标延迟改善需在 Mac 实机上对比；本修复不改变屏幕刷新率或视频编解码路径。
