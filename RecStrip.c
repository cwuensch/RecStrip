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
#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
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
#include "type.h"
//#include "../../../../../Topfield/FireBirdLib/time/AddTime.c"
#include "RecStrip.h"
#include "InfProcessor.h"
#include "NavProcessor.h"
#include "CutProcessor.h"
#include "RebuildInf.h"
#include "NALUDump.h"


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
unsigned long long      RecFileSize = 0;
SYSTEM_TYPE             SystemType = ST_UNKNOWN;
byte                    PACKETSIZE, PACKETOFFSET, OutPacketSize = 0;
word                    VideoPID = 0;
bool                    isHDVideo = FALSE, AlreadyStripped = FALSE;
bool                    DoStrip = FALSE, DoCut = FALSE, RemoveEPGStream = FALSE, RebuildNav = FALSE, RebuildInf = FALSE;

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker         *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NewStartTimeOffset = 0, CutTimeOffset = 0;

FILE                   *fIn = NULL;  // dirty Hack: erreichbar machen für NALUDump
static FILE            *fOut = NULL;

static unsigned int        RecFileBlocks = 0;
static unsigned long long  CurrentPosition = 0, PositionOffset = 0, NrPackets;
static dword               CurPosBlocks = 0, CurBlockBytes = 0, BlocksOneSecond = 250;
static unsigned long long  NrDroppedFillerNALU = 0, NrDroppedZeroStuffing = 0, NrDroppedNullPid = 0, NrDroppedEPGPid = 0;
static dword               LastPCR = 0, LastTimeStamp = 0, CurTimeStep = 5000;
static unsigned long long  PosLastPCR = 0;


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
  int                   i = 0;
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

    RecStartArray = (byte*) malloc(1925);  // 1925 = 10*192 + 5
    if (RecStartArray)
    {
      if (fread(RecStartArray, 1, 1925, fIn) == 1925)
      {
        while (!ret && (i < 192))
        {
          PACKETSIZE = 188;
          PACKETOFFSET = 0;
          ret = isPacketStart(&RecStartArray[i], 1925-i);

          if (!ret)
          {
            PACKETSIZE = 192;
            PACKETOFFSET = 4;
            ret = isPacketStart(&RecStartArray[i], 1925-i);
          }
          i++;
        }
      }
      fseeko64(fIn, i-1, SEEK_SET);
      free(RecStartArray);
    }
  }
  if (!OutPacketSize)
    OutPacketSize = PACKETSIZE;
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
  char                  NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], NavFileOld[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], InfFileOld[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE];
  byte                  Buffer[192];
  int                   ReadBytes;
  bool                  DropCurPacket;
  time_t                startTime, endTime;
  long long             CurPCR = 0;
  static bool           ResumeSet = FALSE;
  static int            CurSeg = 0, i = 0, j = 0;
  static dword          BlocksOnePercent, Percent = 0, BlocksSincePercent = 0;
  
  TRACEENTER;
  #ifndef _WIN32
    setvbuf(stdout, NULL, _IOLBF, 4096);  // zeilenweises Buffering, auch bei Ausgabe in Datei
  #endif
  printf("\nRecStrip for Topfield PVR " VERSION "\n");
  printf("(C) 2016 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- based on MovieCutter 3.6 -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)\n");

  // Eingabe-Parameter prüfen
  while ((argc > 1) && (argv && argv[1] && argv[1][0] == '-'))
  {
    switch (argv[1][1])
    {
      case 'n':   RebuildNav = TRUE;      break;
      case 'i':   RebuildInf = TRUE;      break;
      case 'c':   DoCut = TRUE;           break;
      case 's':   DoStrip = TRUE;         break;
      case 'e':   RemoveEPGStream = TRUE; break;
      case 'o':   OutPacketSize = (argv[1][2] == '2') ? 188 : 192; break;
      default:    printf("\nUnknown argument: -%c\n", argv[1][1]);
    }
    argv[1] = argv[0];
    argv++;
    argc--;
  }
  printf("\nParameters:\nDoCut=%s, DoStrip=%s, RemoveEPG=%s, RbldNav=%s, RbldInf=%s, OutPackSize=%hhu\n", (DoCut ? "yes" : "no"), (DoStrip ? "yes" : "no"), (RemoveEPGStream ? "yes" : "no"), (RebuildNav ? "yes" : "no"), (RebuildInf ? "yes" : "no"), OutPacketSize);

  // Eingabe-Dateinamen lesen
  if (argc > 1)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    RecFileIn[sizeof(RecFileIn)-1] = '\0';
  }
  else RecFileIn[0] = '\0';
  if (argc > 2)
  {
    strncpy(RecFileOut, argv[2], sizeof(RecFileOut));
    RecFileOut[sizeof(RecFileOut)-1] = '\0';
  }
  else RecFileOut[0] = '\0';

  if (!*RecFileIn || ((DoCut || DoStrip || OutPacketSize) && !*RecFileOut) || (RemoveEPGStream && !DoStrip))
  {
    printf("\nUsage:\n------\n");
    printf(" RecStrip <RecFile>           Scan the rec file and set Crypt- und RbN-Flag in\n"
           "                              the source inf.\n"
           "                              If source inf/nav not present, generate them new.\n\n");
    printf(" RecStrip <InFile> <OutFile>  Create a copy of the input rec.\n"
           "                              If a inf/nav/cut file exists, copy and adapt it.\n"
           "                              If source inf is present, set Crypt and RbN-Flag\n"
           "                              and reset ToBeStripped if successfully stripped.\n");
    printf("\nParameters:\n-----------\n");
    printf("  -n/-i:     Always generate a new nav/inf file from the rec.\n"
           "             If no OutFile is specified, source nav/inf will be overwritten!\n\n");
    printf("  -c:        Cut the recording according to cut-file. (if OutFile specified)\n"
           "             Copies only the selected segments into the new rec.\n\n");
    printf("  -s:        Strip the recording. (if OutFile specified)\n"
           "             Removes unneeded filler packets. May be combined with -c and -e.\n\n");
    printf("  -e:        Remove also the EPG data. (only in combination with -s)\n\n");
    printf("  -o1/-o2:   Set the packet size for output-rec: \n"
           "             1: PacketSize = 192 Bytes, 2: PacketSize = 188 Bytes.\n");
    printf("\nExamples:\n---------\n");
    printf("  RecStrip 'RecFile.rec'                     RebuildNav.\n\n");
    printf("  RecStrip -s -e InFile.rec OutFile.rec      Strip recording.\n\n");
    printf("  RecStrip -n -i -o1 InFile.ts OutFile.rec   Convert TS to Topfield rec.\n\n");
    printf("  RecStrip -c -s -e -o2 InRec.rec OutMpg.ts  Strip & cut rec and convert to TS.\n");
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
    RecFileBlocks = CalcBlockSize(RecFileSize);
    BlocksOnePercent = RecFileBlocks / 100;
    if (GetPacketSize(RecFileIn))  // Verschiebt ggf. den Dateianfang bis zum ersten 'G'
      printf("File size of rec: %llu, packet size: %u\n", RecFileSize, PACKETSIZE);
    else
    {
      printf("ERROR: Ivalid TS packet size.\n");
      TRACEEXIT;
      exit(3);
    }
  }
  else
  {
    printf("ERROR: Cannot open %s.\n", RecFileIn);
    TRACEEXIT;
    exit(2);
  }

  // ggf. Output-File öffnen
  if (*RecFileOut)
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
      exit(4);
    }
  }

  // ggf. inf-File einlesen
  snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
  printf("\nInf file: %s\n", InfFileIn);

/*
WENN inf existiert
  -> einlesen

WENN OutFile nicht existiert
  -> inf = in
SONST
  WENN inf existiert oder Rebuildinf
    -> inf = out
  SONST
    -> inf = NULL */

  if (LoadInfFile(InfFileIn))
  {
    InfFileOld[0] = '\0';
    if (*RecFileOut)
    {
      if (RebuildInf || *InfFileIn)
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
      else
        InfFileOut[0] = '\0';
    }
    else
    {
      if (*InfFileIn)
      {
        if (RebuildInf)
        {
          InfFileIn[0] = '\0';
          snprintf(InfFileOld, sizeof(InfFileOld), "%s.inf", RecFileIn);
          snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf_new", RecFileIn);
        }
        else
          InfFileOut[0] = '\0';
      }
      else
      {
        RebuildInf = TRUE;
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileIn);
      }
    }
    if (*InfFileOut)
      printf("Inf output: %s\n", InfFileOut);
  }
  else
  {
    fclose(fIn); fIn = NULL;
    if (fOut) fclose(fOut); fOut = NULL;
    TRACEEXIT;
    exit(5);
  }

  if (InfDuration)
    BlocksOneSecond = RecFileBlocks / InfDuration;

  if (AlreadyStripped)
  {
    printf("INFO: File has already been stripped.\n");
/*    fclose(fIn); fIn = NULL;
    if (fOut) fclose(fOut); fOut = NULL;
    CloseInfFile(NULL, NULL, FALSE);
    TRACEEXIT;
    exit(0); */
  }


/*
WENN nav existiert
  -> einlesen

WENN OutFile nicht existiert
  -> nav = in
SONST
  WENN nav existiert oder Rebuildinf
    -> nav = out
  SONST
    -> nav = NULL */

  // ggf. nav-Files öffnen
  snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);
  printf("\nNav file: %s\n", NavFileIn);
  if (!LoadNavFileIn(NavFileIn))
  {
    printf("WARNING: Cannot open nav file %s.\n", NavFileIn);
    NavFileIn[0] = '\0';
  }
  
  NavFileOld[0] = '\0';
  if (*RecFileOut)
  {
    if (RebuildNav || *NavFileIn)
    {
      if (DoStrip || OutPacketSize != PACKETSIZE) RebuildNav = TRUE;
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
    }
    else
      NavFileOut[0] = '\0';
  }
  else
  {
    if (*NavFileIn)
    {
      if (RebuildNav)
      {
        snprintf(NavFileOld, sizeof(NavFileOld), "%s.nav", RecFileIn);
        snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav_new", RecFileIn);
      }
      else
        NavFileOut[0] = '\0';
    }
    else
    {
      RebuildNav = TRUE;
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileIn);
    }
  }
  if (*NavFileOut && LoadNavFileOut(NavFileOut))
    printf("Nav output: %s\n", NavFileOut);

  // ggf. cut-File einlesen
  GetCutNameFromRec(RecFileIn, CutFileIn);
  printf("\nCut file: %s\n", CutFileIn);

  if (CutFileLoad(CutFileIn))
  {
    if (*RecFileOut)
    {
      GetCutNameFromRec(RecFileOut, CutFileOut);
      printf("Cut output: %s\n", CutFileOut);
    }
  }
  else
    CutFileIn[0] = '\0';
  printf("\n");

  // -----------------------------------------------
  // Datei paketweise einlesen und verarbeiten
  // -----------------------------------------------
  printf("\n");
  time(&startTime);
  memset(Buffer, 0, sizeof(Buffer));
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
          Percent = (100*CurPosBlocks / RecFileBlocks);
          CurBlockBytes = 0;
          BlocksSincePercent = 0;

          if (BookmarkInfo)
          {
            // Bookmarks kurz vor der Schnittstelle löschen
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))
              j--;

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) löschen
            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CurPosBlocks + 3*BlocksOneSecond))
              DeleteBookmark(j);

            // neues Bookmark an Schnittstelle setzen
            if ((CurrentPosition-PositionOffset > 0) && (CurPosBlocks + 3*BlocksOneSecond < RecFileBlocks))
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
    ReadBytes = fread(&Buffer[4-PACKETOFFSET], 1, PACKETSIZE, fIn);
    if (ReadBytes == PACKETSIZE)
    {
      if (Buffer[4] == 'G' && ((tTSPacket*) &Buffer[4])->Scrambling_Ctrl <= 0x01)
      {
        int CurPID = TsGetPID((tTSPacket*) &Buffer[4]);
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
            switch (ProcessTSPacket(&Buffer[4], CurrentPosition + PACKETOFFSET))
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
        while ((i < NrSegmentMarker) && (CurPosBlocks >= SegmentMarker[i].Block))
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

        // PCR berechnen
        if (OutPacketSize > PACKETSIZE)
        {
          if (GetPCR(&Buffer[4], &CurPCR))
          {
            if (LastPCR)
              CurTimeStep = ((dword)CurPCR - LastPCR) / ((dword)(CurrentPosition-PosLastPCR) / PACKETSIZE);
            LastPCR = (dword)CurPCR;
            PosLastPCR = CurrentPosition;
          }
          LastTimeStamp += CurTimeStep;
          Buffer[0] = ((byte*)&LastTimeStamp)[3];
          Buffer[1] = ((byte*)&LastTimeStamp)[2];
          Buffer[2] = ((byte*)&LastTimeStamp)[1];
          Buffer[3] = ((byte*)&LastTimeStamp)[0];
        }

        if (!DropCurPacket)
        {
          // NAV NEU BERECHNEN
          if (PACKETSIZE > OutPacketSize)       PositionOffset += 4;  // Reduktion auf 188 Byte Packets
          else if (PACKETSIZE < OutPacketSize)  PositionOffset -= 4;

          // nav-Eintrag korrigieren und ausgeben, wenn Position < CurrentPosition ist (um PositionOffset reduzieren)
          if (RebuildNav)
          {
            if (CurPID == VideoPID)
              ProcessNavFile(CurrentPosition + PACKETOFFSET, PositionOffset, (tTSPacket*) &Buffer[4]);
          }
          else if (DoCut && !RebuildNav && *NavFileIn)
            QuickNavProcess(CurrentPosition, PositionOffset);

          // PACKET AUSGEBEN
          if (fOut && !fwrite(&Buffer[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))  // Reduktion auf 188 Byte Packets
          {
            printf("ERROR: Failed writing to output file.\n");
            fclose(fIn); fIn = NULL;
            fclose(fOut); fOut = NULL;
            CloseNavFiles();
            CutFileClose(NULL, FALSE);
            CloseInfFile(NULL, NULL, FALSE);
            TRACEEXIT;
            exit(6);
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
          BlocksSincePercent++;
          CurBlockBytes = 0;
        }

        if (BlocksSincePercent >= BlocksOnePercent)
        {
          Percent++;
          BlocksSincePercent = 0;
          fprintf(stderr, "%3d %%\r", Percent);
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
          if (Buffer[4] == 'G')
          {
            printf("ERROR: Scrambled TS - Scrambling bit at position %llu.\n", CurrentPosition);
            SetInfCryptFlag(InfFileIn);
          }
          else
            printf("ERROR: Incorrect TS - Missing sync byte at position %llu.\n", CurrentPosition);
          fclose(fIn); fIn = NULL;
          if (fOut) fclose(fOut); fOut = NULL;
          CloseNavFiles();
          CutFileClose(CutFileOut, TRUE);
          CloseInfFile(InfFileOut, NULL, TRUE);
          TRACEEXIT;
          exit(7);
        }
      }
    }
    else
      break; 
  }
  printf("\n");

  if (!CloseNavFiles())
    printf("WARNING: Failed closing the nav file.\n");

  if ((DoCut || RebuildInf) && LastTimems)
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
      CutFileClose(CutFileOut, TRUE);
      CloseInfFile(InfFileOut, NULL, TRUE);
      TRACEEXIT;
      exit(8);
    }
    fOut = NULL;
  }

  if (*CutFileIn && *RecFileOut && !CutFileClose(CutFileOut, TRUE))
    printf("WARNING: Cannot create cut %s.\n", CutFileOut);

  if (*InfFileOut && !CloseInfFile(InfFileOut, InfFileIn, TRUE))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  if (*NavFileOld)
  {
    char NavFileBak[FBLIB_DIR_SIZE];
    snprintf(NavFileBak, sizeof(NavFileBak), "%s_bak", NavFileOld);
    remove(NavFileBak);
    rename(NavFileOld, NavFileBak);
    rename(NavFileOut, NavFileOld);
  }
  if (*InfFileOld)
  {
    char InfFileBak[FBLIB_DIR_SIZE];
    snprintf(InfFileBak, sizeof(InfFileBak), "%s_bak", InfFileOld);
    remove(InfFileBak);
    rename(InfFileOld, InfFileBak);
    rename(InfFileOut, InfFileOld);
  }

  NrPackets = ((CurrentPosition + PACKETSIZE-1) / PACKETSIZE);
  if (NrPackets > 0)
    printf("\nPackets: %llu, FillerNALUs: %llu (%llu%%), ZeroByteStuffing: %llu (%llu%%), NullPackets: %llu (%llu%%), EPG: %llu (%llu%%), Dropped (all): %lli (%llu%%)\n", NrPackets, NrDroppedFillerNALU, NrDroppedFillerNALU*100/NrPackets, NrDroppedZeroStuffing, NrDroppedZeroStuffing*100/NrPackets, NrDroppedNullPid, NrDroppedNullPid*100/NrPackets, NrDroppedEPGPid, NrDroppedEPGPid*100/NrPackets, NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid, (NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedNullPid+NrDroppedEPGPid)*100/NrPackets);
  else
    printf("\n\n0 Packets!\n");

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  #ifdef _WIN32
//    getchar();
  #endif
  TRACEEXIT;
  exit(0);
}
