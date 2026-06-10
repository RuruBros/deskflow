param(
  [ValidateSet("dev", "core", "gui", "daemon", "platform", "all", "configure", "shell")]
  [string]$Command = "dev"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$VsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
$VsCMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path $VsDevCmd)) {
  throw "VsDevCmd.bat not found: $VsDevCmd"
}

function Invoke-DevCmd {
  param([Parameter(Mandatory = $true)][string]$InnerCommand)

  $CommandLine = "call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && cd /d `"$Root`" && $InnerCommand"
  & cmd.exe /d /s /c $CommandLine
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

$CMake = "cmake"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  if (-not (Test-Path $VsCMake)) {
    throw "cmake not found in PATH or Visual Studio bundle."
  }
  $CMake = "`"$VsCMake`""
}

switch ($Command) {
  "dev" { Invoke-DevCmd "$CMake --build --preset dev" }
  "core" { Invoke-DevCmd "$CMake --build --preset core" }
  "gui" { Invoke-DevCmd "$CMake --build --preset gui" }
  "daemon" { Invoke-DevCmd "$CMake --build --preset daemon" }
  "platform" { Invoke-DevCmd "$CMake --build --preset platform" }
  "all" { Invoke-DevCmd "$CMake --build --preset windows-debug-vcpkg" }
  "configure" { Invoke-DevCmd "$CMake --preset dev" }
  "shell" { Invoke-DevCmd "cmd.exe /k" }
}
