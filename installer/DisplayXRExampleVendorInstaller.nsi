; DisplayXR Example Vendor Plug-in Installer (TEMPLATE / STUB)
; Copyright 2026, The DisplayXR Project
; SPDX-License-Identifier: Apache-2.0
;
; Registers the plug-in at HKLM\Software\DisplayXR\DisplayProcessors\example-vendor
; so the runtime's registry-driven discovery (target_plugin_loader.c) picks it up
; at xrCreateInstance time. Mirrors the discovery contract in
; docs/specs/runtime/plugin-discovery.md (runtime repo).
;
; VENDOR TODO when you fork:
;   - change every "example-vendor" / "ExampleVendor" to your own id / name
;   - set ProbeOrder into 1-99 once probe() detects real hardware (see below)
;   - bundle any redistributable vendor SDK runtime DLLs alongside the plug-in
;   - point the license page + icon at your own assets

;--------------------------------
; Build-time definitions (passed from CMake):
;   VERSION, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, BUILD_NUM
;   BIN_DIR    — <prefix>/bin (the DLL lives in BIN_DIR\plugins\)
;   SOURCE_DIR — repo root (for the license file)
;   OUTPUT_DIR — where the Setup .exe is written

!ifndef VERSION
	!define VERSION "0.1.0"
!endif
!ifndef VERSION_MAJOR
	!define VERSION_MAJOR "0"
!endif
!ifndef VERSION_MINOR
	!define VERSION_MINOR "1"
!endif
!ifndef VERSION_PATCH
	!define VERSION_PATCH "0"
!endif
!ifndef BUILD_NUM
	!define BUILD_NUM "0"
!endif

;--------------------------------
; Code signing (SIGN_CMD passed from CMake; empty = unsigned build).
!ifdef SIGN_CMD
	!if "${SIGN_CMD}" != ""
		!finalize '${SIGN_CMD} "%1"'
		!uninstfinalize '${SIGN_CMD} "%1"'
	!endif
!endif

;--------------------------------
; General attributes

Name "DisplayXR Example Vendor Plug-in ${VERSION}"
OutFile "${OUTPUT_DIR}\DisplayXRExampleVendorSetup-${VERSION}.${BUILD_NUM}.exe"
InstallDir "$PROGRAMFILES64\DisplayXR\Plugins\ExampleVendor"
InstallDirRegKey HKLM "Software\DisplayXR\Plugins\ExampleVendor" "InstallPath"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show
; A silent install must never skip a locked file and exit 0.
AllowSkipFiles off

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING

;--------------------------------
; Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Installer section

Section "Example Vendor Plug-in" SecPlugin
	SectionIn RO

	; NSIS is 32-bit; force the 64-bit registry view to match the runtime.
	SetRegView 64

	; ---------------------------------------------------------------
	; Hard prereq: the DisplayXR runtime must already be installed.
	; Without it, DisplayXR-ExampleVendor.dll's DisplayXRClient.dll
	; import can't resolve and LoadLibraryEx fails.
	; ---------------------------------------------------------------
	ReadRegStr $0 HKLM "Software\DisplayXR\Runtime" "InstallPath"
	${If} $0 == ""
		MessageBox MB_OK|MB_ICONSTOP \
			"DisplayXR Runtime is required and was not found.$\r$\n$\r$\nInstall it first from https://github.com/DisplayXR/displayxr-runtime/releases then retry."
		Abort
	${EndIf}
	${IfNot} ${FileExists} "$0\DisplayXRClient.dll"
		MessageBox MB_OK|MB_ICONSTOP \
			"DisplayXR Runtime path registered ($0) but DisplayXRClient.dll is missing. Reinstall the runtime then retry."
		Abort
	${EndIf}
	DetailPrint "Verified DisplayXR Runtime at $0"

	SetOutPath "$INSTDIR"

	; Stop the service before overwriting the DLL (it maps plug-in DLLs once a
	; client connects). Restarted at the end of the section.
	nsExec::ExecToLog 'taskkill /f /im displayxr-service.exe'
	Pop $0
	Sleep 1500

	; Install the plug-in DLL.
	File "${BIN_DIR}\plugins\DisplayXR-ExampleVendor.dll"

	; ---------------------------------------------------------------
	; Register at HKLM\Software\DisplayXR\DisplayProcessors\example-vendor.
	;
	; ProbeOrder=200 — the vendor-neutral FALLBACK band, because this
	; template's probe() always claims the system. A real vendor whose
	; probe() detects hardware (and declines cleanly when absent) uses a
	; value in 1-99 so it wins over sim-display on machines with its panel.
	; ---------------------------------------------------------------
	WriteRegStr   HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"Binary"          "$INSTDIR\DisplayXR-ExampleVendor.dll"
	WriteRegStr   HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"DisplayName"     "DisplayXR Example Vendor"
	WriteRegStr   HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"Vendor"          "The DisplayXR Project"
	WriteRegStr   HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"Version"         "${VERSION}"
	WriteRegStr   HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
	WriteRegDWORD HKLM "Software\DisplayXR\DisplayProcessors\example-vendor" \
		"ProbeOrder"      200

	; Track our own install location.
	WriteRegStr HKLM "Software\DisplayXR\Plugins\ExampleVendor" "InstallPath" "$INSTDIR"
	WriteRegStr HKLM "Software\DisplayXR\Plugins\ExampleVendor" "Version"     "${VERSION}"

	WriteUninstaller "$INSTDIR\Uninstall.exe"

	; Add/Remove Programs entry.
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"DisplayName" "DisplayXR Example Vendor Plug-in"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"Publisher" "The DisplayXR Project"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"DisplayVersion" "${VERSION}"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor" \
		"NoRepair" 1

	; Restart the service unless the bundle passed /NOSTART.
	${GetParameters} $R0
	ClearErrors
	${GetOptions} $R0 "/NOSTART" $R1
	${IfNot} ${Errors}
		DetailPrint "Skipping service restart (/NOSTART)."
	${Else}
		ReadRegStr $0 HKLM "Software\DisplayXR\Runtime" "InstallPath"
		${If} $0 != ""
		${AndIf} ${FileExists} "$0\displayxr-service.exe"
			DetailPrint "Restarting DisplayXR Service..."
			Exec '"$0\displayxr-service.exe"'
		${EndIf}
	${EndIf}
SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"
	SetRegView 64

	; Drop the discovery key first so the runtime doesn't pick us up after the
	; DLL is gone.
	DeleteRegKey HKLM "Software\DisplayXR\DisplayProcessors\example-vendor"

	Delete "$INSTDIR\DisplayXR-ExampleVendor.dll"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	RMDir "$PROGRAMFILES64\DisplayXR\Plugins"

	DeleteRegKey HKLM "Software\DisplayXR\Plugins\ExampleVendor"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DisplayXRExampleVendor"
	DeleteRegKey /ifempty HKLM "Software\DisplayXR\Plugins"
SectionEnd

;--------------------------------
; Section descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecPlugin} "DisplayXR Example Vendor display-processor plug-in (required)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Installer functions

Function .onInit
	${IfNot} ${RunningX64}
		MessageBox MB_ICONSTOP "This plug-in requires 64-bit Windows."
		Abort
	${EndIf}
FunctionEnd

;--------------------------------
; Version information

VIProductVersion "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.0"
VIAddVersionKey "ProductName" "DisplayXR Example Vendor Plug-in"
VIAddVersionKey "CompanyName" "The DisplayXR Project"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 The DisplayXR Project"
VIAddVersionKey "FileDescription" "DisplayXR Example Vendor Display Processor Plug-in Installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
