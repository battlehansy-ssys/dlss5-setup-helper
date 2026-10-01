@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_en.ps1"
if errorlevel 1 goto fail
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
rc /nologo app.rc
if errorlevel 1 goto fail
cl /nologo /utf-8 /O2 /MT /EHsc /DUNICODE /D_UNICODE /Fo:main_en.obj /Fe:DLSS5_Setup_EN.exe main_en.cpp app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib urlmon.lib advapi32.lib shlwapi.lib comdlg32.lib version.lib
:fail
echo EXITCODE=%ERRORLEVEL%
