; Alfred installer for Windows (Inno Setup 6). The version comes from the build: ISCC /DAppVer=0.27 /DFileVer=0.27_beta
#ifndef AppVer
  #define AppVer "0.33 beta"
#endif
#ifndef FileVer
  #define FileVer "0.33_beta"
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

[Files]
Source: "..\..\build\Alfred_artefacts\Release\VST3\Alfred.vst3\*"; DestDir: "{commoncf64}\VST3\Alfred.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\Alfred_artefacts\Release\CLAP\Alfred.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion
; neural voice engine: ONNX Runtime and the model, shared by both plug-in formats
Source: "..\..\support\*"; DestDir: "{commonappdata}\dmajorsound\Alfred"; Flags: ignoreversion

[Messages]
WelcomeLabel1=Alfred%nA simple Denoiser at your Service
WelcomeLabel2=This installs Alfred as VST3 and CLAP plug-in into the common plug-in folders.%n%nAfterwards, rescan your plug-ins in REAPER.
