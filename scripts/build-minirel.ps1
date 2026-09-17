$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$gcc = Get-Command gcc -ErrorAction SilentlyContinue

if (-not $gcc) {
    $gcc = Get-ChildItem `
        "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.*\mingw64\bin\gcc.exe" `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

if (-not $gcc) {
    throw 'GCC was not found. Install WinLibs with: winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact'
}

$gccPath = if ($gcc -is [System.Management.Automation.CommandInfo]) {
    $gcc.Source
} else {
    $gcc.FullName
}

$sources = @(
    (Get-ChildItem "$repoRoot\algebra\*.c").FullName
    (Get-ChildItem "$repoRoot\schema\*.c").FullName
    (Get-ChildItem "$repoRoot\physical\*.c").FullName
    "$repoRoot\frontend\fes.c"
    "$repoRoot\run\main.c"
)

$output = "$repoRoot\run\minirel.exe"
& $gccPath `
    -std=gnu89 `
    -fcommon `
    -Wno-implicit-int `
    -Wno-implicit-function-declaration `
    "-I$repoRoot\include" `
    @sources `
    -o $output

if ($LASTEXITCODE -ne 0) {
    throw "MINIREL build failed with exit code $LASTEXITCODE"
}

Write-Output "Built $output"
