#Requires -Version 5.1
<#
.SYNOPSIS
    把 Release 产物打包成可分发的 zip。
.DESCRIPTION
    只挑运行时真正需要的文件，不包含 .pdb（58 MB 调试符号）和链接中间产物。
    输出到仓库的 dist\ 目录，该目录已被 .gitignore 忽略，不要提交。

    脚本本身不删除任何文件：临时暂存目录建在 %TEMP% 下，交给系统清理。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1
    powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 -Version 0.2.0
#>

[CmdletBinding()]
param(
    [string]$Version = '0.1.0',
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$buildDir = Join-Path $repoRoot "app\x64\$Configuration\BTAudioReceiver"
$distDir = Join-Path $repoRoot 'dist'
$zipPath = Join-Path $distDir "BTAudioReceiver-$Version-win-x64.zip"

if (-not (Test-Path $buildDir)) {
    throw "找不到构建输出: $buildDir`n请先运行 app\build-app.bat"
}

# 缺少任何一个都跑不起来
$required = @(
    'BTAudioReceiver.exe',
    'btcore.dll',
    'App.xbf',
    'MainWindow.xbf',
    'BTAudioReceiver.pri',
    'Microsoft.WindowsAppRuntime.Bootstrap.dll'
)

foreach ($name in $required) {
    if (-not (Test-Path (Join-Path $buildDir $name))) {
        throw "缺少必需文件: $name"
    }
}

# 可选依赖：存在就带上
$optional = @(
    'Microsoft.Web.WebView2.Core.dll',
    'Microsoft.Web.WebView2.Core.winmd'
)

# 暂存目录建在 TEMP，避免脚本删除工作区里的东西
$stage = Join-Path $env:TEMP ("BTAudioReceiver-pkg-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $stage -Force | Out-Null

foreach ($name in $required) {
    Copy-Item (Join-Path $buildDir $name) (Join-Path $stage $name)
}
foreach ($name in $optional) {
    $src = Join-Path $buildDir $name
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage $name) }
}

$readme = @"
BTAudioReceiver $Version
========================================

把 Windows 11 电脑变成蓝牙音箱。

运行前必读
----------
本程序依赖 Windows App Runtime 1.8。如果没有安装，双击不会有任何反应。
请先安装：
  https://aka.ms/windowsappsdk/1.8/latest/windowsappruntimeinstall-x64.exe

使用
----
1. 双击 BTAudioReceiver.exe
2. 在手机蓝牙设置里配对这台电脑
3. 在列表中选中设备，点「连接」

命令行参数
----------
  BTAudioReceiver.exe                    正常运行，不产生任何文件
  BTAudioReceiver.exe --debug            日志输出到控制台
  BTAudioReceiver.exe --debug --output   控制台 + 在程序目录写日志文件
  BTAudioReceiver.exe --debug -d <路径>  控制台 + 写入指定路径

本目录下的文件请勿删除
----------------------
  BTAudioReceiver.exe / btcore.dll / *.xbf / *.pri
  Microsoft.WindowsAppRuntime.Bootstrap.dll
缺少任意一个都无法启动。

卸载
----
删除整个目录即可。设置保存在注册表：
  HKEY_CURRENT_USER\Software\BTAudioReceiver
可用 scripts\uninstall.ps1 清理。
"@

Set-Content -Path (Join-Path $stage 'README.txt') -Value $readme -Encoding UTF8

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
# 仅覆盖本脚本自己上一次生成的同名 zip（dist\ 已被 .gitignore 忽略）
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal

$size = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host ''
Write-Host "打包完成: $zipPath" -ForegroundColor Green
Write-Host "体积: $size MB"
Write-Host ''
Write-Host '上传到 GitHub Releases:' -ForegroundColor Cyan
Write-Host "  gh release create v$Version `"$zipPath`" --title `"v$Version`" --notes `"...`""
