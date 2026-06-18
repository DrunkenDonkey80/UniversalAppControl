@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
echo VCToolsInstallDir=%VCToolsInstallDir%
echo VCToolsVersion=%VCToolsVersion%
echo VCTargetsPath=%VCTargetsPath%
echo Platform=%VSCMD_ARG_TGT_ARCH%
