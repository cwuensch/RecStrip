/*
  RecStrip for Topfield PVR
  (C) 2015 Christian Wünsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan Pöschel)
  Contains portions of VDR (Klaus Schmidinger)
  Contains portions of MovieCutter (Christian Wünsch)
  Contains portions of FireBirdLib

  This program is free software;  you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY;  without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
  the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program;  if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#define _LARGEFILE64_SOURCE
#define __USE_LARGEFILE64  1
#define _FILE_OFFSET_BITS  64
#ifdef _MSC_VER
  #define __const const
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "remux.h"
#include "tools.h"
#include "../../../../API/TMS/include/type.h"

#define PACKETSIZE       192
#define PACKETOFFSET       4
#define BUFSIZE        65536
#define FBLIB_DIR_SIZE   512

#ifdef _WIN32
  #define inline
  #define __attribute__(a)
#endif

typedef struct
{
  char                  HeaderMagic[4];
  word                  HeaderVersion;
  byte                  HeaderUnknown2[2];
  dword                 HeaderStartTime;
  word                  HeaderDuration;
  word                  HeaderDurationSec;

  word                  CryptFlag:2;  // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  Flags:6;
  word                  Flags2:6;
  word                  TSFlag:1;
  word                  CopyFlag:1;

  byte                  HeaderUnknown4[10];
} TYPE_RecHeader_Info;

typedef struct
{
  byte                  SatIdx;
  byte                  ServiceType;

  word                  TPIdx:10;   // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  TunerNum:2;
  word                  DelFlag:1;
  word                  CASFlag:1;
  word                  LockFlag:1;
  word                  SkipFlag:1;

  word                  SVCID;
  word                  PMTPID;
  word                  PCRPID;
  word                  VideoPID;
  word                  AudioPID;

  char                  ServiceName[24];

  byte                  VideoStreamType;
  byte                  AudioStreamType;
} TYPE_Service_Info;

typedef struct
{
  byte                  EventUnknown1[2];
  byte                  EventDurationMin;
  byte                  EventDurationHr;
  dword                 EventID;
  dword                 EventStartTime;
  dword                 EventEndTime;
  byte                  EventRunningStatus;
  byte                  EventTextLength;
  byte                  EventParentalRate;
  char                  EventNameDescription[273];
} TYPE_Event_Info;

typedef struct
{
  word                  ExtEventServiceID;
  word                  ExtEventTextLength;
  dword                 ExtEventEventID;
  char                  ExtEventText[1024];
} TYPE_ExtEvent_Info;

typedef struct
{
  dword                 NrBookmarks;
  dword                 Bookmarks[177];
  dword                 Resume;
} TYPE_Bookmark_Info;

typedef struct
{
  byte                  SatIdx;
  word                  Polar:1;              // 0=V, 1=H
  word                  unused1:3;
  word                  ModulationSystem:1;   // 0=DVBS, 1=DVBS2
  word                  ModulationType:2;     // 0=Auto, 1=QPSK, 2=8PSK, 3=16QAM
  word                  FECMode:4;            // 0x0 = AUTO, 0x1 = 1_2, 0x2 = 2_3, 0x3 = 3_4,
                                            // 0x4 = 5_6 , 0x5 = 7_8, 0x6 = 8_9, 0x7 = 3_5,
                                            // 0x8 = 4_5, 0x9 = 9_10, 0xa = reserved, 0xf = NO_CONV
  word                  Pilot:1;
  word                  unused2:4;
  byte                  unused3;
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  AllowTimeSync:1;
  word                  unused4:15;
  word                  OriginalNetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMSS;

typedef struct
{
  byte                  SatIdx;
  byte                  ChannelNr;
  byte                  Bandwidth;
  byte                  unused1;
  dword                 Frequency;
  word                  TSID;
  byte                  LPHP;
  byte                  unused2;
  word                  OriginalNetworkID;
  word                  NetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMST;

typedef struct
{
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  OriginalNetworkID;
  byte                  ModulationType;
  byte                  unused1;
}__attribute__((packed)) TYPE_TpInfo_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSS      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSS;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSC      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMST      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMST;

typedef struct
{
  dword                 SHOffset; // = (FrameType shl 24) or SHOffset
  byte                  MPEGType;
  byte                  FrameIndex;
  byte                  Field5;
  byte                  Zero1;
  dword                 PHOffsetHigh;
  dword                 PHOffset;
  dword                 PTS2;
  dword                 NextPH;
  dword                 Timems;
  dword                 Zero5;
} tnavSD;


// Globale Variablen
FILE                   *fIn = NULL, *fOut = NULL;
FILE                   *fNavIn = NULL, *fNavOut = NULL;
unsigned long long int  CurrentPacket = 0, DroppedPackets = 0;
unsigned long long int  CurrentPosition = 0, PositionOffset = 0;


void LoadInfFile(char *RecFileName)
{
  //Calculate inf header size
  InfSize = ((GetSystemType()==ST_TMSC) ? sizeof(TYPE_RecHeader_TMSC) : sizeof(TYPE_RecHeader_TMSS));

  //Allocate and clear the buffer
  Buffer = (byte*) TAP_MemAlloc(max(InfSize, 32768));
  if(Buffer) 
    memset(Buffer, 0, InfSize);
  else
  {
    WriteLogMC("MovieCutterLib", "PatchInfFiles() E0901: source inf not patched, cut inf not created.");
    TRACEEXIT();
    return FALSE;
  }

  //Read the source .inf
  TAP_SPrint(AbsSourceInfName, sizeof(AbsSourceInfName), "%s/%s.inf", AbsDirectory, SourceFileName);
  fSourceInf = open(AbsSourceInfName, O_RDONLY);
  if(fSourceInf < 0)
  {
    WriteLogMC("MovieCutterLib", "PatchInfFiles() E0902: source inf not patched, cut inf not created.");
    TAP_MemFree(Buffer);
    TRACEEXIT();
    return FALSE;
  }

  BytesRead = read(fSourceInf, Buffer, InfSize);
  #ifdef FULLDEBUG
    struct stat statbuf;
    fstat(fSourceInf, &statbuf);
    WriteLogMCf("MovieCutterLib", "PatchInfFiles(): %d / %llu Bytes read.", BytesRead, statbuf.st_size);
  #endif
  close(fSourceInf);
}



void ProcessNavFile(const unsigned long long CurrentPosition, const unsigned long long PositionOffset)
{
  while (fNavIn && (navBuffer.Position < CurrentPosition))
    {
      navBuffer.Position -= PositionOffset;
      if (fNavOut && !fwrite(navBuffer, 32, 1, fNavOut))
      {
        printf("ERROR writing to nav file!");
        fclose(fNavOut); fNavOut = NULL;
      }
      if (!fread(navBuffer, 32, 1, fNavIn))
      {
        fclose(fNavIn); fNavIn = NULL;
      }
    }
}





int main(int argc, const char* argv[])
{
  char                  NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE];
  char                  Buffer[PACKETSIZE], navBuffer[32];
  int                   ReadBytes;
  int                   ReturnCode = 0;
  int                   i;
  time_t                startTime, endTime;

  printf("\nRecStrip for Topfield PVR v0.0\n");
  printf("(C) 2015 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- portions of VDR (K. Schmidinger), Mpeg2cleaner (S. Poeschel) and MovieCutter -\n\n");

  // Eingabe-Parameter prüfen
  if (argc < 2)
  {
    printf("Usage: %s <source-rec> <dest-rec>\n\n", argv[0]);
    exit(1);
  }

  // Input-File öffnen
  printf("Input file: %s\n", argv[1]);
  fIn = fopen(argv[1], "rb");
  if (fIn)
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);
  else
  {
    printf("ERROR: Cannot open %s.\n", argv[1]);
    exit(2);
  }

  // ggf. Output-File öffnen
  if (argc > 2)
  {
    printf("Output file: %s\n\n", argv[2]);
    fOut = fopen(argv[2], "wb");
    if (fOut)
      setvbuf(fOut, NULL, _IOFBF, BUFSIZE);
    else
    {
      fclose(fIn);
      printf("ERROR: Cannot create %s.\n", argv[2]);
      exit(3);
    }
  }

  // ggf. nav-Files öffnen
  sprintf(NavFileIn, "%s.nav", argv[1]);
  printf("Nav file: %s\n", NavFileIn);
  fNavIn = fopen(NavFileIn, "rb");
  if (fNavIn)
  {
    dword FirstDword = 0;
    fread(&FirstDword, 4, 1, fNavIn);
    if(FirstDword == 0x72767062)  // 'bpvr'
      fseek(fNavIn, 1056, SEEK_SET);
    else
      rewind(fNavIn);

    if (argc > 2)
    {
      sprintf(NavFileOut, "%s.nav", argv[2]);
      printf("Nav output: %s\n\n", NavFileOut);
      fNavOut = fopen(NavFileOut, "wb");
      if (!fNavIn)
        printf("WARNING: Cannot create nav %s.\n", NavFileOut);
    }
  }
  else
    printf("WARNING: Cannot open nav file %s.\n", NavFileIn);

  // ggf. inf-File einlesen
  sprintf(InfFileIn, "%s.inf", argv[1]);
  printf("Inf file: %s\n", NavFileIn);
  if (LoadInfFile(InfFileIn))
  {
    if (argc > 2)
    {
      sprintf(InfFileOut, "%s.inf", argv[2]);
      printf("Inf output: %s\n\n", InfFileOut);
    }
  }
  else
    printf("WARNING: Cannot open inf file %s.\n", InfFileIn);

  // ggf. cut-File einlesen
  sprintf(CutFileIn, "%s.cut", argv[1]);
  printf("Cut file: %s\n", CutFileIn);
  if (LoadCutFile(CutFileIn))
  {
    if (argc > 2)
    {
      sprintf(InfFileOut, "%s.cut", argv[2]);
      printf("Inf output: %s\n\n", CutFileOut);
    }
  }
  else
    printf("WARNING: Cannot open cut file %s.\n", CutFileIn);


  // Datei paketweise einlesen und verarbeiten
  time(&startTime);
  while (true)
  {
    ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);

    // alle nav-Einträge, deren Position < CurrentPosition sind, um PositionOffset reduzieren und ausgeben
    ProcessNavFile(CurrentPosition, PositionOffset);

    // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren


    if (ReadBytes > 0)
    {
/*      if (ProcessPacket(&Buffer[PACKETOFFSET]) == true)
      {
        // Paket wird entfernt
        DroppedPackets++;
        PositionOffset += ReadBytes;
      }
      else */
        // (ggf. verändertes) Paket wird in Ausgabe geschrieben
        if (fOut && !fwrite(Buffer, ReadBytes, 1, fOut))
        {
          printf("ERROR: Failed writing to output file.\n");
          fclose(fOut);
          fclose(fIn);
          exit(4);
        }
      CurrentPacket++;
      CurrentPosition += ReadBytes;
    }
    else
      break;
  }
  time(&endTime);

  fclose(fIn);
  if(fOut) fclose(fOut);
  if(fNavIn) fclose(fNavIn);
  if (fNavOut) fclose(fNavOut);

  if (InfFileIn && (argc > 2) && !SaveInfFile(InfFileOut))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  if (CutFileIn && (argc > 2) && !SaveInfFile(InfFileOut))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  printf("Packets: %lli Dropped: %lli (%lli%%)\n", CurrentPacket, DroppedPackets, CurrentPacket ? DroppedPackets*100/CurrentPacket : 0);

  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  #ifdef _WIN32
    getchar();
  #endif
  exit(0);
}
