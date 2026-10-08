; Sigma Q one-click Windows installer (Inno Setup 6.3+)
#define AppName "Sigma Q"
#define Slug "SigmaQ"
#define AppVersion "0.1.0"
#define Publisher "Kone Labie"
#define BuildDir "..\build\SigmaQ_artefacts\Release"

[Setup]
AppId={{C7D1E0A2-4F3B-4B6C-8E21-30710E510002}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={commonpf64}\Common Files\VST3
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableReadyMemo=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=output
OutputBaseFilename={#Slug}-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
UsePreviousAppDir=no
UninstallDisplayName={#AppName}
CreateUninstallRegKey=yes

[Files]
Source: "{#BuildDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commonpf64}\Common Files\VST3\{#AppName}.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
    WizardForm.FinishedLabel.Caption :=
      'Sigma Q is installed!' + #13#10#13#10 +
      'Open Ableton Live or FL Studio and look for "Sigma Q" in your plugins.' + #13#10#13#10 +
      'Not showing up?' + #13#10 +
      '  Ableton: Preferences > Plug-ins > turn on "Use VST3 Plug-in System Folders", then Rescan.' + #13#10 +
      '  FL Studio: Options > Manage plugins > Find installed plugins.';
end;
