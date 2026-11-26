; Alfred uninstaller for Windows (Inno Setup 6): removes Alfred as VST3 and CLAP, with its neural engine and
; Enhance models. Projects and audio stay untouched. ISCC /DAppVer=0.45 beta /DFileVer=0.45_beta
#ifndef AppVer
  #define AppVer "0.45 beta"
#endif
#ifndef FileVer
  #define FileVer "0.45_beta"
#endif

[Setup]
AppId=Alfred_Uninstaller
AppName=Alfred Uninstaller
AppVersion={#AppVer}
AppPublisher=dmajorsound
AppPublisherURL=https://dmajorsound.com
CreateAppDir=no
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
OutputBaseFilename=Alfred_{#FileVer}_uninstaller_win64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=no
CloseApplications=no
WizardStyle=modern
WizardImageFile=wizard-100.bmp,wizard-200.bmp
WizardSmallImageFile=small-100.bmp,small-200.bmp
WizardImageStretch=no
DisableWelcomePage=no

[Files]
Source: "welcome-icon-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-icon-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy

[Messages]
SetupWindowTitle=Uninstall Alfred
WelcomeLabel1=Uninstall Alfred
WelcomeLabel2=This removes Alfred as VST3 and CLAP plug-in from the common plug-in folders, with its neural engine and Enhance models.%n%nYour projects and audio stay untouched.%n%nQuit your DAW first.
WizardReady=Ready to uninstall
ReadyLabel1=Alfred will now be removed.
ReadyLabel2a=Click Uninstall to continue.
ReadyLabel2b=Click Uninstall to continue.
ButtonInstall=&Uninstall
WizardInstalling=Uninstalling
InstallingLabel=Please wait while Alfred is removed.
FinishedHeadingLabel=Alfred is removed
FinishedLabelNoIcons=Alfred has been removed. Rescan your plug-ins in your DAW.
FinishedLabel=Alfred has been removed. Rescan your plug-ins in your DAW.

[Code]
#include "branding.iss"

procedure InitializeWizard();
begin
  BrandWelcome('Uninstall Alfred',
    'This removes Alfred as VST3 and CLAP plug-in from the common plug-in folders, with its neural engine and Enhance models. Your projects and audio stay untouched.' + #13#10 + #13#10 +
    'Quit your DAW first.');
end;

procedure Gone(Path: String; IsDir: Boolean; var Failed: Boolean);
begin
  if IsDir then begin
    if DirExists(Path) and not DelTree(Path, True, True, True) then Failed := True;
  end else begin
    if FileExists(Path) and not DeleteFile(Path) then Failed := True;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Unins: String;
  Code: Integer;
  Failed: Boolean;
begin
  if CurStep <> ssInstall then exit;
  Failed := False;
  { the uninstaller the installer left behind also clears the Installed apps entry }
  Unins := ExpandConstant('{commonappdata}\dmajorsound\Alfred\unins000.exe');
  if FileExists(Unins) then
    Exec(Unins, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Gone(ExpandConstant('{commoncf64}\VST3\Alfred.vst3'), True, Failed);
  Gone(ExpandConstant('{commoncf64}\CLAP\Alfred.clap'), False, Failed);
  RegDeleteKeyIncludingSubkeys(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{3F7C2D10-6A4B-4E8E-9C51-A1F0ED000027}_is1');
  DelTree(ExpandConstant('{commonappdata}\dmajorsound\Alfred'), True, True, True);
  RemoveDir(ExpandConstant('{commonappdata}\dmajorsound'));
  if Failed then
    MsgBox('Alfred could not be removed completely because an audio application is still running.' + #13#10 +
           'Quit it and run this uninstaller again.', mbError, MB_OK);
end;
