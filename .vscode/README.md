# N32G435CBL7 编译、烧录与调试

## 环境配置

安装项目推荐的 Cortex-Debug、CMake Tools、C/C++ 和 Tasks 扩展，然后执行
`开发人员: 重新加载窗口`。

首次在一台电脑上使用本项目时，需要创建以下两个本机配置文件：

```powershell
Copy-Item .vscode/settings.example.json .vscode/settings.json
Copy-Item CMakeUserPresets.example.json CMakeUserPresets.json
```

在 `.vscode/settings.json` 中填写当前电脑上的 CMake、Ninja、GNU Arm
工具链和 J-Link 安装路径。在 `CMakeUserPresets.json` 中填写 Ninja
可执行文件路径和 GNU Arm 工具链的 `bin` 目录。

这两个文件已加入 `.gitignore`，只用于保存每台电脑自己的路径，不会提交到
远程仓库。仓库中的 `settings.example.json` 和
`CMakeUserPresets.example.json` 仅作为配置模板。

连接已供电的 N32G435CBL7 开发板，并确认 J-Link 的 SWD、GND 和 VTref
连接正确。烧录或调试前，请关闭 J-Flash 以及其他正在占用 J-Link 的程序。

如果出现 `Failed to power up DAP`、`GDB Server Quit Unexpectedly` 或超时：

1. 完全断开目标板电源，确认 J-Link 的 VTref、GND、SWDIO、SWCLK 和 NRST
   均已连接；其中 NRST 用于恢复已经进入 STOP2 或异常状态的芯片。
2. 将板子重新上电，把本地 `n32.swdSpeed` 暂时设为 `1`，先运行 `JLink-Flash`。
3. 如果仍不能连接，按住板上的复位键，再启动烧录任务；看到 J-Link 开始连接
   后再松开复位键。

Debug 版本默认屏蔽 STOP2，仅使用 WFI，便于连接和单步调试。Release 版本仍保留
   原有的 STOP2 低功耗策略。

## 编译

按 `Ctrl+Shift+B` 执行默认的 Debug 编译任务。该任务会使用本机
`CMakeUserPresets.json` 中的 `Debug` 预设重新配置并编译工程。

也可以通过 CMake Tools 选择以下预设：

- `Debug`：调试版本。
- `Release`：发布版本。

如果更换了工具链路径或配置失败，请执行：

1. `CMake: 删除缓存并重新配置`。
2. 选择 `Debug` 或 `Release` 配置预设。
3. 再次点击“生成”。

成功编译后，固件位于：

```text
build/Debug/N32G435_Template.elf
build/Release/N32G435_Template.elf
```

## 烧录

点击状态栏中的 `JLink-Flash`，或者执行
`任务: 运行任务` -> `JLink-Flash`。

烧录任务不会自动编译工程，请先完成 Debug 或 Release 编译。任务会从
`build/Debug` 和 `build/Release` 中选择修改时间最新的 ELF 文件，生成新的
BIN 文件，然后执行以下操作：

1. 下载固件到 `0x08000000`。
2. 回读并验证固件。
3. 复位芯片并运行程序。

任务输出中会显示实际选择的 ELF 路径。烧录日志保存在该 ELF 文件所在目录的
`jlink-flash.log` 中。成功时会输出：

```text
Flash verified; target running.
```

如果编译失败，请先解决编译错误再执行烧录，否则任务可能会选择之前生成的旧
ELF 文件。

## 调试

在“运行和调试”中选择 `N32G435CBL7: J-Link Debug`，然后按 `F5`。
调试配置会先编译 Debug 版本、下载 ELF 文件，并在 `main` 函数处停止。

常用快捷键：

- `F9`：添加或取消断点。
- `F10`：单步跳过。
- `F11`：单步进入。
- `Shift+F5`：停止调试。

示例程序可观察以下变量：

- `g_thread_a_count`
- `g_thread_b_count`
- `g_rtos_error`

调试会话占用 J-Link 时不要同时执行烧录任务。不要使用 CMake 的
Run/Launch 命令运行固件，因为 Windows 无法直接执行 ARM ELF 文件。

项目使用本机配置的 J-Link 版本和 SWD 速度。N32G435CBL7 使用 128 KB Flash、32 KB RAM，
应用布局保留版本信息和参数数据分区。烧录和调试流程不会
执行芯片解锁或修改 Option Bytes。
