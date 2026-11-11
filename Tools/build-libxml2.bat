@echo off
rem BUSMASTER-NG: builds Sources\BUSMASTER\BIN\Release\libxml2.dll for the Windows 7 build (see Sources\Directory.Build.props).
rem
rem libxml2 2.9.4, the version of the headers and import library in Sources\BUSMASTER\EXTERNAL\libxml2, built with the
rem Visual Studio 2013 compiler (runtime msvcr120) and /arch:SSE, so that it runs on processors without SSE2. No iconv and
rem no zlib, like the DLL shipped before; the exports are the same, so BUSMASTER does not need to be rebuilt.
rem
rem Sources: https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.4.tar.xz
rem          SHA-256 21b2824f9ce56a72296f678103eed5a8446417c07e904e61748affa230ec9072
rem
rem Usage:   Tools\build-libxml2.bat <folder of the extracted libxml2-2.9.4 sources>
rem The DLL is built in <folder>\win32\bin.msvc and copied to Sources\BUSMASTER\BIN\Release.

if "%~1"=="" (
    echo Usage: %~nx0 ^<libxml2-2.9.4 source folder^>
    exit /b 1
)
setlocal
set ROOT=%~dp0..
call "%ProgramFiles(x86)%\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86 || exit /b 1
rem cl.exe adds the options of the CL variable to every call
set CL=/arch:SSE
cd /d "%~1\win32" || exit /b 1
cscript //nologo configure.js compiler=msvc iconv=no zlib=no cruntime=/MD debug=no || exit /b 1
nmake /nologo /f Makefile.msvc libxml || exit /b 1
copy /y bin.msvc\libxml2.dll "%ROOT%\Sources\BUSMASTER\BIN\Release\libxml2.dll" || exit /b 1
echo libxml2.dll copied to Sources\BUSMASTER\BIN\Release
