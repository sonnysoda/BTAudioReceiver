# BTAudioReceiver

把 Windows 11 电脑变成蓝牙音箱：接收手机 / 平板推来的音频，用电脑的扬声器播放。

> Turn a Windows 11 PC into a Bluetooth speaker (A2DP sink).

![platform](https://img.shields.io/badge/platform-Windows%2011-0078D4)
![rust](https://img.shields.io/badge/core-Rust-orange)
![ui](https://img.shields.io/badge/UI-WinUI%203%20(C%2B%2B%2FWinRT)-blue)

## 功能

- **A2DP Sink 接收**：手机连上电脑即可外放
- **托盘常驻**：关闭窗口只是收进托盘，后台继续工作；双击还原，右键菜单可断开 / 连接最近设备 / 退出
- **单实例**：重复启动只会唤起已有窗口，不会多开抢音频连接
- **设备列表**：一页最多 6 行，超出滚动；超长设备名左右折返滚动
- **中英双语**：简体中文 / English，即时切换
- **主题**：浅色 / 深色 / 跟随系统（Mica 材质）
- **零文件**：设置存注册表，不产生任何配置文件
- **固定窗口**：480×660，不可缩放

## 系统要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 11 |
| 架构 | x64（未提供 ARM64 构建） |
| 蓝牙 | 适配器与驱动需支持 **A2DP Sink** 角色 |
| 运行时 | **Windows App Runtime 1.8**（必装，否则双击无反应） |

> 这是框架依赖发布。运行时下载：<https://aka.ms/windowsappsdk/1.8/latest/windowsappruntimeinstall-x64.exe>

## 命令行参数

默认运行**不产生任何文件**。日志由旗标控制：

| 参数 | 行为 |
|---|---|
| （无） | 无日志，零文件 |
| `--debug` | 日志输出到**控制台** |
| `--debug --output` | 控制台 + 在 exe 同目录生成 `BTAudioReceiver-YYYYMMDD-HHmmss.log` |
| `--debug -d <路径>` | 控制台 + 写入指定路径（若为目录则自动生成文件名） |

```bat
BTAudioReceiver.exe --debug
BTAudioReceiver.exe --debug --output
BTAudioReceiver.exe --debug -d D:\logs
```

## 数据存储与卸载

本程序**绿色免安装**，不需要安装程序，解压即用。用户数据只有一处：

```
注册表  HKEY_CURRENT_USER\Software\BTAudioReceiver   （一个 JSON 字符串值）
```

卸载 = 删除 exe + 清理注册表。用附带脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
# 连同日志一起清理
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1 -RemoveLogs -Yes
```

也可以在应用「设置 → 重置所有设置」里清空。

## 构建

**前置条件**

- Visual Studio 2022（17.10+），勾选「使用 C++ 的桌面开发」
- Windows App SDK **C++** 组件（VS Installer → 单个组件 → 搜 “Windows App SDK C++”）
- Windows SDK 10.0.26100.0
- Rust（`x86_64-pc-windows-msvc`）

**编译**

```bat
cd core
cargo build --release

cd ..\app
build-app.bat
```

产物：`app\x64\Release\BTAudioReceiver\BTAudioReceiver.exe`

也可以用 VS 直接打开 `app\BTAudioReceiver.vcxproj`。

两点注意：

- **必须先编 `core` 再编 `app`**，`build-app.bat` 会检查 `btcore.lib` 是否存在。
- **首次构建较慢**（几分钟），因为 CppWinRT 要为整个 Windows SDK 生成投影头文件；
  之后是增量的。

## 发布

仓库**只存源码**，二进制通过 GitHub Releases 分发（不提交 exe，避免仓库无限膨胀）。

```powershell
# 1. 打包（只挑运行时文件，排除 58 MB 的 .pdb）
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 -Version 0.1.0

# 2. 上传为 release 附件
gh release create v0.1.0 "dist\BTAudioReceiver-0.1.0-win-x64.zip" --title "v0.1.0" --notes "..."
```

`dist/` 已加入 `.gitignore`。下载地址可固定使用：

```
https://github.com/<user>/BTAudioReceiver/releases/latest/download/BTAudioReceiver-0.1.0-win-x64.zip
```

## 常见问题

**Q：只拷贝 `BTAudioReceiver.exe` 到别的文件夹，提示缺少 `Microsoft.WindowsAppRuntime.Bootstrap.dll`**

**A：它不是单文件程序**，必须整个文件夹一起复制。Rust 内核已静态链接进 exe，
但 Windows App SDK 的引导器和 XAML 资源仍是外部文件，缺了无法启动。

完整的运行时文件集（共 7 个，`.pdb` 不需要）：

```
BTAudioReceiver.exe
App.xbf
MainWindow.xbf
BTAudioReceiver.pri
Microsoft.WindowsAppRuntime.Bootstrap.dll
Microsoft.Web.WebView2.Core.dll
Microsoft.Web.WebView2.Core.winmd
```

最省事的办法是用 `scripts/package.ps1` 打好的 zip，解压即用。

**Q：双击完全没反应，也没有报错框**

**A：大概率是没装 Windows App Runtime 1.8**。安装地址见上方「系统要求」。

**Q：为什么不能做成单个 exe？（参考项目就是一个 exe）**

**A：这是 WinUI 3 的固有限制，与体积无关。** 两处硬依赖：

1. **`.xbf` / `.pri` 是磁盘文件**。XAML 编译产物不嵌入 exe 资源，而是通过
   `ms-appx:///` 由 MRT Core 从磁盘读取，没有"从内存加载编译后 XAML"的 API。
2. **`Microsoft.WindowsAppRuntime.Bootstrap.dll` 无法静态链接**。NuGet 里虽然有
   `Microsoft.WindowsAppRuntime.Bootstrap.lib`，但它只有 3 KB、符号带 `__imp_`
   前缀，是**导入库**而非静态库。

参考项目能做到单文件，是因为它用 **C# / .NET 8 + WPF**，走
`dotnet publish -p:PublishSingleFile=true`——.NET 有单文件打包机制，WinUI 3 没有。

真要单文件只有三条路：改用 MSIX 打包（变成"安装"）、去掉 XAML 全用代码写 UI
（仍需 Bootstrap.dll）、或换 Win32 自绘。当前选择是"绿色文件夹"，与绝大多数
C++ 桌面应用一致。

## 目录结构

```
BTAudioReceiver/
├── app/                     WinUI 3 外壳（C++/WinRT）
│   ├── App.xaml(.h/.cpp)    应用入口、命令行旗标、日志初始化、单实例守卫
│   ├── MainWindow.xaml      主窗口 UI
│   ├── MainWindow.xaml.cpp  设备列表、连接状态机、设置弹窗、托盘联动
│   ├── tray.cpp             托盘图标与右键菜单（独立消息线程）
│   ├── single.cpp           单实例 Mutex + 唤起已有实例
│   ├── settings.h           注册表读写
│   ├── strings.h            双语字符串表
│   ├── log.h                日志（控制台 / 文件）
│   └── cli.h                命令行解析
├── core/                    Rust 业务内核（静态库，链入 exe）
│   ├── src/lib.rs           WinRT 调用、连接状态机
│   ├── include/btc_core.h   C ABI 契约
│   └── examples/            验证用探针（STA / 线程公寓）
├── scripts/
│   ├── package.ps1          打包（生成可分发的 zip）
│   └── uninstall.ps1        卸载（清理注册表）
└── docs/
    ├── NOTES.md             开发踩坑记录（WinUI 3 C++ 的坑基本都在这里）
    └── REQUIREMENTS.md      需求规格
```

架构上分两层：**C++/WinRT 只做界面**，所有 WinRT 蓝牙/音频调用都在 **Rust** 里，通过 C ABI 交互（结构化数据走 UTF-8 JSON）。Rust 内核以静态库链接进 exe，没有独立的 DLL。

## 已知限制

- **无法区分“已连接”与“正在串流”**：`AudioPlaybackConnectionState` 只有 `Closed` / `Opened`
- **只支持 A2DP**：LE Audio 在 Windows 没有接收端 API；aptX/LDAC 由系统协商，无公开接口
- **没有音量增强（>100%）**：系统音量 API 上限为 0 dB，软件层面无法突破
- **不是单文件**：WinUI 3 的资源与引导器必须是磁盘文件，分发形式为文件夹（见 FAQ）
- 应用内的“配对接管”开关、每设备自动连接尚未实现

## 致谢

思路参考 [Noxy229/BluetoothAudioReceiver](https://github.com/Noxy229/BluetoothAudioReceiver)（MIT）。

两者技术栈不同：那个项目是 C# / .NET 8 + WPF，本项目是 **Rust 内核 + C++/WinRT / WinUI 3**，
代码完全独立实现，不共享任何代码。
