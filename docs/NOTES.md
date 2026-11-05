# app — WinUI 3 外壳（C++/WinRT）

壳层只做界面与交互，所有蓝牙/音频逻辑都在 `../core`（Rust）。

## 前置条件

- Visual Studio 2022（17.10+），含 **C++ 桌面开发** 工作负载
- Windows App SDK **C++** 组件（VS Installer → 单个组件 → 搜 “Windows App SDK C++”）
- Windows SDK 10.0.26100.0
- Rust（`x86_64-pc-windows-msvc`）
- Windows App Runtime 1.8 运行时（已随 WASDK 安装）

## 构建

```bat
cd ..\core
cargo build --release

cd ..\app
build-app.bat
```

产物：`x64\Release\BtSink\BtSink.exe`（+ `btcore.dll` 由 MSBuild 自动复制）。

日常开发也可以直接用 VS 打开 `BTAudioReceiver.vcxproj`。

## 已验证的坑（都是实测踩出来的，改之前先看）

1. **头文件必须包含 `X.g.h`，不是 `X.xaml.g.h`**
   `X.xaml.g.h` 依赖 `X.g.h` 里定义的 `X_base`，直接包含会报
   “`MainWindow_base` 不是 `implementation` 的成员”。

2. **`App.xaml.h` / `MainWindow.xaml.h` 必须提供 `factory_implementation::X`**
   生成的 `X.g.cpp` 会 `make<factory_implementation::X>()`。
   缺了就是 `LNK2001: winrt_make_BtSink_App`。
   官方 stub 位置：`Generated Files\sources\App.h`。

3. **`App.g.cpp` / `MainWindow.g.cpp` 需要手动加入编译**
   它们不含 `#include "pch.h"`，需 `PrecompiledHeader=NotUsing`
   + `ForcedIncludeFiles=pch.h;X.xaml.h`。

4. **`App.idl` 里不要写 `App();`**
   否则 cppwinrt 生成 `App::App() : App(make<implementation::App>())`，
   而 `make<>` 返回可组合基类 `Application`，没有对应构造 → C2665。
   入口点是生成的 `wWinMain`（见 `App.xaml.g.hpp`），直接
   `Application::Start([]{ make<implementation::App>(); })`，不需要公开构造。

5. **必须设置 `<LanguageStandard>stdcpp20</LanguageStandard>`**
   CppWinRT 3.x 的生成代码需要 C++20。

6. **必须 `/utf-8`**
   源码含中文字面量，MSVC 默认按 GBK 解析会乱码。

7. **`RuntimeIdentifiers` 用 `win10-x64`**，不是 .NET 风格的 `win-x64`。

8. **`build-app.bat` 不调用 `vcvars64.bat`**
   它内部依赖 `reg.exe`，在受限环境会被拦截。脚本改为显式设置
   INCLUDE/LIB/PATH。正常在 VS 里构建不受影响。

9. **`btc_init` 必须容忍 `RPC_E_CHANGED_MODE`**
   XAML 的 `wWinMain` 把 UI 线程初始化为 **STA**，内核若再请求 MTA，
   `RoInitialize` 返回 `RPC_E_CHANGED_MODE (0x80010106)`。
   把它当失败就会出现“引擎初始化失败”且列表永远为空。
   验证方式：`cargo run --release --example sta_check`（core 内）。

10. **绝不能在 UI 线程上 `join()` WinRT 异步操作**
    `AudioPlaybackConnection::OpenAsync()` 的完成回调会被投递回创建它的公寓
    （XAML 的 UI 线程是 STA）。在同一个线程上阻塞等待 → 自己等自己，界面假死，
    表现为“点连接没反应”。所有 `btc_open_device` / `btc_close_device` 调用
    必须放到工作线程，结果再用 `DispatcherQueue.TryEnqueue` 回 UI。

    ⚠️ 同时记录一个被推翻的假设：曾怀疑“每个线程都要 `btc_init`”，
    实测不成立（见 `core/examples/thread_check.rs`），WinRT 会自动初始化。

11. **列表行必须用强引用保存 UI 元素**
    `MakeRow()` 里 `Border root` 是局部变量。若在行模型里存 `weak_ref`，
    函数返回时局部对象引用计数归零被回收，随后 `weak_ref.get()` 得到 `nullptr`，
    `Items().Append(nullptr)` 抛异常 → 进程以 `0xC000027B`（STOWED_EXCEPTION）
    崩溃，错误模块显示为 `CoreMessagingXP.dll`，极具误导性。
    **行模型里存强引用**（`Border` / `TextBlock` / `Button`），窗口持有行即持有元素。

## 诊断

异常会写到 exe 同目录的 `bt_sink.log`。启动异常、扫描失败、连接结果、
核心错误都会记录。排查问题时先看这个文件。

## 实测数据（2026-11-05）

| 项 | 值 |
|---|---|
| BtSink.exe | 239 KB |
| btcore.dll | 195 KB |
| 常驻 Working Set | 100 MB（启用 Mica 后） |
| 常驻 Private Bytes | 80.9 MB |
| 线程数 | 57 |

⚠️ 92.5 MB 说明 **WinUI 3 的运行时开销远大于 Rust 省下的部分**，
与 C# WPF 版处于同一量级。选 WinUI 3 等于放弃“低内存”这一目标。

12. **根元素透明但没设 SystemBackdrop = 全透明窗口**
    RootGrid 使用 `Background="Transparent"` 是为了让 Mica 透出来。
    一旦忘了 `SystemBackdrop(MicaBackdrop())`，窗口会变成完全透明：
    内容照常渲染、日志正常，但用户什么都看不见（切深色后尤其像“内容消失”）。
    现在的兜底：Mica 设置失败时用 `UISettings` 的系统背景色铺实底，
    并在每次切主题时重算。

13. **`windows.h` 的 `GetCurrentTime` 宏与 `Storyboard::GetCurrentTime` 冲突**
    包含 `Microsoft.UI.Xaml.Media.Animation.h` 会报“GetCurrentTime 不是成员”。
    必须在 pch.h 里先 `#undef GetCurrentTime`（在 windows.h 之后、winrt 头之前）。

14. **`KeyTime::FromTimeSpan` 实际在 `KeyTimeHelper` 上**
    `KeyTimeHelper::FromTimeSpan(TimeSpan)`，不是 `KeyTime::`。同理
    `RepeatBehavior` 的“永久”是 `RepeatBehaviorHelper::Forever()`。

15. **垂直 `StackPanel` 不会把子元素拉伸到满宽**
    跑马灯容器若放进垂直 StackPanel，量到的 `ActualWidth` 等于文本宽度，
    永远判定“没溢出”，动画永不触发。容器要用 `Grid`（行定义 Auto）承载。

16. **改了工程名后，旧中间目录会污染 XAML 元数据生成**
    `IntDir` 默认按 .vcxproj **文件名**生成。工程改名后，旧的
    `x64\Release\<旧名>\Merged\<旧名>.winmd` 仍在，XAML 编译器会捡到它，
    导致 `XamlTypeInfo.g.cpp` 里出现旧命名空间，报
    “`BtSink` 左侧的符号必须是一种类型”，且**把生成文件删掉重建也修不好**。
    根治办法：把 .vcxproj 文件本身改名（或显式指定 `IntDir`/`OutDir`），
    再确保旧中间目录不被引用。

## 已实现（v0.3）

- 设备列表：一页最多 6 行（`ListView.MaxHeight=344`），不足 6 行不出现滚动条
- 已连接行高亮：左侧 3px 强调色竖条 + 半透明底，颜色取自系统强调色
- 选中已连接设备时，「连接」按钮自动禁用
- 设置（左下角）：语言（简体中文 / English）、外观（浅色 / 深色 / 跟随系统），即时生效
- 设置持久化：`%LOCALAPPDATA%\BtSink\settings.json`
- 托盘常驻：关闭窗口只隐藏；右键菜单 = 显示主窗口 / 断开连接 / 连接设备（最近 3 个）/ 退出；双击图标还原
- 保护：若托盘未创建成功，关闭窗口会真正退出而不是隐藏（否则用户无法唤回）
- **单实例**：命名 Mutex（`Local\BtSink.SingleInstance`）；第二个进程
  `FindWindow` 找到已有实例的托盘窗口并投递唤起消息，然后自行退出
- **固定窗口**：480×660，`OverlappedPresenter.IsResizable(false)` +
  `IsMaximizable(false)`，不允许拖拽缩放
- **超长设备名折返滚动**：滚到末尾停 1.2s，再退回起点循环；
  文本未超出容器时完全静止（按 28px/s 计算时长）
- **产品名/进程名**：`BTAudioReceiver`（exe、窗口类、Mutex、注册表键全部统一）
- **设置存注册表** `HKCU\Software\BTAudioReceiver`（单个 JSON 字符串值），
  **不落地任何文件**；设置里有「重置所有设置」按钮清空该键
- **调试模式**（设置内开关，默认关）：
  - 关 → 全程不产生任何文件（日志写入为空操作）
  - 开 → 每次运行在 exe 同目录生成 `BTAudioReceiver-YYYYMMDD-HHmmss.log`

## 尚未完成

- 语言切换时，已打开的设置弹窗自身文案不会立即变（重开生效）
- 音量合成器品牌化（`IAudioSessionManager2`）
- 开机自启（按需求暂缓）
- 真正的「串流中」状态（需 `IAudioMeterInformation` 读端点电平）
