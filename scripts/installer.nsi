; ══════════════════════════════════════════════════════════════════════════
;  Calculator — NSIS Installer
;  Build:  makensis -DVERSION=1.0.0 -DAPP_NAME=Calculator \
;                   -DOUTFILE=... -DSRCDIR=... installer.nsi
; ══════════════════════════════════════════════════════════════════════════

!define PRODUCT_NAME "${APP_NAME}"
!define PRODUCT_VERSION "${VERSION}"
!define PRODUCT_PUBLISHER "Calculator Project"
!define PRODUCT_WEB_SITE "https://github.com/your-username/Calculator"

; ── Compressor ──────────────────────────────────────────────────────────
SetCompressor /SOLID lzma
SetCompressorDictSize 32

; ── Output ──────────────────────────────────────────────────────────────
OutFile "${OUTFILE}"

; ── Default install path ─────────────────────────────────────────────────
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"

; Request admin privileges
RequestExecutionLevel admin

; ── Interface ────────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "WinCore.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; ── Install Section ────────────────────────────────────────────────────
Section "Install" SecMain
    SetOutPath "$INSTDIR"

    ; Main executable.
    File "${SRCDIR}\${APP_NAME}.exe"

    ; Create Start Menu shortcut.
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
                   "$INSTDIR\${APP_NAME}.exe" "" "$INSTDIR\${APP_NAME}.exe" 0
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" \
                   "$INSTDIR\${APP_NAME}.exe" "" "$INSTDIR\${APP_NAME}.exe" 0

    ; Write uninstaller.
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Registry — Add/Remove Programs.
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                     "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                     "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                     "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                     "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                     "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                       "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" \
                       "NoRepair" 1
SectionEnd

; ── Uninstall Section ──────────────────────────────────────────────────
Section "Uninstall"
    ; Remove files.
    Delete "$INSTDIR\${APP_NAME}.exe"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove shortcuts.
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

    ; Remove registry keys.
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
SectionEnd
