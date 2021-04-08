@echo off
cd /d %~dp0
set PATH=C:\Programme\MSYS\MinGW\bin;%path%

gcc -static -O2 -MD -fstack-protector-all -W -Wall -Wstack-protector -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -o RecStrip.exe  RecStrip.c NALUDump.c NavProcessor.c InfProcessor.c CutProcessor.c RebuildInf.c
set BuildState=%errorlevel%

if not "%1"=="/quiet" (
  if not "%2"=="/quiet" (
    pause
  )
)
exit %BuildState%
