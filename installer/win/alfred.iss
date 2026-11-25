; Alfred installer for Windows (Inno Setup 6). The version comes from the build: ISCC /DAppVer=0.27 /DFileVer=0.27_beta
#ifndef AppVer
  #define AppVer "0.36 beta"
#endif
#ifndef FileVer
  #define FileVer "0.36_beta"
#endif

[Setup]
AppId={{3F7C2D10-6A4B-4E8E-9C51-A1F0ED000027}
AppName=Alfred
AppVersion={#AppVer}
AppPublisher=dmajorsound
AppPublisherURL=https://dmajorsound.com
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DirExistsWarning=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputBaseFilename=Alfred_{#FileVer}_installer_win64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=yes
UninstallDisplayName=Alfred (VST3, CLAP)
UninstallFilesDir={commonappdata}\dmajorsound\Alfred
WizardStyle=modern
WizardImageFile=wizard-100.bmp,wizard-200.bmp
WizardSmallImageFile=small-100.bmp,small-200.bmp
WizardImageStretch=no
DisableWelcomePage=no

; a page with check boxes: which plug-in formats to install
[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 (REAPER and most other hosts)"; Types: full custom
Name: "clap"; Description: "CLAP (REAPER, Bitwig Studio and other hosts that support CLAP)"; Types: full custom
Name: "neural"; Description: "Neural voice engine files (shared by both formats; without them Alfred uses its classic engine only)"; Types: full custom

[Files]
Source: "..\..\build\Alfred_artefacts\Release\VST3\Alfred.vst3\*"; DestDir: "{commoncf64}\VST3\Alfred.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\Alfred_artefacts\Release\CLAP\Alfred.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
; neural voice engine: ONNX Runtime and the model, shared by both plug-in formats
Source: "..\..\support\*"; DestDir: "{commonappdata}\dmajorsound\Alfred"; Components: neural; Flags: ignoreversion

[Messages]
WelcomeLabel1=Alfred%nA simple Denoiser at your Service
WelcomeLabel2=This installs Alfred as VST3 and CLAP plug-in into the common plug-in folders. On the next page you can choose which of them you want.%n%nAfterwards, rescan your plug-ins in REAPER.
