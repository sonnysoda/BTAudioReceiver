# BTAudioReceiver

把 Windows 电脑变成蓝牙音箱：接收手机 / 平板推来的音频，用电脑的扬声器播放。

**单个 242 KB 的 exe，零依赖**——拷到任何 Windows 10/11 机器上双击即用，不需要安装任何运行时。

> Turn a Windows PC into a Bluetooth speaker (A2DP sink), shipped as one self-contained exe.

![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4)
![size](https://img.shields.io/badge/exe-242%20KB-informational)
![ui](https://img.shields.io/badge/UI-Win32%20%2B%20Direct2D-blue)
![core](https://img.shields.io/badge/core-Rust-orange)

## 特性

- **A2DP Sink 接收**：手机连上电脑即可外放
- **单文件分发**：一个 exe，无 DLL、无运行时、无配置文件
- **托盘常驻**：关闭窗口只是收进托盘，双击还原；右键菜单可连接最近设备 / 断开 / 退出
- **记住最近设备**：连接成功后自动记录，最多 3 台，可从托盘菜单一键重连
- **单实例**：重复启动只会唤起已有窗口，不会多开抢音频连接
- **设备列表**：一页最多 6 行，超出可滚动；超长设备名左右折返滚动
- **选中高亮**：已连接行左侧 3px 强调色竖条 + 低透明度底色
- **中英双语**、**浅色 / 深色 / 跟随系统**
- **零配置文件**：设置存注册表，运行后目录里不会多出任何文件
- **命令行调试日志**：默认关闭，开启后可输出到控制台或指定路径

## 系统要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 2004+ / Windows 11 |
| 架构 | x64 |
| 蓝牙 | 适配器与驱动需支持 **A2DP Sink** 角色 |

不需要 .NET、不需要 Visual C++ 运行库、不需要 Windows App Runtime。

## 使用

1. 双击 `BTAudioReceiver.exe`
2. 在手机蓝牙设置里配对这台电脑
3. 在列表中选中设备，点「连接」

关闭窗口会收进托盘；右键托盘图标可以连接最近设备、断开或退出。

## 命令行参数

默认运行**不产生任何文件**。

| 参数 | 行为 |
|---|---|
| （无） | 无日志，零文件 |
| `--debug` | 日志输出到控制台 |
| `--debug --output` | 控制台 + 在 exe 同目录生成 `BTAudioReceiver-YYYYMMDD-HHmmss.log` |
| `--debug -d <路径>` | 控制台 + 写入指定路径（若为目录则自动生成文件名） |

```bat
BTAudioReceiver.exe --debug
BTAudioReceiver.exe --debug --output
BTAudioReceiver.exe --debug -d D:\logs
```

## 数据存储与卸载

运行期只写注册表，不落地任何文件：

```
HKEY_CURRENT_USER\Software\BTAudioReceiver
├── Config   语言、外观（key=value 串）
└── Recent   最近连接的设备（每行 id<TAB>名称，最多 3 条）
```

卸载 = 删除 exe + 清理注册表：

```powershell
# 只清注册表
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1

# 连同日志一起清理
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1 -RemoveLogs -Yes
```

也可以在应用「设置 → 重置所有设置」里清空。

## 构建

**前置条件**

- Visual Studio 2022（勾选「使用 C++ 的桌面开发」）
- Windows SDK 10.0.26100.0
- Rust（`x86_64-pc-windows-msvc`）

**编译**

```bat
cd core
cargo build --release

cd ..\app
build-app.bat
```

产物会被复制到 `dist\BTAudioReceiver.exe`。

也可以用 VS 直接打开 `app\BTAudioReceiver.vcxproj`。

> 必须**先编 core 再编 app**：`build-app.bat` 会检查 `core\target\release\btcore.lib` 是否存在。

## 目录结构

```
BTAudioReceiver/
├── app/                    Win32 外壳
│   ├── main.cpp            窗口、列表交互、设置面板、业务接线
│   ├── gfx.cpp             Direct2D / DirectWrite 渲染栈
│   ├── tray.cpp            托盘图标与右键菜单（独立消息线程）
│   ├── single.cpp          单实例 Mutex + 唤起已有实例
│   ├── settings.h          注册表读写（无 JSON 依赖）
│   ├── texts.h             双语字符串表
│   ├── theme.h             浅色 / 深色配色
│   ├── log.h               日志（控制台 / 文件）
│   └── cli.h               命令行解析
├── core/                   Rust 业务内核（静态库，链入 exe）
│   ├── src/lib.rs          WinRT 蓝牙调用、连接状态机
│   ├── include/btc_core.h  C ABI 契约
│   └── examples/           验证用探针（STA / 线程公寓）
├── scripts/uninstall.ps1   卸载脚本
└── dist/                   构建产物（不提交）
```

## 技术说明

### 架构

分两层：**Win32 只做界面**，所有 WinRT 蓝牙 / 音频调用都在 **Rust** 里，通过 C ABI 交互
（结构化数据用 UTF-8 文本）。Rust 内核编译为**静态库**直接链入 exe，所以没有独立的 DLL。

### 为什么不用 WinUI 3

第一版用的是 C++/WinRT + WinUI 3，功能相同但分发代价大：

| | WinUI 3 版 | 本版（Win32） |
|---|---|---|
| 分发的文件 | 7 个 | **1 个** |
| 前置依赖 | 必须预装 Windows App Runtime 1.8 | **无** |
| 体积 | 557 KB exe + 1.3 MB 资源与 DLL | **242 KB** |
| 内存（Working Set） | 100 MB | **约 60 MB** |

WinUI 3 做不到单文件，是结构性原因，不是配置问题：

1. **`.xbf` / `.pri` 是磁盘文件**：XAML 编译产物不嵌入 exe 资源，而是由 MRT Core 从磁盘读取，
   没有"从内存加载编译后 XAML"的 API。
2. **`Microsoft.WindowsAppRuntime.Bootstrap.dll` 无法静态链接**：NuGet 里那个
   `Microsoft.WindowsAppRuntime.Bootstrap.lib` 只有 3 KB、符号带 `__imp_` 前缀，是**导入库**。

### 渲染

- 直接 `ID2D1HwndRenderTarget` + DirectWrite，通过 GDI 重定向表面呈现
- 全部 UI 自绘：列表行、按钮、分段控件、模态设置面板
- **不做 Mica**：Mica 需要 DWM 背景透出，必须走 D3D11 合成交换链 +
  DirectComposition，而 `ID2D1HwndRenderTarget` 与之不兼容。实测两者内存只差 1.5 MB，
  却要多出 34 个线程和一整套设备管理代码，因此选择单层渲染。

### 线程模型

`AudioPlaybackConnection` 的 `OpenAsync` 完成回调会投递回创建它的公寓。
在 UI 线程上阻塞等待等于自己等自己，界面会假死。因此：

- 设备扫描、连接、断开全部在 `std::thread` 里执行
- 结果通过 `PostMessage` 回到 UI 线程（`WM_APP + 12..14`）
- UI 线程只做绘制和状态更新

### 磁盘 I/O

除日志（命令行显式开启）外，**运行期不写任何文件**；设置存注册表。

## 已知限制

- **无法区分「已连接」与「正在串流」**：`AudioPlaybackConnectionState` 只有 `Closed` / `Opened`
- **只支持 A2DP**：LE Audio 在 Windows 没有接收端 API；aptX / LDAC 由系统协商，无公开接口
- **没有音量增强（>100%）**：系统音量 API 上限为 0 dB
- **无辅助功能**：自绘控件不被 UI Automation 识别，读屏软件读不到

## 致谢

思路参考 [Noxy229/BluetoothAudioReceiver](https://github.com/Noxy229/BluetoothAudioReceiver)（MIT）。

两者技术栈不同：那个项目是 C# / .NET 8 + WPF，本项目是 **Rust + Win32 / Direct2D**，
代码完全独立实现，不共享任何代码。
