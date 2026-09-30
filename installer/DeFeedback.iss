; DeFeedback - Inno Setup script
; บิ้ว: iscc installer\DeFeedback.iss   (ต้องบิ้วปลั๊กอิน + standalone ให้เสร็จก่อน)
; ผลลัพธ์: installer\Output\DeFeedback-Setup-<version>.exe

#define MyAppName      "DeFeedback"
#define MyAppVersion   "0.3.0"
#define MyAppPublisher "Arty"
#define MyBundle       "DeFeedback.vst3"
#define MyExe          "DeFeedback.exe"
#define MyArtefacts    "..\build\DeFeedback_artefacts\Release"

[Setup]
AppId={{8F3C6A12-4D7B-4E59-9C21-7A5E0B6D4F83}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} installer

; {app} = โฟลเดอร์โปรแกรมเดี่ยว ส่วน VST3 ลงที่ตำแหน่งมาตรฐานเสมอ
DefaultDirName={commonpf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName} {#MyAppVersion}
UninstallDisplayIcon={app}\{#MyExe}

PrivilegesRequired=admin
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=DeFeedback-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
InfoAfterFile=info-after.txt
LicenseFile=license.txt

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "ติดตั้งทั้งหมด"
Name: "app";    Description: "เฉพาะโปรแกรมเดี่ยว (ไม่ต้องมีโปรแกรมทำเพลง)"
Name: "plugin"; Description: "เฉพาะปลั๊กอิน VST3"
Name: "custom"; Description: "เลือกเอง"; Flags: iscustom

[Components]
Name: "app";  Description: "DeFeedback - โปรแกรมเดี่ยว เปิดใช้ได้เลย"; Types: full app custom
Name: "vst3"; Description: "ปลั๊กอิน VST3 (Cubase, Reaper, SuperRack ฯลฯ)"; Types: full plugin custom

[Tasks]
Name: "desktopicon"; Description: "สร้างไอคอนบนหน้าจอ"; Components: app; Flags: unchecked

[Files]
; โปรแกรมเดี่ยว
Source: "{#MyArtefacts}\Standalone\{#MyExe}"; DestDir: "{app}"; \
    Components: app; Flags: ignoreversion
; VST3 บน Windows เป็นโฟลเดอร์ bundle ต้องก๊อปทั้งโครงสร้าง
Source: "{#MyArtefacts}\VST3\{#MyBundle}\*"; DestDir: "{commoncf64}\VST3\{#MyBundle}"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyExe}"; Components: app
Name: "{group}\ถอนการติดตั้ง {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyExe}"; \
    Components: app; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyExe}"; Description: "เปิด {#MyAppName} เลย"; \
    Components: app; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#MyBundle}"
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsWin64 then
  begin
    MsgBox('DeFeedback ใช้ได้กับ Windows 64-bit เท่านั้น', mbCriticalError, MB_OK);
    Result := False;
  end;
end;
