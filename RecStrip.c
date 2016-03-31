/*
  RecStrip for Topfield PVR
  (C) 2016 Christian Wünsch

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
SYSTEM_TYPE             SystemType = ST_UNKNOWN;
byte                    PACKETSIZE, PACKETOFFSET;
word                    VideoPID = 0;
bool                    isHDVideo = FALSE, AlreadyStripped = FALSE;
bool                    DoStrip = FALSE, DoCut = FALSE, RemoveEPGStream = TRUE;

FILE                   *fIn = NULL;  // dirty Hack: erreichbar machen für NALUDump
static FILE            *fOut = NULL;

static unsigned long long  RecFileSize = 0;
static unsigned long long  CurrentPosition = 0, PositionOffset = 0, CurrentPacket = 0;
static unsigned long long  NrDroppedFillerNALU = 0, NrDroppedZeroStuffing = 0, NrDroppedNullPid = 0, NrDroppedEPGPid = 0;


bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize)
{
  struct stat64         statbuf;
  bool                  ret = FALSE;

  TRACEENTER;
  if(AbsFileName)
  {
    ret = (stat64(AbsFileName, &statbuf) == 0);
    if (ret && OutFileSize)
      *OutFileSize = statbuf.st_size;
  }
  TRACEEXIT;
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

  TRACEENTER;
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
  TRACEEXIT;
  return ret;
}

int GetPacketSize(char *RecFileName)
{
  char                 *p;
  bool                  ret = FALSE;

  TRACEENTER;
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
  TRACEEXIT;
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
  bool                  DropCurPacket;
  time_t                startTime, endTime;

  TRACEENTER;
  #ifndef _WIN32
    setvbuf(stdout, NULL, _IOLBF, 4096);  // zeilenweises Buffering, auch bei Ausgabe in Datei
  #endif
  printf("\nRecStrip for Topfield PVR " VERSION "\n");
  printf("(C) 2016 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & MovieCutter -\n");

  // Eingabe-Parameter prüfen
  while ((argc > 1) && (argv && argv[1] && argv[1][0] == '-'))
  {
    switch (argv[1][1])
    {
      case 'c':   DoCut = TRUE;           break;
      case 's':   DoStrip = TRUE;         break;
      case 'e':   RemoveEPGStream = TRUE; break;
      default:    printf("\nUnknown argument: -%c\n", argv[1][1]);
    }
    argv[1] = argv[0];
    argv++;
    argc--;
  }
//  if (!DoCut && !DoStrip) DoStrip = TRUE;
  if (!DoStrip) RemoveEPGStream = FALSE;

  // Eingabe-Dateinamen lesen
  if (argc > 2)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    RecFileIn[sizeof(RecFileIn)-1] = '\0';
//    if (argc > 2)
    {
      strncpy(RecFileOut, argv[2], sizeof(RecFileOut));
      RecFileOut[sizeof(RecFileOut)-1] = '\0';
    }
  }
  else
  {
    printf("\nUsage: %s [-s] [-c] [-e] <source-rec> <dest-rec>\n", argv[0]);
    printf(" -s: Strip recording (default if no option is provided.)\n");
    printf(" -c: Cut movie (copy only selected parts from cutfile). May be combined with -s\n");
    printf(" -e: Remove the EPG stream (only if -s is provided).\n");
    TRACEEXIT;
    exit(1);
  }

  // Input-File öffnen
  printf("\nInput file: %s\n", RecFileIn);
  if (HDD_GetFileSize(RecFileIn, &RecFileSize))
    fIn = fopen(RecFileIn, "rb");
  if (fIn)
  {
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);
    GetPacketSize(RecFileIn);
    printf("File size of rec: %llu, packet size: %u\n", RecFileSize, PACKETSIZE);
  }
  else
  {
    printf("ERROR: Cannot open %s.\n", RecFileIn);
    TRACEEXIT;
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
      TRACEEXIT;
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
    if (SystemType != ST_UNKNOWN)
    {
      printf("WARNING: Cannot open inf file %s.\n", InfFileIn);
      InfFileIn[0] = '\0';
    }
    else
    {
      printf("ERROR: Unknown SystemType.\n");
      fclose(fIn); fIn = NULL;
      fclose(fOut); fOut = NULL;
      CloseInfFile(NULL, NULL, FALSE);
      TRACEEXIT;
      exit(4);
    }
  }
  if (AlreadyStripped)
  {
    printf("INFO: File has already been stripped.\n");
/*    fclose(fIn); fIn = NULL;
    fclose(fOut); fOut = NULL;
    CloseInfFile(NULL, NULL, FALSE);
    TRACEEXIT;
    exit(0); */
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
  while (fIn)
  {
    ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);
    if (ReadBytes > 0)
    {
      if (Buffer[PACKETOFFSET] == 'G')
      {
        int CurPID = TsGetPID((tTSPacketHeader*) &Buffer[PACKETOFFSET]);
        DropCurPacket = FALSE;

        if (DoStrip)
        {
          if (CurPID == 0x1FFF)
          {
            NrDroppedNullPid++;
            DropCurPacket = TRUE;
          }
          else if (CurPID == 0x12 && RemoveEPGStream)
          {
            NrDroppedEPGPid++;
            DropCurPacket = TRUE;
          }
          else if (CurPID == VideoPID)
          {
            switch (ProcessTSPacket(&Buffer[PACKETOFFSET], CurrentPosition))
            {
              case 1: 
                NrDroppedFillerNALU++;
                DropCurPacket = TRUE;
                break;
              case 2:
                NrDroppedZeroStuffing++;
                DropCurPacket = TRUE;
                break;
              default:
                break;
            }
          }
        }

        if (DropCurPacket)  // nicht bei Reduktion auf 188 Byte Packets
        {
          dword CurPosBlocks = CalcBlockSize(CurrentPosition);
          dword PosOffsetBlocks = CalcBlockSize(PositionOffset);

          // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren
          ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
          ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
        }
        if (DropCurPacket)
        {
          // Paket wird entfernt
          PositionOffset += ReadBytes;
        }
        else
        {
//          PositionOffset += PACKETOFFSET;  // Reduktion auf 188 Byte Packets
          // nav-Eintrag korrigieren und ausgeben, wenn Position < CurrentPosition ist (um PositionOffset reduzieren)
          if (CurPID == VideoPID)
            ProcessNavFile(CurrentPosition, PositionOffset, (tTSPacket*) &Buffer[PACKETOFFSET]);

          // (ggf. verändertes) Paket wird in Ausgabe geschrieben
//          if (fOut && !fwrite(&Buffer[PACKETOFFSET], ReadBytes-PACKETOFFSET, 1, fOut))  // Reduktion auf 188 Byte Packets
          if (fOut && !fwrite(Buffer, ReadBytes, 1, fOut))
          {
            printf("ERROR: Failed writing to output file.\n");
            fclose(fIn); fIn = NULL;
            fclose(fOut); fOut = NULL;
            CloseNavFiles();
            CloseInfFile(NULL, NULL, FALSE);
            CutFileClose(NULL, FALSE);
            TRACEEXIT;
            exit(5);
          }
        }
        CurrentPacket++;
        CurrentPosition += ReadBytes;
      }
      else
      {
        if (CurrentPosition + 4096 >= RecFileSize)
        {
          printf("INFO: Incorrect TS - Ignoring last %llu bytes.\n", RecFileSize - CurrentPosition);
          fclose(fIn); fIn = NULL;
        }
        else
        {
          printf("ERROR: Incorrect TS - Missing sync byte at position %llu.\n", CurrentPosition);
          fclose(fIn); fIn = NULL;
          fclose(fOut); fOut = NULL;
          CloseNavFiles();
          CloseInfFile(NULL, NULL, FALSE);
          CutFileClose(NULL, FALSE);
          TRACEEXIT;
          exit(6);
        }
      }
    }
    else
    {
      fclose(fIn); fIn = NULL;
    }
  }

  {
    dword CurPosBlocks = CalcBlockSize(CurrentPosition);
    dword PosOffsetBlocks = CalcBlockSize(PositionOffset);

    // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren
    ProcessInfFile(CurPosBlocks+1, PosOffsetBlocks);
    ProcessCutFile(CurPosBlocks+1, PosOffsetBlocks);
  }

  if (fIn)
  {
    fclose(fIn); fIn = NULL;
  }
  if(fOut)
  {
    if (/*fflush(fOut) != 0 ||*/ fclose(fOut) != 0)
    {
      printf("ERROR: Failed closing the output file.\n");
      CloseNavFiles();
      CloseInfFile(NULL, NULL, FALSE);
      CutFileClose(NULL, FALSE);
      TRACEEXIT;
      exit(7);
    }
    fOut = NULL;
  }

  if (!CloseNavFiles())
    printf("WARNING: Failed closing the nav file.\n");

  if (*InfFileIn && (argc > 2) && !CloseInfFile(InfFileOut, InfFileIn, TRUE))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  if (*CutFileIn && (argc > 2) && !CutFileClose(CutFileOut, TRUE))
    printf("WARNING: Cannot create cut %s.\n", CutFileOut);


  printf("\nPackets: %llu, FillerNALUs: %llu (%llu%%), ZeroByteStuffing: %llu (%llu%%), NullPackets: %llu (%llu%%), EPG: %llu (%llu%%), Dropped (all): %lli (%llu%%)\n", CurrentPacket, NrDroppedFillerNALU, (CurrentPacket ? NrDroppedFillerNALU*100/CurrentPacket : 0), NrDroppedZeroStuffing, (CurrentPacket ? NrDroppedZeroStuffing*100/CurrentPacket : 0), NrDroppedNullPid, (CurrentPacket ? NrDroppedNullPid*100/CurrentPacket : 0), NrDroppedEPGPid, (CurrentPacket ? NrDroppedEPGPid*100/CurrentPacket : 0), NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid, (CurrentPacket ? (NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid)*100/CurrentPacket : 0));

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  #ifdef _WIN32
//    getchar();
  #endif
  TRACEEXIT;
  exit(0);
}
