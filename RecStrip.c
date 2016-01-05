/*
  RecStrip for Topfield PVR
  (C) 2015 Christian Wünsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan Pöschel)
  Contains portions of RebuildNav (Alexander Ölzant)
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
  #define __attribute__(a)
  #pragma pack(1)
  #define inline
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../../../../../Topfield/API/TMS/include/type.h"
#include "RecStrip.h"
#include "InfProcessor.h"
#include "NavProcessor.h"
#include "CutProcessor.h"
#include "RebuildInf.h"
#include "NALUDump.h"

#ifdef _WIN32
  #define stat64 _stat64
  #define fseeko64 _fseeki64
  #define ftello64 _ftelli64
//  #define fopen fopen_s
//  #define strncpy strncpy_s
//  #define sprintf sprintf_s
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
  int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
  {
    int count = -1;
    if (size != 0)
      count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
    if (count == -1)
      count = _vscprintf(format, ap);
    return count;
  }

  int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
  {
    int count;
    va_list ap;
    va_start(ap, format);
    count = c99_vsnprintf(outBuf, size, format, ap);
    va_end(ap);
    return count;
  }
#endif


// Globale Variablen
char                    RecFileIn[FBLIB_DIR_SIZE], RecFileOut[FBLIB_DIR_SIZE];
word                    VideoPID = 0;
bool                    isHDVideo = FALSE;
byte                    PACKETSIZE, PACKETOFFSET;

FILE                   *fIn = NULL;  // dirty Hack: erreichbar machen für NALUDump
static FILE            *fOut = NULL;

static unsigned long long int  CurrentPacket = 0, DroppedPackets = 0, DroppedNALU = 0;
static unsigned long long int  CurrentPosition = 0, outpos=0, PositionOffset = 0;


bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize)
{
  struct stat64         statbuf;
  bool                  ret = FALSE;

  if(AbsFileName)
  {
    ret = (stat64(AbsFileName, &statbuf) == 0);
    if (ret && OutFileSize)
      *OutFileSize = statbuf.st_size;
  }
  return ret;
}


// ----------------------------------------------
// *****  Analyse von REC-Files  *****
// ----------------------------------------------

static inline dword CalcBlockSize(unsigned long long Size)
{
  // Workaround für die Division durch BLOCKSIZE (9024)
  // Primfaktorenzerlegung: 9024 = 2^6 * 3 * 47
  // max. Dateigröße: 256 GB (dürfte reichen...)
  return (dword)(Size >> 6) / 141;
}

bool isPacketStart(const byte PacketArray[], int ArrayLen)
{
  int                   i;
  bool                  ret = TRUE;

  for (i = 0; i < 10; i++)
  {
    if (PACKETOFFSET + (i * PACKETSIZE) >= ArrayLen)
      break;
    if (PacketArray[PACKETOFFSET + (i * PACKETSIZE)] != 'G')
    {
      ret = FALSE;
      break;
    }
  }
  return ret;
}

int GetPacketSize(char *RecFileName)
{
  char                 *p;
  bool                  ret = FALSE;

  p = strrchr(RecFileName, '.');
  if (p && strcmp(p, ".rec") == 0)
  {
    PACKETSIZE = 192;
    PACKETOFFSET = 4;
    ret = TRUE;
  }
  else
  {
    byte               *RecStartArray = NULL;

    RecStartArray = (byte*) malloc(1733);  // 1733 = 9*192 + 5
    if (RecStartArray)
    {
      if (fread(RecStartArray, 1, 1733, fIn) == 1733)
      {
        PACKETSIZE = 188;
        PACKETOFFSET = 0;
        ret = isPacketStart(RecStartArray, 1733);

        if (!ret)
        {
          PACKETSIZE = 192;
          PACKETOFFSET = 4;
          ret = isPacketStart(RecStartArray, 1733);
        }
      }
      free(RecStartArray);
    }
  }
  return (ret ? PACKETSIZE : 0);
}


// ----------------------------------------------
// *****  MAIN FUNCTION  *****
// ----------------------------------------------

int main(int argc, const char* argv[])
{
  char                  NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE];
  byte                  Buffer[192];
  int                   ReadBytes;
  time_t                startTime, endTime;

  printf("\nRecStrip for Topfield PVR v0.2\n");
  printf("(C) 2016 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & MovieCutter -\n");

  // Eingabe-Parameter prüfen
  if (argc > 2)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    if (argc > 2)
      strncpy(RecFileOut, argv[2], sizeof(RecFileOut));
  }
  else
  {
    printf("\nUsage: %s <source-rec> <dest-rec>\n\n", argv[0]);
    exit(1);
  }

  // Input-File öffnen
  printf("\nInput file: %s\n", RecFileIn);
  fIn = fopen(RecFileIn, "rb");
  if (fIn)
  {
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);
    GetPacketSize(RecFileIn);
  }
  else
  {
    printf("ERROR: Cannot open %s.\n", RecFileIn);
    exit(2);
  }

  // ggf. Output-File öffnen
  if (argc > 2)
  {
    printf("Output file: %s\n", RecFileOut);
    fOut = fopen(RecFileOut, "wb");
    if (fOut)
    {
      setvbuf(fOut, NULL, _IOFBF, BUFSIZE);
    }
    else
    {
      fclose(fIn);
      printf("ERROR: Cannot create %s.\n", RecFileOut);
      exit(3);
    }
  }

  // ggf. inf-File einlesen
  snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
  printf("\nInf file: %s\n", InfFileIn);
  if (LoadInfFile(InfFileIn))
  {
    if (argc > 2)
    {
      snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
      printf("Inf output: %s\n", InfFileOut);
    }
  }
  else
  {
    printf("WARNING: Cannot open inf file %s.\n", InfFileIn);
    InfFileIn[0] = '\0';
  }

  // ggf. nav-Files öffnen
  snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);
  printf("\nNav file: %s\n", NavFileIn);
  if (argc > 2)
  {
    snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
    printf("Nav output: %s\n", NavFileOut);
  }
  if (!LoadNavFiles(NavFileIn, NavFileOut))
    printf("WARNING: Cannot open nav file %s.\n", NavFileIn);

  // ggf. cut-File einlesen
  GetCutNameFromRec(RecFileIn, CutFileIn);
  printf("\nCut file: %s\n", CutFileIn);

  if (CutFileLoad(CutFileIn))
  {
    if (argc > 2)
    {
      GetCutNameFromRec(RecFileOut, CutFileOut);
      printf("Cut output: %s\n", CutFileOut);
    }
  }
  else
    CutFileIn[0] = '\0';
  printf("\n");

  // Aufnahme analysieren
//  if (!VideoPID)
    GetVideoInfos(fIn);

  // Datei paketweise einlesen und verarbeiten
  time(&startTime);
  while (TRUE)
  {
    dword CurPosBlocks = CalcBlockSize(CurrentPosition);
    dword PosOffsetBlocks = CalcBlockSize(PositionOffset);

    ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);
    if (ReadBytes > 0)
    {
      int CurPID = TsGetPID((tTSPacketHeader*) &Buffer[PACKETOFFSET]);

      if ((CurPID == 0x1FFF) || (CurPID == 0x12) || ((CurPID == VideoPID) && (ProcessTSPacket(&Buffer[PACKETOFFSET], CurrentPosition) == TRUE)))
      {
        // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren
        ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
        ProcessCutFile(CurPosBlocks, PosOffsetBlocks);

        // Paket wird entfernt
        DroppedPackets++;
        if ((CurPID != 0x1FFF) && (CurPID != 0x12)) DroppedNALU++;
        PositionOffset += ReadBytes;
      }
      else
      {
        // nav-Eintrag korrigieren und ausgeben, wenn Position < CurrentPosition ist (um PositionOffset reduzieren)
        if (CurPID == VideoPID)
          ProcessNavFile(CurrentPosition, PositionOffset, (trec*) &Buffer[PACKETOFFSET]);

        // (ggf. verändertes) Paket wird in Ausgabe geschrieben
        if (fOut && !fwrite(Buffer, ReadBytes, 1, fOut))
        {
          printf("ERROR: Failed writing to output file.\n");
          fclose(fIn);
          fclose(fOut);
          CloseNavFiles();
//          free(SegmentMarker);
          exit(4);
        }
        outpos += ReadBytes;
      }
      CurrentPacket++;
      CurrentPosition += ReadBytes;
    }
    else
    {
      // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren
      ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
      ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
      break;
    }
  }
  fclose(fIn);
  if(fOut)
  {
    if (fflush(fOut) != 0 || fclose(fOut) != 0)
    {
      printf("ERROR: Failed closing the output file.\n");
      exit(5);
    }
  }
  if (!CloseNavFiles())
    printf("WARNING: Failed closing the nav file.\n");

  if (*InfFileIn && (argc > 2) && !SaveInfFile(InfFileOut, InfFileIn))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  if (*CutFileIn && (argc > 2) && !CutFileSave(CutFileOut))
    printf("WARNING: Cannot create cut %s.\n", CutFileOut);

  printf("\nPackets: %lli, Dropped (NALU): %lli (%lli%%), Dropped (all): %lli (%lli%%)\n", CurrentPacket, DroppedNALU, CurrentPacket ? DroppedNALU*100/CurrentPacket : 0, DroppedPackets, CurrentPacket ? DroppedPackets*100/CurrentPacket : 0);

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

//  free(SegmentMarker);

  #ifdef _WIN32
    getchar();
  #endif
  exit(0);
}
