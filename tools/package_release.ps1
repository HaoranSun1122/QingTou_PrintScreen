param(
    [string]$Executable
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$releaseDirectory = Join-Path $projectRoot 'dist'
if (-not $Executable) { $Executable = Join-Path $releaseDirectory 'QingTou.exe' }
$executableFile = Get-Item -LiteralPath $Executable
$cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($cmakeText, 'project\(QingTou VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'Cannot read the project version from CMakeLists.txt.' }
$releaseVersion = $versionMatch.Groups[1].Value
if ($executableFile.VersionInfo.FileVersion -ne ($releaseVersion + '.0')) {
    throw 'Executable version differs from CMake. Build the current source before packaging.'
}

$reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($executableFile.FullName))
try {
    if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Executable is not a PE file.' }
    $reader.BaseStream.Position = 0x3C
    $peOffset = $reader.ReadInt32()
    $reader.BaseStream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
        throw 'The Windows package requires an x64 executable.'
    }
} finally { $reader.Dispose() }

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Directory]::CreateDirectory($releaseDirectory) | Out-Null

function Add-ArchiveFile($Archive, [string]$SourceFile, [string]$EntryName) {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $Archive, $SourceFile, $EntryName.Replace('\','/'), [System.IO.Compression.CompressionLevel]::Optimal
    ) | Out-Null
}

function New-Archive([string]$ArchivePath) {
    if (Test-Path -LiteralPath $ArchivePath) { Remove-Item -LiteralPath $ArchivePath -Force }
    return [System.IO.Compression.ZipFile]::Open($ArchivePath, [System.IO.Compression.ZipArchiveMode]::Create)
}

$windowsArchivePath = Join-Path $releaseDirectory "QingTou-v$releaseVersion-windows-x64.zip"
$windowsArchive = New-Archive $windowsArchivePath
try {
    Add-ArchiveFile $windowsArchive $executableFile.FullName 'QingTou.exe'
    foreach ($relativePath in @('LICENSE','THIRD_PARTY_NOTICES.md','docs/USAGE.md','docs/USAGE.en.md','docs/toolbar-cutout.png')) {
        Add-ArchiveFile $windowsArchive (Join-Path $projectRoot $relativePath) $relativePath
    }
    foreach ($licenseFile in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'licenses') -File) {
        Add-ArchiveFile $windowsArchive $licenseFile.FullName ('licenses/' + $licenseFile.Name)
    }
    $entry = $windowsArchive.CreateEntry('README.txt')
    $writer = [System.IO.StreamWriter]::new($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine("轻透截图 / QingTou $releaseVersion (Windows x64)")
        $writer.WriteLine('')
        $writer.WriteLine('解压到可写目录，运行 QingTou.exe。更新前从托盘退出旧版。')
        $writer.WriteLine('Extract to a writable folder and run QingTou.exe. Exit the old instance before updating.')
        $writer.WriteLine('')
        $writer.WriteLine('截图 / Capture: Ctrl + Alt + Q')
        $writer.WriteLine('GIF: Ctrl + Alt + G')
        $writer.WriteLine('保存 PNG / Save PNG: Ctrl + S')
        $writer.WriteLine('退出当前截图 / Cancel capture: Esc')
        $writer.WriteLine('')
        $writer.WriteLine('中文指南: docs/USAGE.md | English guide: docs/USAGE.en.md')
        $writer.WriteLine('License: LICENSE | Runtime and asset notices: THIRD_PARTY_NOTICES.md')
        $writer.WriteLine('https://github.com/HaoranSun1122/QingTou_PrintScreen')
    } finally { $writer.Dispose() }
} finally { $windowsArchive.Dispose() }

$sourceArchivePath = Join-Path $releaseDirectory "QingTou-v$releaseVersion-source.zip"
$sourceArchive = New-Archive $sourceArchivePath
try {
    $sourcePaths = @('.github','src','resources','tests','tools','docs','licenses','.gitignore','.gitattributes',
        'CMakeLists.txt','build.ps1','README.md','README.en.md','LICENSE','CHANGELOG.md',
        'CONTRIBUTING.md','SECURITY.md','THIRD_PARTY_NOTICES.md')
    foreach ($sourcePath in $sourcePaths) {
        $sourceItem = Get-Item -Force -LiteralPath (Join-Path $projectRoot $sourcePath)
        $sourceFiles = if ($sourceItem.PSIsContainer) { Get-ChildItem -LiteralPath $sourceItem.FullName -Recurse -File -Force } else { @($sourceItem) }
        foreach ($sourceFile in $sourceFiles) {
            $relativePath = $sourceFile.FullName.Substring($projectRoot.Length + 1).Replace('\','/')
            if ($relativePath -match '(^|/)(\.git|captures|build|test-output|__pycache__)(/|$)|\.(ini|tmp|pyc|exe|zip)$') { continue }
            Add-ArchiveFile $sourceArchive $sourceFile.FullName $relativePath
        }
    }
} finally { $sourceArchive.Dispose() }

$checksumLines = foreach ($filePath in @($windowsArchivePath,$sourceArchivePath,$executableFile.FullName)) {
    $fileHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $fileName = [System.IO.Path]::GetFileName($filePath)
    "$fileHash  $fileName"
}
$checksumLines | Set-Content -LiteralPath (Join-Path $releaseDirectory 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Packaged QingTou $releaseVersion (x64) with license, guides and checksums."
Write-Host $windowsArchivePath
Write-Host $sourceArchivePath
