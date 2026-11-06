# BTAudioReceiver 卸载脚本
#
# 本程序是绿色单文件应用，没有安装程序，"卸载"只有两步：
#   1. 清理注册表里的设置
#   2. （可选）删除日志文件
# 删除 exe 本身请手动进行。
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
Write-Host 'BTAudioReceiver uninstaller'
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
    $root = Split-Path -Parent $scriptDir
    $logs = Get-ChildItem -Path $root -Filter 'BTAudioReceiver-*.log' -File -Recurse -ErrorAction SilentlyContinue
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
Write-Host 'Done. You can now delete BTAudioReceiver.exe.' -ForegroundColor Green
