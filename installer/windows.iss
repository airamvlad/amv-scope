; Inno Setup script for the Windows installer.
; Built by GitHub Actions: iscc /DAppVersion=0.3.0 installer\windows.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6F2A9D3E-41B7-4C8A-9E15-AD27C3B85F40}
AppName=AMVScope
AppVersion={#AppVersion}
AppVerName=AMVScope {#AppVersion}
AppPublisher=AMVScope
AppPublisherURL=https://github.com/airamvlad/amv-scope
LicenseFile=..\LICENSE
; File properties of the installer itself (shown by Windows, checked when signing).
VersionInfoVersion={#AppVersion}
VersionInfoProductName=AMVScope
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=AMVScope Setup
VersionInfoCompany=AMVScope
VersionInfoCopyright=AGPL-3.0
DefaultDirName={commoncf64}\VST3\AMVScope.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
UninstallFilesDir={commonappdata}\AMVScope
OutputDir=..\dist
OutputBaseFilename=AMVScope-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern

[InstallDelete]
; Remove the previous version completely before copying the new one.
Type: filesandordirs; Name: "{commoncf64}\VST3\AMVScope.vst3"

[Files]
Source: "..\build\AMVScope_artefacts\Release\VST3\AMVScope.vst3\*"; DestDir: "{commoncf64}\VST3\AMVScope.vst3"; Flags: recursesubdirs ignoreversion

[Messages]
FinishedLabelNoIcons=AMVScope is installed. Open Bitwig and look for "AMVScope" in the plug-in list.
