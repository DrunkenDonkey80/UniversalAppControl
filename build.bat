@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
set VCTargetsPath=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\
msbuild "C:\SOFT\temp\UniversalAppControl\UniversalAppControl.sln" /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal
echo BUILD_EXIT=%ERRORLEVEL%
