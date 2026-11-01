# ESP-IDF v5.5.3 setup for Windows (PowerShell). Target: esp32p4.
# Usage (PowerShell):  powershell -ExecutionPolicy Bypass -File setup_idf_windows.ps1
# Prerequisites: Git for Windows, Python 3.9+ (both on PATH).
$ErrorActionPreference = "Stop"
$IdfVer  = "v5.5.3"
$IdfRoot = "C:\esp"
$IdfPath = Join-Path $IdfRoot "esp-idf-$IdfVer"

foreach ($cmd in @("git", "python")) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        Write-Error "$cmd not found on PATH. Install it first (see SETUP_ESP_IDF.md)."
    }
}
python -c "import sys; assert sys.version_info >= (3, 9), sys.version" 

# Short path without spaces/Korean characters avoids toolchain path issues.
New-Item -ItemType Directory -Force -Path $IdfRoot | Out-Null
if (-not (Test-Path $IdfPath)) {
    git clone -b $IdfVer --recursive --depth 1 --shallow-submodules https://github.com/espressif/esp-idf.git $IdfPath
} else {
    Write-Host "Already cloned: $IdfPath"
}

Set-Location $IdfPath
.\install.ps1 esp32p4

Write-Host ""
Write-Host "Done. In every new PowerShell window run:"
Write-Host "    $IdfPath\export.ps1"
Write-Host "Then check:  idf.py --version"
