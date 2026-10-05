<#
.SYNOPSIS
  Builds the portable Windows ZIP of ScanTailor OCR from a finished build.

.DESCRIPTION
  Copies the program, exactly the DLLs it needs, the Qt plugins, the translations, the
  OCR language files and the license texts into a new folder, test-starts a copy of it
  with a minimal PATH and packs it into a ZIP.

  Which DLLs are needed is determined from the import tables of the program and its
  plugins, so test libraries in the build folder (Qt6Test, Boost.Test) are left out.
  The license texts are taken from vcpkg: for every shipped DLL, the copyright file of
  the vcpkg port that installed it.

  Run it in Windows PowerShell 5.1 or PowerShell 7 after a successful Release build:

    powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -Version 1.0.0

.PARAMETER Version
  The release version, e.g. "1.0.0".  Becomes part of the folder and ZIP name.

.PARAMETER BuildDir
  The build folder.  Default: "build" in the repository.

.PARAMETER VcpkgRoot
  The vcpkg folder.  Default: the VCPKG_ROOT environment variable.

.PARAMETER Triplet
  The vcpkg triplet.  Default: x64-windows.

.PARAMETER CrtDir
  The folder with the Microsoft C++ runtime DLLs to ship (…\VC\Redist\MSVC\<version>\x64\
  Microsoft.VC<nnn>.CRT).  Default: found through vswhere.

.PARAMETER Languages
  The OCR languages to ship from <BuildDir>\tessdata.  Default: deu, eng.

.PARAMETER OutputDir
  Where the folder and the ZIP are created.  Default: <BuildDir>\package.

.PARAMETER SkipStartTest
  Don't test-start the program (it opens its main window for a few seconds).
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = "",
  [string]$VcpkgRoot = $env:VCPKG_ROOT,
  [string]$Triplet = "x64-windows",
  [string]$CrtDir = "",
  [string[]]$Languages = @("deu", "eng"),
  [string]$OutputDir = "",
  [switch]$SkipStartTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot "build" }
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $OutputDir) { $OutputDir = Join-Path $BuildDir "package" }
$exeName = "scantailor-ocr.exe"
$packageName = "ScanTailor-OCR-$Version-win64"

function Fail([string]$message) {
  Write-Host ""
  Write-Host "ERROR: $message" -ForegroundColor Red
  exit 1
}

function Step([string]$message) {
  Write-Host ""
  Write-Host "== $message" -ForegroundColor Cyan
}

# ---------------------------------------------------------------------------
# Reading the DLL names a PE file (exe or dll) imports, including delay-loaded ones.

function Get-PeImports([string]$Path) {
  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $pe = [BitConverter]::ToInt32($bytes, 0x3C)
  if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) { throw "Not a PE file: $Path" }
  $coff = $pe + 4
  $sectionCount = [BitConverter]::ToUInt16($bytes, $coff + 2)
  $optionalSize = [BitConverter]::ToUInt16($bytes, $coff + 16)
  $optional = $coff + 20
  $is64 = ([BitConverter]::ToUInt16($bytes, $optional) -eq 0x20B)
  if ($is64) {
    $imageBase = [BitConverter]::ToUInt64($bytes, $optional + 24)
    $dataDirs = $optional + 112
  } else {
    $imageBase = [uint64][BitConverter]::ToUInt32($bytes, $optional + 28)
    $dataDirs = $optional + 96
  }
  $sections = $optional + $optionalSize

  $toOffset = {
    param([uint64]$rva)
    for ($i = 0; $i -lt $sectionCount; $i++) {
      $s = $sections + 40 * $i
      $virtualSize = [BitConverter]::ToUInt32($bytes, $s + 8)
      $virtualAddress = [BitConverter]::ToUInt32($bytes, $s + 12)
      $rawSize = [BitConverter]::ToUInt32($bytes, $s + 16)
      $rawPointer = [BitConverter]::ToUInt32($bytes, $s + 20)
      $size = [Math]::Max($virtualSize, $rawSize)
      if (($rva -ge $virtualAddress) -and ($rva -lt ($virtualAddress + $size))) {
        return [int64]($rva - $virtualAddress + $rawPointer)
      }
    }
    return [int64]-1
  }
  $readName = {
    param([int64]$offset)
    $end = [Array]::IndexOf($bytes, [byte]0, [int]$offset)
    return [System.Text.Encoding]::ASCII.GetString($bytes, [int]$offset, $end - [int]$offset)
  }

  $names = New-Object System.Collections.Generic.List[string]

  # Regular imports: data directory 1, descriptors of 20 bytes, name RVA at +12.
  $importRva = [BitConverter]::ToUInt32($bytes, $dataDirs + 8)
  if ($importRva -ne 0) {
    $offset = & $toOffset $importRva
    while ($offset -ge 0) {
      $nameRva = [BitConverter]::ToUInt32($bytes, [int]$offset + 12)
      if ($nameRva -eq 0) { break }
      $names.Add((& $readName (& $toOffset $nameRva)))
      $offset += 20
    }
  }

  # Delay-loaded imports: data directory 13, descriptors of 32 bytes, name at +4.
  $delayRva = [BitConverter]::ToUInt32($bytes, $dataDirs + 13 * 8)
  if ($delayRva -ne 0) {
    $offset = & $toOffset $delayRva
    while ($offset -ge 0) {
      $attributes = [BitConverter]::ToUInt32($bytes, [int]$offset)
      $nameRef = [uint64][BitConverter]::ToUInt32($bytes, [int]$offset + 4)
      if ($nameRef -eq 0) { break }
      # Old style descriptors contain virtual addresses instead of RVAs.
      if (($attributes -band 1) -eq 0) { $nameRef = $nameRef - $imageBase }
      $names.Add((& $readName (& $toOffset $nameRef)))
      $offset += 32
    }
  }
  return $names
}

# ---------------------------------------------------------------------------
Step "Checking the inputs"

$exePath = Join-Path $BuildDir $exeName
if (-not (Test-Path $exePath)) { Fail "$exePath doesn't exist. Build the program first." }
if (-not $VcpkgRoot -or -not (Test-Path $VcpkgRoot)) { Fail "vcpkg not found. Pass -VcpkgRoot or set VCPKG_ROOT." }
$vcpkgInstalled = Join-Path $VcpkgRoot "installed"
$vcpkgInfo = Join-Path $vcpkgInstalled "vcpkg\info"
if (-not (Test-Path $vcpkgInfo)) { Fail "$vcpkgInfo doesn't exist." }
# Qt's own translations (standard buttons, dialogs, context menus), from vcpkg's "qttranslations".
$qtTranslationsDir = Join-Path $vcpkgInstalled "$Triplet\translations\Qt6"
if (-not (Test-Path (Join-Path $qtTranslationsDir "qtbase_de.qm"))) {
  Fail "Qt's translations not found in $qtTranslationsDir. Install them: vcpkg install qttranslations"
}

if (-not $CrtDir) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found. Pass -CrtDir." }
  $vsPath = & $vswhere -latest -products * -property installationPath
  $crtCandidates = @(Get-ChildItem -Path (Join-Path $vsPath "VC\Redist\MSVC") -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem -Path (Join-Path $_.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue } |
    Sort-Object FullName -Descending)
  if ($crtCandidates.Count -eq 0) { Fail "No Microsoft.VC*.CRT folder found below $vsPath\VC\Redist\MSVC. Pass -CrtDir." }
  $CrtDir = $crtCandidates[0].FullName
}
if (-not (Test-Path $CrtDir)) { Fail "$CrtDir doesn't exist." }
Write-Host "Build:   $BuildDir"
Write-Host "vcpkg:   $VcpkgRoot ($Triplet)"
Write-Host "Runtime: $CrtDir"

$tessdataDir = Join-Path $BuildDir "tessdata"
foreach ($language in $Languages) {
  $file = Join-Path $tessdataDir "$language.traineddata"
  if (-not (Test-Path $file)) { Fail "$file doesn't exist." }
}

# ---------------------------------------------------------------------------
Step "Creating $packageName"

$packageDir = Join-Path $OutputDir $packageName
$zipPath = Join-Path $OutputDir "$packageName.zip"
if (Test-Path $packageDir) { Remove-Item -Recurse -Force $packageDir }
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
New-Item -ItemType Directory -Force $packageDir | Out-Null

# The program and the Qt plugins it needs.
Copy-Item $exePath $packageDir
$pluginFiles = New-Object System.Collections.Generic.List[string]
foreach ($group in @("platforms", "styles", "imageformats", "iconengines")) {
  $source = Join-Path $BuildDir $group
  if (-not (Test-Path $source)) { Fail "$source doesn't exist." }
  New-Item -ItemType Directory -Force (Join-Path $packageDir $group) | Out-Null
  foreach ($dll in Get-ChildItem -Path $source -Filter "*.dll" -File) {
    Copy-Item $dll.FullName (Join-Path $packageDir $group)
    $pluginFiles.Add((Join-Path (Join-Path $packageDir $group) $dll.Name))
  }
}
# TLS: Windows' own implementation only.  The OpenSSL plugin can't load without OpenSSL,
# and Qt then stops loading the other TLS plugins.
New-Item -ItemType Directory -Force (Join-Path $packageDir "tls") | Out-Null
foreach ($name in @("qschannelbackend.dll", "qcertonlybackend.dll")) {
  $source = Join-Path $BuildDir "tls\$name"
  if (-not (Test-Path $source)) { Fail "$source doesn't exist." }
  Copy-Item $source (Join-Path $packageDir "tls")
  $pluginFiles.Add((Join-Path $packageDir "tls\$name"))
}

# The DLLs from the build folder that the program and the plugins load, directly or indirectly.
$available = @{}
foreach ($dll in Get-ChildItem -Path $BuildDir -Filter "*.dll" -File) { $available[$dll.Name.ToLowerInvariant()] = $dll.FullName }
$needed = @{}
$queue = New-Object System.Collections.Generic.Queue[string]
$queue.Enqueue($exePath)
foreach ($plugin in $pluginFiles) { $queue.Enqueue($plugin) }
while ($queue.Count -gt 0) {
  foreach ($import in Get-PeImports $queue.Dequeue()) {
    $key = $import.ToLowerInvariant()
    if ($available.ContainsKey($key) -and -not $needed.ContainsKey($key)) {
      $needed[$key] = $available[$key]
      $queue.Enqueue($available[$key])
    }
  }
}
foreach ($path in $needed.Values) { Copy-Item $path $packageDir }
Write-Host ("{0} DLLs from the build folder" -f $needed.Count)

# The Microsoft C++ runtime.
Get-ChildItem -Path $CrtDir -Filter "*.dll" -File | ForEach-Object { Copy-Item $_.FullName $packageDir }

# Translations: the program's own, and Qt's for the same languages (e.g. "qtbase_pt_BR.qm" for "pt").
$qtTranslationsTarget = Join-Path $packageDir "translations"
New-Item -ItemType Directory -Force $qtTranslationsTarget | Out-Null
foreach ($qm in Get-ChildItem -Path $BuildDir -Filter "scantailor-ocr_*.qm" -File) {
  Copy-Item $qm.FullName $packageDir
  $language = $qm.BaseName.Substring("scantailor-ocr_".Length)
  $qtFiles = @(Get-ChildItem -Path $qtTranslationsDir -Filter "qtbase_$language*.qm" -File)
  if ($qtFiles.Count -eq 0) { Write-Host "No Qt translation for $language" }
  foreach ($qtFile in $qtFiles) { Copy-Item $qtFile.FullName $qtTranslationsTarget }
}

# OCR languages.
New-Item -ItemType Directory -Force (Join-Path $packageDir "tessdata") | Out-Null
foreach ($language in $Languages) {
  Copy-Item (Join-Path $tessdataDir "$language.traineddata") (Join-Path $packageDir "tessdata")
}

# ---------------------------------------------------------------------------
Step "Collecting license texts"

Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $packageDir "LICENSE.txt")
$licensesDir = Join-Path $packageDir "licenses"
New-Item -ItemType Directory -Force $licensesDir | Out-Null

# Which vcpkg port installed which DLL, from vcpkg's lists of installed files.
$portOfFile = @{}
foreach ($list in Get-ChildItem -Path $vcpkgInfo -Filter "*_$Triplet.list" -File) {
  $port = $list.Name.Substring(0, $list.Name.IndexOf("_"))
  foreach ($line in [System.IO.File]::ReadAllLines($list.FullName)) {
    if ($line.EndsWith(".dll", [StringComparison]::OrdinalIgnoreCase) -and
        ($line.StartsWith("$Triplet/bin/") -or $line.StartsWith("$Triplet/Qt6/plugins/"))) {
      $portOfFile[[System.IO.Path]::GetFileName($line).ToLowerInvariant()] = $port
    }
  }
}

$components = New-Object System.Collections.Generic.List[string]
$ports = @{}
$shippedDlls = Get-ChildItem -Path $packageDir -Filter "*.dll" -File -Recurse
foreach ($dll in $shippedDlls) {
  $key = $dll.Name.ToLowerInvariant()
  $relative = $dll.FullName.Substring($packageDir.Length + 1)
  if ($portOfFile.ContainsKey($key)) {
    $port = $portOfFile[$key]
    $ports[$port] = $true
    $components.Add("$relative  ->  licenses\$port.txt")
  } elseif (Test-Path (Join-Path $CrtDir $dll.Name)) {
    $components.Add("$relative  ->  Microsoft Visual C++ Redistributable (Visual Studio license terms)")
  } else {
    Fail "No vcpkg port found for $relative, so its license is unknown."
  }
}
foreach ($port in ($ports.Keys | Sort-Object)) {
  $copyright = Join-Path $vcpkgInstalled "$Triplet\share\$port\copyright"
  if (-not (Test-Path $copyright)) { Fail "$copyright doesn't exist." }
  Copy-Item $copyright (Join-Path $licensesDir "$port.txt")
}

$tessdataNote = @"
The OCR language files in the tessdata folder are from the tessdata_best repository of the
Tesseract project, https://github.com/tesseract-ocr/tessdata_best, and licensed under the
Apache License, Version 2.0 (see tesseract.txt).

The program contains the font "GlyphLessFont" (pdf.ttf) of Tesseract, also licensed under the
Apache License, Version 2.0.
"@
Set-Content -Path (Join-Path $licensesDir "tessdata_best.txt") -Value $tessdataNote -Encoding UTF8

# Compiled into the program, not a DLL: the JBIG2 arithmetic coder of jbig2enc.
$jbig2License = Join-Path $repoRoot "src\core\jbig2enc\LICENSE"
if (-not (Test-Path $jbig2License)) {
  Fail "Missing $jbig2License."
}
$jbig2Note = @"
The program contains the JBIG2 arithmetic coder of jbig2enc, https://github.com/agl/jbig2enc,
Copyright 2006 Google Inc., Author: Adam Langley, licensed under the Apache License,
Version 2.0 (below).  It was shortened to the parts for lossless generic region coding.

"@
Set-Content -Path (Join-Path $licensesDir "jbig2enc.txt") -Value ($jbig2Note + (Get-Content $jbig2License -Raw)) -Encoding UTF8

$componentsHeader = @(
  "Third-party components shipped with ScanTailor OCR and their license texts.",
  "ScanTailor OCR itself is licensed under the GNU GPL v3, see LICENSE.txt.",
  "",
  "scantailor-ocr.exe (JBIG2 coder from jbig2enc)  ->  licenses\jbig2enc.txt",
  "tessdata\*.traineddata  ->  licenses\tessdata_best.txt",
  ""
)
Set-Content -Path (Join-Path $licensesDir "components.txt") -Value ($componentsHeader + ($components | Sort-Object)) -Encoding UTF8
Write-Host ("{0} license texts" -f $ports.Count)

# ---------------------------------------------------------------------------
Step "Checking the package contents"

$forbidden = @("*.pdb", "qopensslbackend*", "Qt6Test*", "boost_unit_test*", "*.ScanTailor")
foreach ($pattern in $forbidden) {
  $found = @(Get-ChildItem -Path $packageDir -Filter $pattern -Recurse -File)
  if ($found.Count -gt 0) { Fail "The package contains $($found[0].FullName)." }
}
foreach ($dirName in @("config", "testdaten")) {
  if (Test-Path (Join-Path $packageDir $dirName)) { Fail "The package contains the folder $dirName." }
}
$fileCount = @(Get-ChildItem -Path $packageDir -Recurse -File).Count
Write-Host "$fileCount files"

# ---------------------------------------------------------------------------
if (-not $SkipStartTest) {
  Step "Test-starting a copy with a minimal PATH"

  # A copy, because the program creates a "config" folder next to itself.
  $testDir = Join-Path ([System.IO.Path]::GetTempPath()) ("stocr-starttest-" + [Guid]::NewGuid().ToString("N"))
  Copy-Item -Recurse $packageDir $testDir
  $savedPath = $env:PATH
  try {
    $env:PATH = Join-Path $env:SystemRoot "System32"
    $process = Start-Process -FilePath (Join-Path $testDir $exeName) -PassThru
  } finally {
    $env:PATH = $savedPath
  }
  Start-Sleep -Seconds 6
  if ($process.HasExited) { Fail "The program exited right after starting (exit code $($process.ExitCode))." }

  $process.Refresh()
  $foreign = @()
  foreach ($module in $process.Modules) {
    $path = $module.FileName
    $fromPackage = $path.StartsWith($testDir, [StringComparison]::OrdinalIgnoreCase)
    $fromWindows = $path.StartsWith($env:SystemRoot, [StringComparison]::OrdinalIgnoreCase)
    if (-not $fromPackage -and -not $fromWindows) { $foreign += $path }
  }
  $moduleCount = @($process.Modules).Count
  Stop-Process -Id $process.Id -Force
  Start-Sleep -Seconds 1
  Remove-Item -Recurse -Force $testDir -ErrorAction SilentlyContinue

  if ($foreign.Count -gt 0) {
    Write-Host "Modules loaded from outside the package and Windows:" -ForegroundColor Yellow
    $foreign | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    Fail "The program uses files that aren't in the package."
  }
  Write-Host "Started fine; $moduleCount modules, all from the package or from Windows."
}

# ---------------------------------------------------------------------------
Step "Packing the ZIP"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# ZipFile.CreateFromDirectory() of Windows PowerShell 5.1 writes "\" into the entry names,
# which other tools don't understand.  So the entries are written one by one, with "/".
$zipStream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
try {
  $archive = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($file in Get-ChildItem -Path $packageDir -Recurse -File) {
      $entryName = $packageName + "/" + $file.FullName.Substring($packageDir.Length + 1).Replace("\", "/")
      [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $file.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
  } finally {
    $archive.Dispose()
  }
} finally {
  $zipStream.Dispose()
}

$zipInfo = Get-Item $zipPath
$hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash
Write-Host ""
Write-Host "Done: $zipPath" -ForegroundColor Green
Write-Host ("Size:   {0:N1} MB" -f ($zipInfo.Length / 1MB))
Write-Host "SHA256: $hash"
