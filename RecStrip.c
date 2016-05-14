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
//#include "../../../../../Topfield/FireBirdLib/time/AddTime.c"
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

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker         *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NewStartTimeOffset = 0, CutTimeOffset = 0;

FILE                   *fIn = NULL;  // dirty Hack: erreichbar machen für NALUDump
static FILE            *fOut = NULL;

static unsigned long long  RecFileSize = 0;
static unsigned long long  CurrentPosition = 0, PositionOffset = 0, NrPackets;
static dword               CurPosBlocks = 0, CurBlockBytes = 0, BlocksOneSecond = 250;
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
// *****  Hilfsfunktionen  *****
// ----------------------------------------------

static void DeleteSegmentMarker(int MarkerIndex, bool FreeCaption)
{
  int i;
  TRACEENTER;

  if((MarkerIndex >= 0) && (MarkerIndex < NrSegmentMarker - 1))
  {
    if (FreeCaption && SegmentMarker[MarkerIndex].pCaption)
      free(SegmentMarker[MarkerIndex].pCaption);

    for(i = MarkerIndex; i < NrSegmentMarker - 1; i++)
      memcpy(&SegmentMarker[i], &SegmentMarker[i + 1], sizeof(tSegmentMarker));

    memset(&SegmentMarker[NrSegmentMarker - 1], 0, sizeof(tSegmentMarker));
    NrSegmentMarker--;

    if(ActiveSegment >= MarkerIndex && ActiveSegment > 0) ActiveSegment--;
    if(ActiveSegment >= NrSegmentMarker - 1) ActiveSegment = NrSegmentMarker - 2;
  }
  TRACEEXIT;
}

static void DeleteBookmark(int BookmarkIndex)
{
  int i;
  TRACEENTER;

  if (BookmarkInfo && (BookmarkIndex >= 0) && (BookmarkIndex < BookmarkInfo->NrBookmarks))
  {
    for(i = BookmarkIndex; i < BookmarkInfo->NrBookmarks - 1; i++)
      BookmarkInfo->Bookmarks[i] = BookmarkInfo->Bookmarks[i + 1];
    BookmarkInfo->Bookmarks[BookmarkInfo->NrBookmarks - 1] = 0;
    BookmarkInfo->NrBookmarks--;
  }
  TRACEEXIT;
}

static void AddBookmark(int BookmarkIndex, dword BlockNr)
{
  int i;
  TRACEENTER;

  if (BookmarkInfo && (BookmarkIndex <= BookmarkInfo->NrBookmarks) && (BookmarkInfo->NrBookmarks < 48))
  {
    for(i = BookmarkInfo->NrBookmarks; i > BookmarkIndex; i--)
      BookmarkInfo->Bookmarks[i] = BookmarkInfo->Bookmarks[i - 1];
    BookmarkInfo->Bookmarks[BookmarkIndex] = BlockNr;
    BookmarkInfo->NrBookmarks++;
  }
  TRACEEXIT;
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
  static bool           ResumeSet = FALSE;
  static int            CurSeg = 0, i = 0, j = 0;
  
  TRACEENTER;
  #ifndef _WIN32
    setvbuf(stdout, NULL, _IOLBF, 4096);  // zeilenweises Buffering, auch bei Ausgabe in Datei
  #endif
  printf("\nRecStrip for Topfield PVR " VERSION "\n");
  printf("(C) 2016 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- based on MovieCutter 3.5 -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)\n");

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
    printf("\nUsage: %s [-s] [-c] [-e/n/i] <source-rec> <dest-rec>\n", argv[0]);
    printf(" -s: Strip recording.\n");
    printf(" -c: Cut movie (copy only selected parts from cutfile). May be combined with -s.\n");
    printf(" -e: Remove the EPG stream (only if -s is provided).\n");
    printf(" -n: Generate nav file for output, if not present. (Default)\n");
    printf(" -i: Generate inf file for output, if not present. (not available)\n");
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
    BlocksOneSecond = CalcBlockSize(RecFileSize) / InfDuration;
  }
  else
  {
    InfFileIn[0] = '\0';
    if (SystemType != ST_UNKNOWN)
      printf("WARNING: Cannot open inf file %s.\n", InfFileIn);
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

  // -----------------------------------------------
  // Datei paketweise einlesen und verarbeiten
  // -----------------------------------------------
  time(&startTime);
  while (fIn)
  {
    // SCHNEIDEN
    if (DoCut && (NrSegmentMarker > 2) && (CurSeg < NrSegmentMarker-1) && (CurPosBlocks >= SegmentMarker[CurSeg].Block))
    {
      while ((CurSeg < NrSegmentMarker-1) && (CurPosBlocks >= SegmentMarker[CurSeg].Block) && !SegmentMarker[CurSeg].Selected)
      {
        CutTimeOffset += SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems;
        DeleteSegmentMarker(CurSeg, TRUE);
    
        if (CurSeg < NrSegmentMarker)
        {
          unsigned long long SkippedBytes = (((unsigned long long)SegmentMarker[CurSeg].Block) * 9024) - CurrentPosition;
          fseeko64(fIn, ((unsigned long long)SegmentMarker[CurSeg].Block) * 9024, SEEK_SET);
          SetFirstPacketAfterBreak();

          // Position neu berechnen
          PositionOffset += SkippedBytes;
          CurrentPosition += SkippedBytes;
          CurPosBlocks = CalcBlockSize(CurrentPosition);
          CurBlockBytes = 0;

          if (BookmarkInfo)
          {
            // Bookmarks kurz vor der Schnittstelle löschen
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))
              j--;

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) löschen
            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CurPosBlocks + 3*BlocksOneSecond))
              DeleteBookmark(j);

            // neues Bookmark an Schnittstelle setzen
            if (CurrentPosition-PositionOffset > 0)
              AddBookmark(j++, CalcBlockSize(CurrentPosition-PositionOffset));
          }
        }
        else
        {
          // Bookmarks im verworfenen Nachlauf verwerfen
          while (BookmarkInfo && (j < BookmarkInfo->NrBookmarks))
            DeleteBookmark(j);
          break;
        }
      }

      // nächsten (zu erhaltenen) SegmentMarker anpassen
      if (CurSeg < NrSegmentMarker-1)
      {
        SegmentMarker[CurSeg].Selected = FALSE;
        SegmentMarker[CurSeg].Percent = 0;

        // Zeit neu berechnen
        if (NewStartTimeOffset == 0)
          NewStartTimeOffset = max(SegmentMarker[CurSeg].Timems, 1);
        NewDurationMS += (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);
      }
      CurSeg++;
      if (CurSeg >= NrSegmentMarker)
        break;
    }

    // PACKET EINLESEN
    ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);
    if (ReadBytes > 0)
    {
      if (Buffer[PACKETOFFSET] == 'G' || ((tTSPacket*) &Buffer[PACKETOFFSET])->Scrambling_Ctrl > 0x01)
      {
        int CurPID = TsGetPID((tTSPacket*) &Buffer[PACKETOFFSET]);
        DropCurPacket = FALSE;

        // STRIPPEN
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
            switch (ProcessTSPacket(&Buffer[PACKETOFFSET], CurrentPosition + PACKETOFFSET))
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

        // SEGMENTMARKER ANPASSEN
//        ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
        while ((i < NrSegmentMarker-1) && (CurPosBlocks >= SegmentMarker[i].Block))
        {
          SegmentMarker[i].Block -= CalcBlockSize(PositionOffset);
          SegmentMarker[i].Timems -= CutTimeOffset;
          if (!SegmentMarker[i].Timems)
            pOutNextTimeStamp = &SegmentMarker[i].Timems;
          i++;
        }

        // BOOKMARKS ANPASSEN
//        ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
        if (BookmarkInfo)
        {
          while ((j < min(BookmarkInfo->NrBookmarks, 48)) && (CurPosBlocks >= BookmarkInfo->Bookmarks[j]))
          {
            BookmarkInfo->Bookmarks[j] -= CalcBlockSize(PositionOffset);
            j++;
          }

          if (!ResumeSet && CurPosBlocks >= BookmarkInfo->Resume)
          {
            BookmarkInfo->Resume -= CalcBlockSize(PositionOffset);
            ResumeSet = TRUE;
          }
        }

        if (!DropCurPacket)
        {
          // NAV NEU BERECHNEN
//          PositionOffset += PACKETOFFSET;  // Reduktion auf 188 Byte Packets
          // nav-Eintrag korrigieren und ausgeben, wenn Position < CurrentPosition ist (um PositionOffset reduzieren)
          if (DoCut && !DoStrip && fNavIn)
            QuickNavProcess(CurrentPosition, PositionOffset);
          else
            if (CurPID == VideoPID)
              ProcessNavFile(CurrentPosition + PACKETOFFSET, PositionOffset, (tTSPacket*) &Buffer[PACKETOFFSET]);

          // PACKET AUSGEBEN
//          if (fOut && !fwrite(&Buffer[PACKETOFFSET], ReadBytes-PACKETOFFSET, 1, fOut))  // Reduktion auf 188 Byte Packets
          if (fOut && !fwrite(Buffer, ReadBytes, 1, fOut))
          {
            printf("ERROR: Failed writing to output file.\n");
            fclose(fIn); fIn = NULL;
            fclose(fOut); fOut = NULL;
            CloseNavFiles();
            CutFileClose(NULL, FALSE);
            CloseInfFile(NULL, NULL, FALSE);
            TRACEEXIT;
            exit(5);
          }
        }
        else
          // Paket wird entfernt
          PositionOffset += ReadBytes;
      
        CurrentPosition += ReadBytes;
        CurBlockBytes += ReadBytes;
        if (CurBlockBytes >= 9024)
        {
          CurPosBlocks++;
          CurBlockBytes = 0;
        }
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
          CutFileClose(NULL, FALSE);
          CloseInfFile(NULL, NULL, FALSE);
          TRACEEXIT;
          exit(6);
        }
      }
    }
    else
      break; 
  }

  if (!CloseNavFiles())
    printf("WARNING: Failed closing the nav file.\n");

  if (LastTimems)
    NewDurationMS = LastTimems;
  if (DoCut && NrSegmentMarker >= 2)
  {
    SegmentMarker[NrSegmentMarker-1].Block = CalcBlockSize(CurrentPosition - PositionOffset);
    SegmentMarker[NrSegmentMarker-1].Timems = NewDurationMS;
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
      CutFileClose(NULL, FALSE);
      CloseInfFile(NULL, NULL, FALSE);
      TRACEEXIT;
      exit(7);
    }
    fOut = NULL;
  }

  if (*CutFileIn && (argc > 2) && !CutFileClose(CutFileOut, TRUE))
    printf("WARNING: Cannot create cut %s.\n", CutFileOut);

  if (*InfFileIn && (argc > 2) && !CloseInfFile(InfFileOut, InfFileIn, TRUE))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  NrPackets = ((CurrentPosition + PACKETSIZE-1) / PACKETSIZE);
  if (NrPackets > 0)
    printf("\nPackets: %llu, FillerNALUs: %llu (%llu%%), ZeroByteStuffing: %llu (%llu%%), NullPackets: %llu (%llu%%), EPG: %llu (%llu%%), Dropped (all): %lli (%llu%%)\n", NrPackets, NrDroppedFillerNALU, NrDroppedFillerNALU*100/NrPackets, NrDroppedZeroStuffing, NrDroppedZeroStuffing*100/NrPackets, NrDroppedNullPid, NrDroppedNullPid*100/NrPackets, NrDroppedEPGPid, NrDroppedEPGPid*100/NrPackets, NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid, (NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid)*100/NrPackets);
  else
    printf("\n0 Packets!\n");

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  #ifdef _WIN32
//    getchar();
  #endif
  TRACEEXIT;
  exit(0);
}
