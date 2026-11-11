; BUSMASTER-NG installer (Inno Setup 6). Made by make-release.ps1 -Installer, which passes:
;   AppVersion   version, e.g. 3.5.0 or 3.5.0-test
;   FileVersion  its numeric part, e.g. 3.5.0
;   StageDir     the Windows 10 / 11 package folder
;   UcrtDir      (default build only) the Universal CRT DLLs, installed only on Windows 7; the Windows 7 build
;                (Visual Studio 2013 toolset) does not use the Universal CRT and has its runtime in StageDir
;   OutDir       where BUSMASTER-NG_<version>_setup.exe is written
;   IconFile     BUSMASTER.ico
;   VCRedist2013 (optional) vcredist_x86.exe of Visual C++ 2013, run only when the runtime is missing

#ifndef AppVersion
  #error Run make-release.ps1 -Installer instead of compiling this script directly
#endif

[Setup]
; never change AppId: it is how Windows finds an earlier BUSMASTER-NG to upgrade or uninstall
AppId={{EBEBA200-89AC-4FB2-BC8F-B931C7753CE5}
AppName=BUSMASTER-NG
AppVersion={#AppVersion}
AppVerName=BUSMASTER-NG {#AppVersion}
AppPublisher=BUSMASTER-NG community
AppPublisherURL=https://github.com/teoCLMR/busmaster-ng
AppSupportURL=https://github.com/teoCLMR/busmaster-ng/issues
AppUpdatesURL=https://github.com/teoCLMR/busmaster-ng/releases
VersionInfoVersion={#FileVersion}
DefaultDirName={autopf}\BUSMASTER-NG
DefaultGroupName=BUSMASTER-NG
DisableProgramGroupPage=yes
; all users (administrator) by default, or only the current user without administrator rights
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=6.1sp1
OutputDir={#OutDir}
OutputBaseFilename=BUSMASTER-NG_{#AppVersion}_setup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\BUSMASTER.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Windows 10 and later always use their own Universal CRT: these copies are for Windows 7 only
#ifdef UcrtDir
Source: "{#UcrtDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion; OnlyBelowVersion: 10.0
#endif
#ifdef VCRedist2013
Source: "{#VCRedist2013}"; DestDir: "{tmp}"; DestName: "vcredist2013_x86.exe"; Flags: deleteafterinstall; Check: NeedsVCRedist2013
#endif

[Icons]
Name: "{group}\BUSMASTER-NG"; Filename: "{app}\BUSMASTER.exe"
Name: "{group}\Format Converter"; Filename: "{app}\FormatConverter.exe"
Name: "{group}\Read me"; Filename: "{app}\README.txt"
Name: "{group}\{cm:UninstallProgram,BUSMASTER-NG}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\BUSMASTER-NG"; Filename: "{app}\BUSMASTER.exe"; Tasks: desktopicon

[Run]
#ifdef VCRedist2013
Filename: "{tmp}\vcredist2013_x86.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Visual C++ 2013 runtime..."; Check: NeedsVCRedist2013; Flags: waituntilterminated
#endif
Filename: "{app}\BUSMASTER.exe"; Description: "{cm:LaunchProgram,BUSMASTER-NG}"; Flags: nowait postinstall skipifsilent

[Code]
// Visual C++ 2013 (x86) runtime: the 32-bit registry view, i.e. WOW6432Node on 64-bit Windows
function NeedsVCRedist2013: Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\12.0\VC\Runtimes\x86', 'Installed', Installed)
                 and (Installed = 1));
end;
