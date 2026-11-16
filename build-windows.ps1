param(
    [switch]$Bootstrap,
    [string]$QtRoot = "$PSScriptRoot\.tools\Qt\6.8.3\mingw_64",
    [string]$MingwRoot = "$PSScriptRoot\.tools\Qt\Tools\mingw1310_64",
    [switch]$SkipTests,
    [string]$OutputName = 'm1zuki-windows'
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if ($Bootstrap) {
    Invoke-Checked python @('-m', 'pip', 'install', '--user', 'aqtinstall', 'cmake', 'ninja')
    Invoke-Checked python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', '6.8.3', 'win64_mingw', '-O', "$PSScriptRoot\.tools\Qt", '--archives', 'qtbase', 'qtsvg')
    Invoke-Checked python @('-m', 'aqt', 'install-tool', 'windows', 'desktop', 'tools_mingw1310', 'qt.tools.win64_mingw1310', '-O', "$PSScriptRoot\.tools\Qt")
}
if (!(Test-Path "$QtRoot\bin\qmake.exe") -or !(Test-Path "$MingwRoot\bin\g++.exe")) {
    throw 'Qt or MinGW is missing. Run build-windows.ps1 -Bootstrap (requires Python), or supply -QtRoot and -MingwRoot.'
}
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path (python -c 'import cmake; print(cmake.CMAKE_BIN_DIR)') 'cmake.exe' }
$ninjaExe = if ($ninjaCommand) { $ninjaCommand.Source } else { Join-Path (python -c 'import ninja; print(ninja.BIN_DIR)') 'ninja.exe' }
if (!(Test-Path $cmakeExe) -or !(Test-Path $ninjaExe)) { throw 'Install CMake and Ninja or run with -Bootstrap.' }
$ctestExe = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
$oldPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$MingwRoot\bin;$oldPath"
    $build = "$PSScriptRoot\build-windows"
    Invoke-Checked $cmakeExe @('-S', $PSScriptRoot, '-B', $build, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninjaExe", "-DCMAKE_CXX_COMPILER=$MingwRoot/bin/g++.exe", "-DCMAKE_PREFIX_PATH=$QtRoot", '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON')
    Invoke-Checked $cmakeExe @('--build', $build, '--parallel', '2')
    if (!$SkipTests) { Invoke-Checked $ctestExe @('--test-dir', $build, '--output-on-failure') }
    $dist = "$PSScriptRoot\dist\$OutputName"
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath "$build\m1zuki.exe" -Destination $dist -Force
    Invoke-Checked "$QtRoot\bin\windeployqt.exe" @('--release', '--no-translations', '--compiler-runtime', "$dist\m1zuki.exe")
    Copy-Item -LiteralPath "$dist\m1zuki.exe" -Destination "$dist\mini-answer.exe" -Force
    Copy-Item -LiteralPath "$PSScriptRoot\WINDOWS.md", "$PSScriptRoot\assets\mini-answer-character-credits.txt" -Destination $dist -Force
    Copy-Item -LiteralPath "$PSScriptRoot\install-windows.ps1" -Destination $dist -Force
    Copy-Item -LiteralPath "$PSScriptRoot\THIRD_PARTY_NOTICES.txt" -Destination $dist -Force
    Copy-Item -LiteralPath "$PSScriptRoot\licenses" -Destination $dist -Recurse -Force
    Compress-Archive -Path $dist -DestinationPath "$PSScriptRoot\dist\$OutputName-x64.zip" -Force
    Write-Host "Ready: $dist\m1zuki.exe"
} finally { $env:PATH = $oldPath }
