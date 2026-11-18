param(
    [switch]$SkipTests,
    [switch]$DesktopTests,
    [string]$Generator
)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmakeCommand) { throw 'CMake was not found. Install a C++ desktop toolchain and CMake.' }
$buildDirectory = Join-Path $taskRoot 'build'
if (-not $Generator -and (Get-Command g++.exe -ErrorAction SilentlyContinue) -and (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue)) {
    $Generator = 'MinGW Makefiles'
}
$configureArguments = @('-S', $taskRoot, '-B', $buildDirectory, '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON')
$configureArguments += if ($DesktopTests) { '-DQINGTOU_ENABLE_DESKTOP_TESTS=ON' } else { '-DQINGTOU_ENABLE_DESKTOP_TESTS=OFF' }
if ($Generator) { $configureArguments += @('-G', $Generator) }
& $cmakeCommand.Source @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakeCommand.Source --build $buildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if (-not $SkipTests) {
    & (Join-Path (Split-Path $cmakeCommand.Source) 'ctest.exe') --test-dir $buildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
}
Write-Host ('Ready: ' + (Join-Path $taskRoot 'dist\QingTou.exe'))
