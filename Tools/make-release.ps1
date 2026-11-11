<#
  BUSMASTER-NG: builds the release packages from a finished build in Sources\BUSMASTER\BIN\Release.

  The build is the Windows 7 compatible one (Visual Studio 2013 toolset without SSE2, see Sources\Directory.Build.props;
  the runtime copied next to the executable follows the one BUSMASTER.exe imports). From it this script makes:
    BUSMASTER-NG_<v>_win32.zip        portable, Windows 10 / 11
    BUSMASTER-NG_<v>_win32_win7.zip   portable, Windows 7 SP1: the same files plus the Universal CRT (ucrtbase.dll and
                                      the api-ms-win-*.dll forwarders). Windows 10 and later always use their own
                                      UCRT and ignore these copies, so the Windows 10 / 11 package leaves them out.
    BUSMASTER-NG_<v>_setup.exe        installer for all of them (with -Installer, needs Inno Setup 6): it adds the
                                      UCRT only on Windows 7, and the Visual C++ 2013 runtime if -VCRedist2013 is given.
  All packages have the C++ / MFC runtime of the toolset next to the executable, plus the files the original installer
  used to add: node simulation files, MHS driver library, examples, license texts and third-party notices.

  Usage (Windows PowerShell):
    .\tools\make-release.ps1 -Version 3.3.1
    .\tools\make-release.ps1 -Version 3.3.1 -MinGW C:\Tools\mingw32     # also builds the node simulation libraries
    .\tools\make-release.ps1 -Version 3.5.0 -Installer

  The node simulation libraries (SimulatedSystems\OBJ\libWrapper_*.a) are built with MinGW from
  Sources\BUSMASTER\Application\SimulatedSystems\include. Build them with the same MinGW version the wiki asks users
  to install. Without -MinGW, existing libraries are used; if there are none, the package is made without them and a
  warning is shown.

  -VCRedist2013 is the Visual C++ 2013 Redistributable (x86) from Microsoft (vcredist_x86.exe, 12.0.40664,
  https://aka.ms/highdpimfc2013x86enu). The installer runs it only when the runtime is missing. By default it is taken
  from Tools\redist\vcredist2013_x86.exe (not in git); without it, the installer is made without the runtime and a
  warning is shown.
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$OutDir = "",
    [string]$MinGW = "",
    [string]$VCToolsVersion = "14.38.33130",
    [switch]$Installer,
    [string]$VCRedist2013 = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "Sources\BUSMASTER\BIN\Release"
$app = Join-Path $root "Sources\BUSMASTER\Application"
if ($OutDir -eq "") { $OutDir = Join-Path $root "release-out" }
$name = "BUSMASTER-NG_$Version"
$stage = Join-Path $OutDir $name
$stage7 = Join-Path $OutDir "${name}_win7"
$ucrtStage = Join-Path $OutDir "${name}_ucrt"
$zip = Join-Path $OutDir "${name}_win32.zip"
$zip7 = Join-Path $OutDir "${name}_win32_win7.zip"

if (-not (Test-Path (Join-Path $bin "BUSMASTER.exe"))) { throw "No build found in $bin" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($dir in $stage, $stage7, $ucrtStage) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }

# 1. the build, without build by-products
Write-Host "Copying the build"
robocopy $bin $stage /E /NJH /NJS /NDL /NFL /NP /XF *.pdb *.ilk *.exp *.lib *.pch *.map *.iobj *.ipdb *.recipe *.log *.tlog *.chw *.idb *.obj | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# 2. C++ / MFC runtime of the toolset next to the executable; the Universal CRT apart, for Windows 7 only
# The Windows 7 build (Visual Studio 2013 toolset, see Sources\Directory.Build.props) uses the 120 runtime, the default
# build the one of Visual Studio 2022: BUSMASTER.exe imports mfc120.dll or mfc140.dll.
$exeText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes((Join-Path $bin "BUSMASTER.exe")))
if ($exeText -match "(?i)mfc120\.dll") {
    Write-Host "Visual Studio 2013 build: 120 runtime"
    $redist = "${env:ProgramFiles(x86)}\Microsoft Visual Studio 12.0\VC\redist\x86"
    $runtimeDirs = "Microsoft.VC120.CRT", "Microsoft.VC120.MFC"
}
else {
    # Visual Studio 2022 (version 17), the one the build uses: a newer Visual Studio installed beside it has other runtimes
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -version "[17.0,18.0)" -property installationPath
    $redist = Join-Path $vs "VC\Redist\MSVC\$VCToolsVersion\x86"
    $runtimeDirs = "Microsoft.VC143.CRT", "Microsoft.VC143.MFC"
}
foreach ($sub in $runtimeDirs) {
    $dir = Join-Path $redist $sub
    if (-not (Test-Path $dir)) { throw "Runtime not found: $dir" }
    Copy-Item "$dir\*.dll" $stage
}
$ucrt = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\*\ucrt\DLLs\x86" -Directory | Sort-Object FullName | Select-Object -Last 1
if ($null -eq $ucrt) { throw "Universal CRT redistributable not found (Windows 10 SDK)" }
New-Item -ItemType Directory -Force $ucrtStage | Out-Null
Copy-Item "$($ucrt.FullName)\*.dll" $ucrtStage

# 3. node simulation: makefile template, headers and helper libraries
Write-Host "Node simulation files"
Copy-Item (Join-Path $app "GCCDLLMakeTemplate") $stage
$simInc = Join-Path $stage "SimulatedSystems\include"
$simObj = Join-Path $stage "SimulatedSystems\OBJ"
New-Item -ItemType Directory -Force $simInc, $simObj | Out-Null
Copy-Item (Join-Path $app "SimulatedSystems\include\*.h") $simInc
$libSrc = Join-Path $app "SimulatedSystems\OBJ"
if ($MinGW -ne "") {
    $gcc = Join-Path $MinGW "bin"
    if (-not (Test-Path (Join-Path $gcc "g++.exe"))) { throw "g++.exe not found in $gcc" }
    New-Item -ItemType Directory -Force $libSrc | Out-Null
    $inc = Join-Path $app "SimulatedSystems\include"
    foreach ($bus in "CAN", "J1939", "LIN") {
        Write-Host "  building libWrapper_$bus.a"
        $lib = Join-Path $libSrc "libWrapper_$bus.a"
        if (Test-Path $lib) { Remove-Item $lib }
        # "call .\" is needed: a bare batch name is not found when cmd is started from PowerShell with this PATH
        # (stderr merged into stdout inside cmd: the scripts first delete files that may not exist yet)
        cmd /c "set PATH=$gcc;%PATH% && cd /d `"$inc`" && call .\MakeFile_$bus.bat 2>&1" | Out-Null
        if (-not (Test-Path $lib)) { throw "libWrapper_$bus.a was not built" }
    }
}
$libs = Get-ChildItem $libSrc -Filter "libWrapper_*.a" -ErrorAction SilentlyContinue
if ($libs.Count -eq 3) { Copy-Item $libs.FullName $simObj }
else { Write-Warning "Node simulation libraries missing in ${libSrc}: run again with -MinGW <folder>" }

# 4. other files of the original installer; license texts and change list in Docs, to keep the folder readable
Copy-Item (Join-Path $root "Sources\BUSMASTER\CAN_MHS\EXTERNAL\mhstcan.dll") $stage
Copy-Item (Join-Path $root "Examples") $stage -Recurse
$docs = Join-Path $stage "Docs"
New-Item -ItemType Directory -Force $docs | Out-Null
foreach ($file in "COPYING.LESSER.txt", "COPYING.txt", "DBManager_License.txt", "CHANGELOG.md") {
    Copy-Item (Join-Path $root $file) $docs
}
Copy-Item (Join-Path $PSScriptRoot "third-party-notices.txt") (Join-Path $docs "THIRD-PARTY-NOTICES.txt")
# the commit list of the original BUSMASTER up to v3.2.x would be mistaken for the changes of this version
Remove-Item (Join-Path $stage "Changelog.txt") -ErrorAction SilentlyContinue
$readme = (Get-Content (Join-Path $PSScriptRoot "release-readme.txt") -Raw).Replace("{VERSION}", $Version)

# 5. the two portable packages: Windows 10 / 11, and Windows 7 with the UCRT
Write-Host "Portable packages"
$readme.Replace("{EDITION}", "Windows 10 / 11") | Set-Content (Join-Path $stage "README.txt") -Encoding ASCII
robocopy $stage $stage7 /E /NJH /NJS /NDL /NFL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
Copy-Item "$ucrtStage\*.dll" $stage7
$readme.Replace("{EDITION}", "Windows 7 SP1") | Set-Content (Join-Path $stage7 "README.txt") -Encoding ASCII
foreach ($pkg in @(@($stage, $zip), @($stage7, $zip7))) {
    if (Test-Path $pkg[1]) { Remove-Item $pkg[1] }
    Compress-Archive -Path $pkg[0] -DestinationPath $pkg[1] -CompressionLevel Optimal
}
$packages = @($zip, $zip7)

# 6. installer (Inno Setup 6), from the Windows 10 / 11 files plus the UCRT for Windows 7
if ($Installer) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($null -eq $iscc) { throw "Inno Setup 6 not found (ISCC.exe): install it, e.g. winget install JRSoftware.InnoSetup" }
    # the file version must be numeric: 3.4.1-test -> 3.4.1
    $fileVersion = [regex]::Match($Version, '^\d+(\.\d+){0,3}').Value
    $defines = @("/DAppVersion=$Version", "/DFileVersion=$fileVersion", "/DStageDir=$stage", "/DUcrtDir=$ucrtStage", "/DOutDir=$OutDir",
                 "/DIconFile=$(Join-Path $app 'Res\BUSMASTER.ico')")
    $defaultRedist = Join-Path $PSScriptRoot "redist\vcredist2013_x86.exe"
    if ($VCRedist2013 -eq "" -and (Test-Path $defaultRedist)) { $VCRedist2013 = $defaultRedist }
    if ($VCRedist2013 -ne "") {
        if (-not (Test-Path $VCRedist2013)) { throw "Not found: $VCRedist2013" }
        $defines += "/DVCRedist2013=$((Resolve-Path $VCRedist2013).Path)"
    }
    else { Write-Warning "Installer made without the Visual C++ 2013 runtime: put it in $defaultRedist or use -VCRedist2013" }
    Write-Host "Installer"
    & $iscc /Q @defines (Join-Path $PSScriptRoot "installer.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)" }
    $packages += Join-Path $OutDir "${name}_setup.exe"
}

# 7. sizes and checksums
foreach ($pkg in $packages) {
    Write-Host ("{0} ({1:N1} MB)  SHA-256 {2}" -f (Split-Path $pkg -Leaf), ((Get-Item $pkg).Length / 1MB),
                (Get-FileHash $pkg -Algorithm SHA256).Hash)
}
