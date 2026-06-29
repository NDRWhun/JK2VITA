# Dot-source this before any VitaSDK build command:  . .\tools\env.ps1
# Puts VitaSDK on PATH for the JK2VITA build.
#
# Override the SDK location by setting $env:VITASDK before dot-sourcing.
# CMake + Ninja must be on PATH (system package manager, or `pip install cmake ninja`).
# Host-specific tweaks (e.g. a pip CMake/Ninja path) go in tools\env.local.ps1 (gitignored).

if (-not $env:VITASDK) { $env:VITASDK = "C:\vitasdk" }

# Host-specific overrides first (may reset $env:VITASDK + add a pip CMake/Ninja path).
$LocalEnv = Join-Path $PSScriptRoot "env.local.ps1"
if (Test-Path $LocalEnv) { . $LocalEnv }

# Then put the (possibly-overridden) SDK on PATH.
$env:PATH = "$env:VITASDK\bin;$env:PATH"

Write-Host "[env] VITASDK=$env:VITASDK"
Write-Host ("[env] gcc:   " + ((Get-Command arm-vita-eabi-gcc -ErrorAction SilentlyContinue).Source))
Write-Host ("[env] cmake: " + ((Get-Command cmake -ErrorAction SilentlyContinue).Source))
Write-Host ("[env] ninja: " + ((Get-Command ninja -ErrorAction SilentlyContinue).Source))
