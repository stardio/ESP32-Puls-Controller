# Windows version of build_all.sh (run after export.ps1, from repo root).
$Root = Split-Path -Parent $PSScriptRoot
$Log  = Join-Path $PSScriptRoot "build_all.log"; "" | Set-Content $Log
$Failed = 0
function Run($dir, $b, $defaults) {
    Push-Location $dir
    "== $dir $b" | Tee-Object -FilePath $Log -Append
    idf.py -B $b -D "SDKCONFIG=$b/sdkconfig" -D "SDKCONFIG_DEFAULTS=$defaults" set-target esp32p4 build *>> $Log
    if ($LASTEXITCODE -eq 0) { "   OK" } else { "   FAIL (see $Log)"; $script:Failed = 1 }
    Pop-Location
}
Run "$Root\test-fw" "build_on"  "sdkconfig.defaults;sdkconfig.tfw_iram_on"
Run "$Root\test-fw" "build_off" "sdkconfig.defaults;sdkconfig.tfw_iram_off"
foreach ($m in "1axis_tcp","1axis_rtu","2axis_tcp","2axis_rtu") {
    Run "$Root\firmware" "build_$m" "sdkconfig.defaults;sdkconfig.model_$m"
}
exit $Failed
