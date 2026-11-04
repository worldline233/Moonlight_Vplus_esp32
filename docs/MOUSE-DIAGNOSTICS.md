# Mac 鼠标事件速率诊断

需要使用包含 `MouseDiag` 诊断代码的新构建；旧 v1.0 包不会因为设置环境变量就自动具备此功能。诊断默认关闭，设置 `MOONLIGHT_MOUSE_DIAGNOSTICS=1` 后生效，每个活动采样窗口只写汇总，不逐条记录鼠标事件。

## 在 Mac 上采集

1. 安装新的 arm64 构建，完全退出之前的 Moonlight。
2. 下载该次 Action 产物中的 `diagnose-mouse-macos.sh`。在终端执行：

   ```bash
   bash ~/Downloads/diagnose-mouse-macos.sh /Applications/Moonlight.app
   ```

   如果脚本或应用位于其他目录，替换成实际路径。脚本会打印结果目录，位于 Mac 桌面的 `Moonlight-mouse-时间-随机后缀/`。
3. 先关闭 ESP32 接管，连接同一台主机的桌面。保持网络、分辨率、帧率、V-Sync、鼠标模式及鼠标硬件设置一致。
4. 在视频画面中央连续快速画圈 15 秒，同时记录远端网页的回报率。不要让指针抵住画面边界，也不要打开悬浮菜单。
5. 断开串流并完全退出 Moonlight。脚本会提取本次应用日志里的诊断行，保存成桌面结果目录内的 `mouse-diagnostics.log`。

无需安装 Python 或额外工具。如果没有脚本，也可以直接启动：

```bash
MOONLIGHT_MOUSE_DIAGNOSTICS=1 /Applications/Moonlight.app/Contents/MacOS/Moonlight
```

终端开头的 `Redirecting log output to ...` 会给出本次应用日志路径。在该文件内搜索 `MouseDiag`。

## 看哪些字段

启动串流时，日志应包含：

```text
MouseDiag macOS NSApplicationUpdateCycleEnabled=0
```

`0` 表示当前运行的应用包关闭了该设置；`1` 或 `missing` 表示需要重新核查安装包。这个值确认的是配置，不代表已经验证实机效果。

持续移动时会出现两类汇总。以下数字仅为说明格式的示例：

```text
MouseDiag client mode=relative window_ms=1000 sdl_hz=800.0 batch_hz=790.0 forward_hz=790.0 errors=0
MouseDiag transport window_ms=1000 relative_calls_hz=790.0 absolute_calls_hz=0.0 relative_submitted_hz=200.0 absolute_submitted_hz=0.0
```

| 字段 | 含义 |
| --- | --- |
| `mode` | `relative` 为游戏相对模式，`absolute` 为远程桌面绝对模式 |
| `sdl_hz` | 进入鼠标移动处理并被取出的 SDL 移动事件数，包含随后合并的事件，排除触摸/笔合成事件和未捕获时的输入 |
| `batch_hz` | 客户端合并积压移动事件后的处理批次数 |
| `forward_hz` | 客户端调用公共库鼠标移动接口的次数 |
| `errors` | 当前窗口内上述接口返回非零的次数，不是主机收包错误数 |
| `relative_calls_hz` / `absolute_calls_hz` | 公共库收到的相对/绝对移动调用次数；相对模式排除零位移调用 |
| `relative_submitted_hz` / `absolute_submitted_hz` | 成功交给传输层的相对/绝对移动消息数 |

`submitted` 不代表主机已收到，也不是 UDP 数据报数量；ENet 后续仍可能排队、合包或重传。公共库调用也可能来自触摸板等其他输入，所以本次测试只操作一只物理鼠标。

所有 `*_hz` 均按实际 `window_ms` 换算。两类汇总的时间窗口独立；应比较连续移动中间的 5～10 行，不比较单行边界。停止移动、失焦或打开菜单后，下一条汇总可能包含空闲时间而显著降低，忽略这些窗口。

## 如何定位

| 连续移动时观察到的结果 | 下一步排查范围 |
| --- | --- |
| `sdl_hz` 已只有约 200，本机网页约 800 | Mac 到 SDL 的事件投递、SDL 事件合并及应用处理线程是否被阻塞 |
| `sdl_hz` 约 800，`batch_hz` / `forward_hz` 约 200 | 客户端队列积压、移动事件合并及主线程调度 |
| `forward_hz` 与公共库调用速率较高，`submitted_hz` 约 200 | 公共库合并、发送线程调度或传输层调用阻塞 |
| `submitted_hz` 较高，远端网页约 200 | 后续网络传输、Sunshine 输入注入及远端浏览器测量 |

这些结果划定排查范围，不能单独证明某个组件有 bug。浏览器网页的事件率也不等同于鼠标 USB 硬件回报率。

随后分别测试游戏相对模式和远程桌面绝对模式，每种模式重新启动一次采集。记录 Mac 系统版本、屏幕刷新率、串流帧率、网页浏览器和结果。旧版与新版在相同远端网页上的对比也应一并保留。

## 开发验证

Linux 下运行以下命令，用 ASan/UBSan 检查真实公共库入队和提交路径。测试传输层与时钟是替身，不连接真实主机：

```bash
bash scripts/test-mouse-diagnostics.sh
```
