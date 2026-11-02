[Setup]
AppId=TaskbarLyrics.Native.Prototype
AppName=Taskbar Lyrics Native Prototype
AppVersion=0.1.1
DefaultDirName={localappdata}\Programs\TaskbarLyricsNativePrototype
DefaultGroupName=Taskbar Lyrics Native Prototype
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\artifacts
OutputBaseFilename=TaskbarLyrics-Native-0.1.1-prototype-setup-x64
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\TaskbarLyrics.Native.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
CloseApplications=yes

[Files]
Source: "..\artifacts\native-win-x64\TaskbarLyrics.Native.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Taskbar Lyrics Native Prototype"; Filename: "{app}\TaskbarLyrics.Native.exe"
Name: "{group}\Demo (simulated data)"; Filename: "{app}\TaskbarLyrics.Native.exe"; Parameters: "--demo --offline"

[Run]
Filename: "{app}\TaskbarLyrics.Native.exe"; Description: "Launch Taskbar Lyrics Native Prototype"; Flags: nowait postinstall skipifsilent
