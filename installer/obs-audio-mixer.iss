; Windows-Installer für den Auto Mixer (Inno Setup 6)
;
; Bauen (nach "cmake --install build_x64 --prefix release/RelWithDebInfo"):
;   ISCC.exe /DAppVersion=0.1.0 installer\obs-audio-mixer.iss
; Ergebnis: release\obs-audio-mixer-<Version>-windows-x64-setup.exe

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\release\RelWithDebInfo\obs-audio-mixer"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif

#define AppName "Auto Mixer"
#define AppUrl "https://github.com/BellocccYT/obs-audio-mixer"

[Setup]
AppId={{76AC4A38-CBB9-4D1C-A941-9D8CABFDBD15}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Belloccc
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
UninstallDisplayName={#AppName} (OBS-Plugin)
#ifdef TestDir
; Testbau: ohne Adminrechte in einen beliebigen Ordner
DefaultDirName={#TestDir}
PrivilegesRequired=lowest
#else
; OBS lädt Plugins für alle Benutzer aus diesem Ordner
DefaultDirName={commonappdata}\obs-studio\plugins\obs-audio-mixer
PrivilegesRequired=admin
#endif
DisableDirPage=yes
DisableProgramGroupPage=yes
; Die Plugin-Einstellungen gehören dem angemeldeten Benutzer und bleiben beim Deinstallieren erhalten
UsedUserAreasWarning=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=obs-audio-mixer-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Messages]
FinishedHeadingLabel=Fertig!
FinishedLabelNoIcons=Der Auto Mixer ist installiert.%n%nStarte OBS: Das Dock „Auto Mixer“ erscheint automatisch. Zum Ausprobieren läuft er erst im Modus „Nur beobachten“: Er misst und warnt, ändert aber nichts.
FinishedLabel=Der Auto Mixer ist installiert.%n%nStarte OBS: Das Dock „Auto Mixer“ erscheint automatisch. Zum Ausprobieren läuft er erst im Modus „Nur beobachten“: Er misst und warnt, ändert aber nichts.

[InstallDelete]
; Reste einer älteren Version (z. B. aus der ZIP-Datei) entfernen
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Run]
Filename: "{code:ObsExe}"; WorkingDir: "{code:ObsBinDir}"; Description: "OBS Studio jetzt starten"; Flags: postinstall nowait skipifsilent runasoriginaluser; Check: ObsFound

[Code]
function ObsInstallDir: String;
begin
  if not RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Result) then
    Result := '';
end;

function ObsBinDir(Param: String): String;
begin
  Result := AddBackslash(ObsInstallDir) + 'bin\64bit';
end;

function ObsExe(Param: String): String;
begin
  Result := ObsBinDir('') + '\obs64.exe';
end;

function ObsFound: Boolean;
begin
  Result := (ObsInstallDir <> '') and FileExists(ObsExe(''));
end;

function IsProcessRunning(const FileName: String): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery(Format('SELECT Name FROM Win32_Process WHERE Name = "%s"', [FileName]));
    Result := Processes.Count > 0;
  except
  end;
end;

function WaitForObsClosed: Boolean;
begin
  Result := True;
  while IsProcessRunning('obs64.exe') do begin
    if SuppressibleMsgBox('OBS Studio läuft gerade. Bitte schließe OBS und klicke dann auf „Wiederholen“.',
      mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then begin
      Result := False;
      Exit;
    end;
  end;
end;

function InitializeSetup: Boolean;
begin
  Result := WaitForObsClosed;
  if Result and not ObsFound then
    Result := SuppressibleMsgBox('OBS Studio wurde auf diesem PC nicht gefunden. Das Plugin braucht OBS Studio 31 oder neuer.' + #13#10#13#10 +
      'Trotzdem installieren?', mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;

function InitializeUninstall: Boolean;
begin
  // Ohne das Plugin kennt OBS den Filter „Auto Mixer“ nicht mehr; er tut dann nichts, steht aber noch da
  Result := SuppressibleMsgBox('Tipp: Klicke vor dem Deinstallieren in OBS unter „Auto Mixer → Einstellungen → Allgemein“ ' +
    'auf „Filter von allen Quellen entfernen“. Sonst bleiben unbekannte Filter in deinen Quellen stehen.' + #13#10#13#10 +
    'Jetzt deinstallieren?', mbConfirmation, MB_YESNO, IDYES) = IDYES;
  if Result then
    Result := WaitForObsClosed;
end;
