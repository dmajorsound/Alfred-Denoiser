; Alfred installer for Windows (Inno Setup 6). The version comes from the build: ISCC /DAppVer=0.27 /DFileVer=0.27_beta
#ifndef AppVer
  #define AppVer "0.50 beta"
#endif
#ifndef FileVer
  #define FileVer "0.50_beta"
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
Name: "restore"; Description: "Enhance model Restore: studio voice, worked out when you render (36 MB, needs the neural files). OpenRAIL license, shown on the next page"; Types: full custom
Name: "sidon"; Description: "Enhance model Sidon: builds the voice anew, worked out when you render, slow (300 MB, needs the neural files). MIT License"; Types: full custom

[Files]
Source: "welcome-icon-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-icon-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "..\..\build\Alfred_artefacts\Release\VST3\Alfred.vst3\*"; DestDir: "{commoncf64}\VST3\Alfred.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\Alfred_artefacts\Release\CLAP\Alfred.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
; neural voice engine: ONNX Runtime and the model, shared by both plug-in formats
Source: "..\..\support\*"; DestDir: "{commonappdata}\dmajorsound\Alfred"; Components: neural; Flags: ignoreversion
Source: "..\..\enhance\restore\*"; DestDir: "{commonappdata}\dmajorsound\Alfred"; Components: restore; Flags: ignoreversion
; shown on the license page that appears when Restore is selected
Source: "..\..\enhance\restore\restore-LICENSE-OpenRAIL.txt"; DestDir: "{tmp}"; Flags: dontcopy
Source: "..\..\enhance\sidon\*"; DestDir: "{commonappdata}\dmajorsound\Alfred"; Components: sidon; Flags: ignoreversion

[Messages]
WelcomeLabel1=Alfred%nA simple Denoiser at your Service
WelcomeLabel2=This installs Alfred as VST3 and CLAP plug-in into the common plug-in folders. On the next page you can choose what to install.%n%nQuit your DAW first. Afterwards, rescan your plug-ins in your DAW.

; ---- a license page for the Restore model: shown only when that component is selected, and it must be accepted
[Code]
#include "branding.iss"

var
  RailPage: TWizardPage;
  RailMemo: TNewMemo;
  RailCheck: TNewCheckBox;

procedure InitializeWizard;
var
  Raw: AnsiString;
  Txt: String;
begin
  BrandWelcome('Alfred - A simple Denoiser at your Service',
    'This installs Alfred as VST3 and CLAP plug-in into the common plug-in folders. On the next page you can choose what to install.' + #13#10 + #13#10 +
    'Quit your DAW first. Afterwards, rescan your plug-ins in your DAW.');
  RailPage := CreateCustomPage(wpSelectComponents, 'License of the Enhance model Restore',
    'It covers the model and the audio made with it. Accept it to install Restore.');
  RailMemo := TNewMemo.Create(RailPage);
  RailMemo.Parent := RailPage.Surface;
  RailMemo.Left := 0;
  RailMemo.Top := 0;
  RailMemo.Width := RailPage.SurfaceWidth;
  RailMemo.Height := RailPage.SurfaceHeight - ScaleY(30);
  RailMemo.ScrollBars := ssVertical;
  RailMemo.ReadOnly := True;
  RailMemo.WordWrap := True;
  ExtractTemporaryFile('restore-LICENSE-OpenRAIL.txt');
  if LoadStringFromFile(ExpandConstant('{tmp}\restore-LICENSE-OpenRAIL.txt'), Raw) then
  begin
    Txt := UTF8Decode(Raw);
    StringChangeEx(Txt, #13#10, #10, True);
    StringChangeEx(Txt, #10, #13#10, True);
    RailMemo.Text := Txt;
  end;
  RailCheck := TNewCheckBox.Create(RailPage);
  RailCheck.Parent := RailPage.Surface;
  RailCheck.Left := 0;
  RailCheck.Top := RailMemo.Height + ScaleY(8);
  RailCheck.Width := RailPage.SurfaceWidth;
  RailCheck.Height := ScaleY(20);
  RailCheck.Caption := 'I accept this license and its use restrictions (Section 2)';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = RailPage.ID) and not WizardIsComponentSelected('restore');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = RailPage.ID) and not RailCheck.Checked then
  begin
    MsgBox('Please accept the license. If you do not want its terms, go back and switch off "Enhance model Restore".', mbInformation, MB_OK);
    Result := False;
  end;
end;
