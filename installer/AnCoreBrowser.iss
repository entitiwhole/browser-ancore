; AnCore Browser — установщик Inno Setup 6
; Сборка: installer\build-installer.ps1  или  ISCC.exe /Qp installer\AnCoreBrowser.iss

#ifndef SourceRoot
  #define SourceRoot ".."
#endif

#define MyAppName "AnCore Browser"
#define MyAppShortName "AnCore"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "AnCore"
#define MyAppExeName "AnCoreBrowser.exe"
#define StagingDir SourceRoot + "\installer\staging"
#define RedistDir SourceRoot + "\installer\redist"
#define OutputDir SourceRoot + "\installer\output"

[Setup]
AppId={{A7C4E1B2-3F8D-4E91-9C2A-1B5D6E8F0A3C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppShortName}
DefaultGroupName={#MyAppShortName}
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=no
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=AnCoreBrowser-Setup-{#MyAppVersion}-x64
SetupIconFile={#SourceRoot}\resources\app.ico
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
MinVersion=10.0
ShowLanguageDialog=auto

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#StagingDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingDir}\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RedistDir}\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: WebView2InstallNeeded
Source: "{#RedistDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VcRedistInstallNeeded

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Установка WebView2 Runtime..."; Flags: waituntilterminated; Check: WebView2InstallNeeded
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Установка Visual C Redistributable..."; Flags: waituntilterminated; Check: VcRedistInstallNeeded
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  WebView2RegKey = 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  VcRedistMinMajor = 14;

function RegHasMinVersion(Root: Integer; SubKey, ValueName: String; MinVersion: String): Boolean;
var
  S: String;
begin
  Result := RegQueryStringValue(Root, SubKey, ValueName, S);
  if Result then
    Result := (CompareText(S, MinVersion) >= 0);
end;

function WebView2Installed: Boolean;
begin
  Result :=
    RegHasMinVersion(HKLM, WebView2RegKey, 'pv', '120.0.0.0') or
    RegHasMinVersion(HKCU, WebView2RegKey, 'pv', '120.0.0.0');
end;

function WebView2InstallNeeded: Boolean;
begin
  Result := not WebView2Installed;
end;

function VcRedistInstalled: Boolean;
begin
  Result :=
    RegHasMinVersion(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', 'v14.30.00000') or
    RegHasMinVersion(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64', 'Version', 'v14.30.00000');
end;

function VcRedistInstallNeeded: Boolean;
begin
  Result := not VcRedistInstalled;
end;

