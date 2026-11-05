# BTAudioReceiver 卸载脚本
#
# 本程序是绿色单文件应用，没有安装程序，所以没有传统意义的"卸载"。
# 这个脚本负责清理运行期间写入的用户数据：
#   1. 注册表设置  HKCU\Software\BTAudioReceiver
#   2. 日志文件（仅在指定 -RemoveLogs 时）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1 -RemoveLogs -Yes

[CmdletBinding()]
param(
    [switch]$RemoveLogs,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$regKey = 'HKCU:\Software\BTAudioReceiver'

Write-Host ''
Write-Host 'BTAudioReceiver uninstaller' -ForegroundColor Cyan
Write-Host '---------------------------'

if (-not $Yes) {
    $answer = Read-Host 'Remove registry settings (HKCU\Software\BTAudioReceiver)? [y/N]'
    if ($answer -notin @('y', 'Y')) {
        Write-Host 'Cancelled, nothing changed.'
        return
    }
}

if (Test-Path $regKey) {
    Remove-Item -Path $regKey -Recurse -Force
    Write-Host "Removed registry key: $regKey"
}
else {
    Write-Host "Registry key not found (already clean): $regKey"
}

if ($RemoveLogs) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $appDir = Split-Path -Parent $scriptDir
    $logs = Get-ChildItem -Path $appDir -Filter 'BTAudioReceiver-*.log' -File -Recurse -ErrorAction SilentlyContinue
    if ($logs) {
        foreach ($log in $logs) {
            Write-Host "Removing log: $($log.FullName)"
            Remove-Item $log.FullName -Force
        }
    }
    else {
        Write-Host 'No log files found.'
    }
}
else {
    Write-Host 'Log files kept. Re-run with -RemoveLogs to delete them.'
}

Write-Host ''
Write-Host 'Done. You can now delete BTAudioReceiver.exe and this folder.' -ForegroundColor Green
