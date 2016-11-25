; This script generates an installer for a Winamp 2.x / 5.x plug-in.
;
; The installer will automatically close Winamp if it's running and then if
; successful, ask the user whether or not they would like to run Winamp with
; the newly installed plug-in.
;
; This is a single section installer but is easily altered for multiple
; sections and is based of the original Winamp installer script but tweaked
; to be easier to use i think :o)
 
;--------------------------------

!define MINIMAL_VERSION "5.6.6.3507"

; common defines for a generic DrO installer :o)
!define VERSION "2.3.1"
!define ALT_VER "2_3_1"
!define PLUG "Waveform Seeker"
!define PLUG_ALT "Waveform_Seeker"
!define PLUG_FILE "gen_waveseek"
 
; use lzma compression
SetCompressor /SOLID lzma
 
; adds xp style support
XPStyle on

; The name of the installer based on the filename and version
Name "${PLUG} v${VERSION}"
 
; The file to write
OutFile "${PLUG_ALT}_v${ALT_VER}.exe"
 
InstType "Plugin only"
InstType "Plugin + Example language file"
InstType /NOCUSTOM
InstType /COMPONENTSONLYONCUSTOM

Icon "..\installer.ico"
UninstallIcon "..\installer.ico"

;Header Files

!include "WordFunc.nsh"
!include "Sections.nsh"
!include "WinMessages.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!insertmacro GetSize

; The default installation directory
InstallDir "$PROGRAMFILES\Winamp\"
InstProgressFlags smooth
 
; detect Winamp path from uninstall string if available
InstallDirRegKey HKLM \
          "Software\Microsoft\Windows\CurrentVersion\Uninstall\Winamp" \
          "UninstallString"
 
; The text to prompt the user to enter a directory
DirText "Please select your Winamp path below (you will be able to proceed when Winamp is detected):"
# currently doesn't work - DirShow hide
 
; automatically close the installer when done.
AutoCloseWindow true
 
; hide the "show details" box
ShowInstDetails show
 
;--------------------------------
 
;Pages
 
PageEx directory
PageCallbacks "" "" directoryLeave
Caption " "
PageExEnd
Page components
Page instfiles
 
;--------------------------------
 
; The stuff to install
Section ""
  SetOverwrite on
  SetOutPath "$INSTDIR\Plugins"
  ; File to extract
  File "Release\${PLUG_FILE}.dll"
  SetOverwrite off
SectionEnd
 
Section "Example language file"
;  SectionSetFlags 0  SF_BOLD
  SectionIn 2

  SetOverwrite on
  SetOutPath "$INSTDIR\Plugins\${PLUG_FILE}"
  ; File to extract
  File "Release\LangFiles\${PLUG_FILE}.lng"
  SetOverwrite off
SectionEnd

Function FindAndCloseWinamp
  ;Detect running Winamp instances and close them
  !define WINAMP_FILE_EXIT 40001

  GetTempFileName $R3

checkagain:
  FindWinamp::find $R3 $INSTDIR

  ReadINIStr $R0 $R3 "found" "num"
  IntCmp $R0 0 ok

  MessageBox MB_YESNO|MB_ICONEXCLAMATION "Please close all instances of Winamp before installing$\n\
             ${PLUG} v${VERSION}.$\n$\nAttempt to close all Winamp instances now?" IDYES process IDNO 0
  Delete $R3
  Abort

process:
  ; adjust the size down by one otherwise
  ; the loop will do one unwanted iteration
  IntOp $R0 $R0 - 1

  ${ForEach} $R2 0 $R0 + 1
    ReadINIStr $R1 $R3 $R2 "hwnd"
    SendMessage $R1 ${WM_COMMAND} ${WINAMP_FILE_EXIT} 0
  ${Next}
      Goto checkagain
  ok:
  Delete $R3
FunctionEnd

;--------------------------------
 
Function directoryLeave
  ; check that it's a supported Winamp version
  Call CheckWinampVersion
  ; and if it is then ensure it's not running
  Call FindAndCloseWinamp
FunctionEnd

Function .onInstSuccess
  MessageBox MB_YESNO '${PLUG} was installed.$\nDo you want to run Winamp now?' /SD IDYES IDNO end
    ExecShell open "$INSTDIR\Winamp.exe"
  end:
FunctionEnd
 
; here we check to see if this a valid location ie is there a Winamp.exe
; in the directory?
Function .onVerifyInstDir
  ;Check for Winamp installation
  IfFileExists $INSTDIR\Winamp.exe Good
    Abort
  Good:
FunctionEnd

Function CheckWinampVersion
  ${GetFileVersion} "$INSTDIR\winamp.exe" $R0 ; Get Winamp.exe version information, $R0 = Actual Version
  ${if} $R0 != "" ; check if Version info is not empty
    ${VersionCompare} $R0 ${MINIMAL_VERSION} $R1 ; $R1 = Result $R1=0  Versions are equal, $R1=1  Version1 is newer, $R1=2  Version2 is newer
    ${if} $R1 == "2"
      MessageBox MB_OK "Warning: This requires at least Winamp v${MINIMAL_VERSION} or higher to continue.$\n$\nPlease update your Winamp install if you are using an older version or check$\nto see if there is an updated version of ${PLUG} for your install.$\n$\nThe detected version of your Winamp install is: $R0"
      Abort
    ${EndIf}
  ${Else}
    MessageBox MB_OK "Warning: A valid Winamp install was not detected in the specified path.$\n$\nPlease check the Winamp directory and either install the latest version$\nfrom Winamp.com or choose another directory with a valid Winamp install$\nbefore you can install the ${PLUG} on your machine."
    Abort
  ${EndIf}
FunctionEnd