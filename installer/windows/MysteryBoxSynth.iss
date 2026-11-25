; Mystery Box Synthesizer by Raygun -- Windows installer (Inno Setup 6)
;   ISCC.exe /DSourceDir=<artefacts\Release> /DOutputDir=<dir> MysteryBoxSynth.iss
#ifndef SourceDir
  #define SourceDir "..\..\build-win\MysteryBoxSynth_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist\Windows"
#endif
#define Product "Mystery Box Synthesizer"
#define Version "1.0.0"

[Setup]
AppId={{6D3B9A52-6F0C-4B6E-9A3C-5B7E2F1D8C41}
AppName={#Product}
AppVersion={#Version}
AppPublisher=Raygun
DefaultDirName={autopf}\Raygun\{#Product}
DefaultGroupName=Raygun
OutputDir={#OutputDir}
OutputBaseFilename=MysteryBoxSynth_Setup
Compression=lzma2/ultra
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#Product}

[Types]
Name: "full"; Description: "VST3 plug-in + standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "app"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#SourceDir}\VST3\{#Product}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Product}.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#SourceDir}\Standalone\{#Product}.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{group}\{#Product}"; Filename: "{app}\{#Product}.exe"; Components: app
Name: "{group}\Uninstall {#Product}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#Product}.exe"; Description: "Launch {#Product}"; Components: app; Flags: postinstall nowait skipifsilent
