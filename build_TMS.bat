@echo off
cd /d %~dp0
if "%TFROOT%"=="" set TFROOT=C:\sw\prgm\Topfield
set PATH=%TFROOT%\gccForTMS\crosstool\bin;%TFROOT%\Cygwin_mini\bin;C:\sw\OS\cygwin\bin;%PATH%
rem del /Q bin obj 2> nul
rem bash -i -c make

if "%1"=="/debug" (
  set MakeParam=--makefile=Makedebug
) else (
  set MakeParam= 
)
make %MakeParam%
set BuildState=%errorlevel%

if not "%1"=="/quiet" (
  if not "%2"=="/quiet" (
    pause
  )
)
if "%BuildState%"=="0" (
  move /y MovieCutter.tap ..\MovieCutter.tap
  copy /y ..\MovieCutter.tap ..\MovieCutter_CW.tap > nul
)
exit %BuildState%
