param([string]$SourceRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
foreach ($name in @('esp32_mouse_router', 'esp32_keyboard_router', 'esp32_performance_stats', 'esp32_mouse_bridge')) {
    $folder = Join-Path $SourceRoot "build\input-tests\$name"
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    Push-Location $folder
    try {
        & qmake.exe (Join-Path $SourceRoot "tests\$name\$name.pro") 'CONFIG+=release' 'CONFIG-=debug'
        if ($LASTEXITCODE -ne 0) { throw "$name qmake failed" }
        & nmake.exe /NOLOGO
        if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
        $binary = Join-Path $folder "release\${name}_test.exe"
        if (!(Test-Path $binary)) { $binary = Join-Path $folder "${name}_test.exe" }
        if ($name -eq 'esp32_mouse_bridge') {
            $report = Join-Path $folder 'results.txt'
            & $binary -o "$report,txt"
            $testExit = $LASTEXITCODE
            if (Test-Path $report) { Get-Content -LiteralPath $report }
        } else {
            & $binary
            $testExit = $LASTEXITCODE
        }
        if ($testExit -ne 0) { throw "$name failed" }
    } finally { Pop-Location }
}
