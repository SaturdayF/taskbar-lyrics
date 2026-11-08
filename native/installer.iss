[Setup]
AppId=TaskbarLyrics.Native.Prototype
AppName=Taskbar Lyrics
AppVersion=0.3.1
DefaultDirName={localappdata}\Programs\TaskbarLyricsNativePrototype
DefaultGroupName=Taskbar Lyrics
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\artifacts
OutputBaseFilename=TaskbarLyrics-Native-0.3.1-setup-x64
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\TaskbarLyrics.Native.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
CloseApplications=yes

[Files]
Source: "..\artifacts\native-win-x64\TaskbarLyrics.Native.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\artifacts\native-win-x64\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\assets\app.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}\native"; Flags: ignoreversion

[Icons]
Name: "{group}\Taskbar Lyrics"; Filename: "{app}\TaskbarLyrics.Native.exe"
Name: "{group}\Demo (simulated data)"; Filename: "{app}\TaskbarLyrics.Native.exe"; Parameters: "--demo --offline"

[Run]
Filename: "{app}\TaskbarLyrics.Native.exe"; Description: "Launch Taskbar Lyrics"; Flags: nowait postinstall skipifsilent
