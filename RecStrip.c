/*
  RecStrip for Topfield PVR
  (C) 2016 Christian W�nsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan P�schel)
  Contains portions of RebuildNav (Alexander �lzant)
  Contains portions of MovieCutter (Christian W�nsch)
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
#include "HumaxHeader.h"


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
word                    VideoPID = 0, TeletextPID = 0;
bool                    isHDVideo = FALSE, AlreadyStripped = FALSE, HumaxSource = FALSE;
bool                    DoStrip = FALSE, DoCut = FALSE, RemoveEPGStream = FALSE, RemoveTeletext = FALSE, RebuildNav = FALSE, RebuildInf = FALSE;

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker         *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NewStartTimeOffset = 0, CutTimeOffset = 0;

// Lokale Variablen
static FILE            *fIn = NULL;  // dirty Hack: erreichbar machen f�r InfProcessor
static FILE            *fOut = NULL;
static byte            *PendingBuf = NULL;
static int              PendingBufLen = 0, PendingBufStart = 0;
static bool             isPending = FALSE;

static unsigned int     RecFileBlocks = 0;
static long long        CurrentPosition = 0, PositionOffset = 0, NrPackets;
static unsigned int     CurPosBlocks = 0, CurBlockBytes = 0, BlocksOneSecond = 250;
static long long        NrDroppedFillerNALU = 0, NrDroppedZeroStuffing = 0, NrDroppedAdaptation = 0, NrDroppedNullPid = 0, NrDroppedEPGPid = 0, NrDroppedTxtPid=0, NrIgnoredPackets = 0;
static dword            LastPCR = 0, LastTimeStamp = 0, CurTimeStep = 5000;
static long long        PosLastPCR = 0;


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
  // Workaround f�r die Division durch BLOCKSIZE (9024)
  // Primfaktorenzerlegung: 9024 = 2^6 * 3 * 47
  // max. Dateigr��e: 256 GB (d�rfte reichen...)
  return (dword)(Size >> 6) / 141;
}

static int GetPacketSize(FILE *RecFile, int *OutOffset)
{
  byte                 *Buffer = NULL;
  int                   Offset = -1;

  TRACEENTER;

  Buffer = (byte*) malloc(5573);
  if (Buffer)
  {
    fseeko64(RecFile, +9024, SEEK_CUR);
    if (fread(Buffer, 1, 5573, RecFile) == 5573)
    {
      char *p = strrchr(RecFileIn, '.');
      if (p && strcmp(p, ".vid") == 0)
        HumaxSource = TRUE;

      PACKETSIZE = 188;
      PACKETOFFSET = 0;
      Offset = FindNextPacketStart(Buffer, 5573);

      if (Offset < 0)
      {
        PACKETSIZE = 192;
        PACKETOFFSET = 4;
        Offset = FindNextPacketStart(Buffer, 5573);
      }
    }
    free(Buffer);
  }
  if(OutOffset) *OutOffset = Offset;

  TRACEEXIT;
  return ((Offset >= 0) ? PACKETSIZE : 0);
}

bool isPacketStart(const byte PacketArray[], int ArrayLen)  // braucht 9*192+5 = 1733 / 3*192+5 = 581
{
  int                   i;
  bool                  ret = TRUE;

  TRACEENTER;
  for (i = 0; i < 10; i++)
  {
    if (PACKETOFFSET + (i * PACKETSIZE) >= ArrayLen)
    {
      if (i < 3) ret = FALSE;
      break;
    }
    if (PacketArray[PACKETOFFSET + (i * PACKETSIZE)] != 'G')
    {
      ret = FALSE;
      break;
    }
  }
  TRACEEXIT;
  return ret;
}

int FindNextPacketStart(const byte PacketArray[], int ArrayLen)  // braucht 20*192+1733 = 5573 / 1185+1733 = 2981
{
  int ret = -1;
  int i;

  TRACEENTER;
  for (i = 0; i <= 20; i++)
  {
    if (PACKETOFFSET + (i * PACKETSIZE) >= ArrayLen)
      break;

    if (PacketArray[PACKETOFFSET + (i * PACKETSIZE)] == 'G')
    {
      if (isPacketStart(&PacketArray[i * PACKETSIZE], ArrayLen - i*PACKETSIZE))
      {
        ret = i * PACKETSIZE;
        break;
      }
    }
  }
  
  if (ret < 0)
  {
    for (i = 0; i <= 1184; i++)
    {
      if (i + PACKETOFFSET >= ArrayLen)
        break;

      if (PacketArray[i + PACKETOFFSET] == 'G')
      {
        if (isPacketStart(&PacketArray[i], ArrayLen - i))
        {
          ret = i;
          break;
        }
      }
    }
  }
  return ret;
  TRACEEXIT;
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
  bool                  ResumeSet = FALSE;
  int                   CurSeg = 0, i = 0, j = 0;
  dword                 BlocksOnePercent, Percent = 0, BlocksSincePercent = 0;
  bool                  ret = TRUE;

  TRACEENTER;
  #ifndef _WIN32
    setvbuf(stdout, NULL, _IOLBF, 4096);  // zeilenweises Buffering, auch bei Ausgabe in Datei
  #endif
  printf("\nRecStrip for Topfield PVR " VERSION "\n");
  printf("(C) 2016 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- based on MovieCutter 3.6 -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)\n");

  // Eingabe-Parameter pr�fen
  while ((argc > 1) && (argv && argv[1] && argv[1][0] == '-'))
  {
    switch (argv[1][1])
    {
      case 'n':   RebuildNav = TRUE;      break;
      case 'i':   RebuildInf = TRUE;      break;
      case 'c':   DoCut = TRUE;           break;
      case 's':   DoStrip = TRUE;         break;
      case 'e':   RemoveEPGStream = TRUE; break;
      case 't':   RemoveTeletext = TRUE;  break;
      case 'o':   OutPacketSize = (argv[1][2] == '2') ? 188 : 192; break;
      default:    printf("\nUnknown argument: -%c\n", argv[1][1]);
    }
    argv[1] = argv[0];
    argv++;
    argc--;
  }
  printf("\nParameters:\nDoCut=%s, DoStrip=%s, RmEPG=%s, RmTxt=%s, RbldNav=%s, RbldInf=%s, PkSize=%hhu\n", (DoCut ? "yes" : "no"), (DoStrip ? "yes" : "no"), (RemoveEPGStream ? "yes" : "no"), (RemoveTeletext ? "yes" : "no"), (RebuildNav ? "yes" : "no"), (RebuildInf ? "yes" : "no"), OutPacketSize);

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

  if (!*RecFileIn || ((DoCut || DoStrip || OutPacketSize) && !*RecFileOut) || ((RemoveEPGStream || RemoveTeletext) && !DoStrip))
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
           "             Removes unneeded filler packets. May be combined with -c, -e, -t.\n\n");
    printf("  -e:        Remove also the EPG data. (only with -s)\n\n");
    printf("  -t:        Remove also the teletext data. (only with -s)\n\n");
    printf("  -o1/-o2:   Change the packet size for output-rec: \n"
           "             1: PacketSize = 192 Bytes, 2: PacketSize = 188 Bytes.\n");
    printf("\nExamples:\n---------\n");
    printf("  RecStrip 'RecFile.rec'                     RebuildNav.\n\n");
    printf("  RecStrip -s -e InFile.rec OutFile.rec      Strip recording.\n\n");
    printf("  RecStrip -n -i -o1 InFile.ts OutFile.rec   Convert TS to Topfield rec.\n\n");
    printf("  RecStrip -c -s -e -o2 InRec.rec OutMpg.ts  Strip & cut rec and convert to TS.\n");
    TRACEEXIT;
    exit(1);
  }

  // Input-File �ffnen
  printf("\nInput file: %s\n", RecFileIn);
  if (HDD_GetFileSize(RecFileIn, &RecFileSize))
    fIn = fopen(RecFileIn, "rb");
  if (fIn)
  {
    int FileOffset;
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);
    RecFileBlocks = CalcBlockSize(RecFileSize);
    BlocksOnePercent = RecFileBlocks / 100;
    if (GetPacketSize(fIn, &FileOffset))
    {
      CurrentPosition = FileOffset;
      PositionOffset = FileOffset;
      if (!OutPacketSize)
        OutPacketSize = PACKETSIZE;
      fseeko64(fIn, CurrentPosition, SEEK_SET);
      printf("File size of rec: %llu, packet size: %hhu\n", RecFileSize, PACKETSIZE);
    }
    else
    {
      printf("ERROR: Ivalid TS packet size.\n");
      fclose(fIn); fIn = NULL;
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

  // ggf. Output-File �ffnen
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
      fclose(fIn); fIn = NULL;
      printf("ERROR: Cannot create %s.\n", RecFileOut);
      TRACEEXIT;
      exit(4);
    }
  }

  // ggf. inf-File einlesen
  snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
  printf("\nInf file: %s\n", InfFileIn);
  InfFileOld[0] = '\0';

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
    fseeko64(fIn, CurrentPosition, SEEK_SET);
    if (*RecFileOut)
    {
      if (RebuildInf || *InfFileIn)
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
      else
        InfFileOut[0] = '\0';
    }
    else
    {
      if (RebuildInf)
      {
        InfFileIn[0] = '\0';
        snprintf(InfFileOld, sizeof(InfFileOld), "%s.inf", RecFileIn);
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf_new", RecFileIn);
      }
      else if (*InfFileIn)
      {
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
  if (VideoPID == 0)
  {
    printf("ERROR: No video PID determined.\n");
    fclose(fIn); fIn = NULL;
    if (fOut) fclose(fOut); fOut = NULL;
    CloseInfFile(NULL, NULL, FALSE);
    TRACEEXIT;
    exit(6);
  }

  if (HumaxSource && fOut)
  {
    printf("Generate new PAT/PMT for Humax recording.\n");
    if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;
    if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;
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

  // ggf. nav-Files �ffnen
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
      if (DoStrip || RemoveEPGStream || RemoveTeletext || OutPacketSize != PACKETSIZE) RebuildNav = TRUE;
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
  NavProcessor_Init();
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

  // Pending Buffer initialisieren
  if (DoStrip)
  {
    NALUDump_Init();
    PendingBuf = (byte*) malloc(PENDINGBUFSIZE);
    if (!PendingBuf)
    {
      printf("ERROR: Memory allocation failed.\n");
      fclose(fIn); fIn = NULL;
      fclose(fOut); fOut = NULL;
      CloseNavFiles();
      CutFileClose(NULL, FALSE);
      CloseInfFile(NULL, NULL, FALSE);
      TRACEEXIT;
      exit(7);
    }
  }


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
          long long SkippedBytes = (((unsigned long long)SegmentMarker[CurSeg].Block) * 9024) - CurrentPosition;
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
            // Bookmarks kurz vor der Schnittstelle l�schen
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-SkippedBytes)))
              j--;

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) l�schen
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

      // n�chsten (zu erhaltenen) SegmentMarker anpassen
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
        if (DoStrip /*|| RemoveEPGStream || RemoveTeletext*/)
        {
          if (CurPID == 0x1FFF)
          {
            NrDroppedNullPid++;
            DropCurPacket = TRUE;
          }
          else if (RemoveEPGStream && CurPID == 0x12)
          {
            NrDroppedEPGPid++;
            DropCurPacket = TRUE;
          }
          else if (RemoveTeletext && CurPID == TeletextPID)
          {
            NrDroppedTxtPid++;
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

              case 3:
                // PendingPacket -> PendingBuffer aktivieren, Pakete werden ab jetzt dort hineinkopiert
                isPending = TRUE;
                PendingBufStart = OutPacketSize;
                break;

              case 4:
                NrDroppedAdaptation++;
                DropCurPacket = TRUE;
                break;

              case -1:
                // PendingPacket soll nicht gel�scht werden
                PendingBufStart = 0;
//                break;

              default:
                // ein Paket wird behalten -> vorher PendingBuffer in Ausgabe schreiben, ggf. PendingPacket l�schen
                if (isPending)
                {
                  if (PendingBufStart > 0)
                  {
                    PositionOffset += OutPacketSize;
                    NrDroppedZeroStuffing++;
                  }
                  if (fOut && (PendingBufLen > PendingBufStart))
                    if (!fwrite(&PendingBuf[PendingBufStart], PendingBufLen-PendingBufStart, 1, fOut))
                      ret = FALSE;
                  isPending = FALSE;
                  PendingBufLen = 0;
                }
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
          else if (*RecFileOut && *NavFileIn)
            QuickNavProcess(CurrentPosition, PositionOffset);

          // PACKET AUSGEBEN
          if (fOut)
          {
            // Wenn PendingPacket -> Daten in Puffer schreiben
            if (isPending)
            {
              // Wenn PendingBuffer voll ist -> alle Pakete au�er PendingPacket in Ausgabe schreiben
              if (PendingBufLen + OutPacketSize > PENDINGBUFSIZE)
              {
                if (!fwrite(&PendingBuf[OutPacketSize], PendingBufLen-OutPacketSize, 1, fOut))
                  ret = FALSE;
                PendingBufLen = OutPacketSize;
              }

              // Dann neues Paket in den PendingBuffer schreiben
              memcpy(&PendingBuf[PendingBufLen], &Buffer[(OutPacketSize==192 ? 0 : 4)], OutPacketSize);
              PendingBufLen += OutPacketSize;
            }
            else
              // Daten direkt in Ausgabe schreiben
              if (!fwrite(&Buffer[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))  // Reduktion auf 188 Byte Packets
                ret = FALSE;

            if (!ret)
            {
              printf("ERROR: Failed writing to output file.\n");
              fclose(fIn); fIn = NULL;
              fclose(fOut); fOut = NULL;
              CloseNavFiles();
              CutFileClose(NULL, FALSE);
              CloseInfFile(NULL, NULL, FALSE);
              free(PendingBuf);
              TRACEEXIT;
              exit(8);
            }
          }
        }
        else
          // Paket wird entfernt
          PositionOffset += ReadBytes;
      
        CurrentPosition += ReadBytes;
        CurBlockBytes += ReadBytes;
      }
      else
      {
        if ((unsigned long long) CurrentPosition + 4096 >= RecFileSize)
        {
          printf("INFO: Incomplete TS - Ignoring last %lld bytes.\n", RecFileSize - CurrentPosition);
          fclose(fIn); fIn = NULL;
        }
        else
        {
          if (Buffer[4] == 'G')
          {
            printf("WARNING: Scrambled TS - Scrambling bit at position %lld -> packet ignored.\n", CurrentPosition);
            SetInfCryptFlag(InfFileIn);
            PositionOffset += ReadBytes;
            CurrentPosition += ReadBytes;
            CurBlockBytes += ReadBytes;
            NrIgnoredPackets++;
          }
          else if (HumaxSource && (*((dword*)&Buffer[4]) == HumaxHeaderAnfang) && ((unsigned int)CurrentPosition % HumaxHeaderIntervall == HumaxHeaderIntervall-HumaxHeaderLaenge))
          {
            fseeko64(fIn, +HumaxHeaderLaenge-ReadBytes, SEEK_CUR);
            PositionOffset += HumaxHeaderLaenge;
            CurrentPosition += HumaxHeaderLaenge;
            CurBlockBytes += HumaxHeaderLaenge;
/*            if (fOut)
            {
              ((tTSPacket*) &PATPMTBuf[4])->ContinuityCount++;
              ((tTSPacket*) &PATPMTBuf[196])->ContinuityCount++;
              if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
                PositionOffset -= OutPacketSize;
              if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
                PositionOffset -= OutPacketSize;
            }  */
          }
          else
          {
            int Offset = 0;
            byte *Buffer2 = (byte*) malloc(5573);
            if (Buffer2)
            {
              memcpy(Buffer2, &Buffer[4-PACKETOFFSET], PACKETSIZE);
              if (fread(&Buffer2[PACKETSIZE], 1, 5573-PACKETSIZE, fIn) == (unsigned int)5573-PACKETSIZE)
                Offset = FindNextPacketStart(Buffer2, 5573);
              free(Buffer2);
            }
            if (Offset > 0)
            {
              if (Offset % PACKETSIZE == 0)
                printf("WARNING: Missing sync byte at position %lld -> %d packets ignored.\n", CurrentPosition, Offset/PACKETSIZE);
              else
                printf("WARNING: Missing sync byte at position %lld -> %d Bytes ignored.\n", CurrentPosition, Offset);
              fseeko64(fIn, CurrentPosition + Offset, SEEK_SET);
              PositionOffset += Offset;
              CurrentPosition += Offset;
              CurBlockBytes += Offset;
              NrIgnoredPackets++;
            }
            else
            {
              printf("ERROR: Incorrect TS - No sync bytes after position %lld -> aborted.\n", CurrentPosition);
              NrIgnoredPackets = 0x0fffffffffffffffLL;
            }
          }
          if (NrIgnoredPackets >= 10)
          {
            if (NrIgnoredPackets < 0x0fffffffffffffffLL)
              printf("ERROR: Too many ignored packets: %lld -> aborted.\n", NrIgnoredPackets);
            fclose(fIn); fIn = NULL;
            if (fOut) fclose(fOut); fOut = NULL;
            CloseNavFiles();
            CutFileClose(CutFileOut, TRUE);
            CloseInfFile(InfFileOut, NULL, TRUE);
            free(PendingBuf);
            TRACEEXIT;
            exit(9);
          }
        }
      }

      if (CurBlockBytes >= 9024)
      {
        CurPosBlocks++;
        BlocksSincePercent++;
        CurBlockBytes -= 9024;
      }

      if (BlocksSincePercent >= BlocksOnePercent)
      {
        Percent++;
        BlocksSincePercent = 0;
        fprintf(stderr, "%3u %%\r", Percent);
      }
    }
    else
      break;
  }
  free(PendingBuf); PendingBuf = NULL;
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
      exit(10);
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
    printf("\nPackets: %lld, FillerNALUs: %lld (%lld%%), ZeroByteStuffing: %lld (%lld%%), AdaptationFields: %lld (%lld%%), NullPackets: %lld (%lld%%), EPG: %lld (%lld%%), Teletext: %lld (%lld%%), Dropped (all): %lld (%lld%%)\n", NrPackets, NrDroppedFillerNALU, NrDroppedFillerNALU*100/NrPackets, NrDroppedZeroStuffing, NrDroppedZeroStuffing*100/NrPackets, NrDroppedAdaptation, NrDroppedAdaptation*100/NrPackets, NrDroppedNullPid, NrDroppedNullPid*100/NrPackets, NrDroppedEPGPid, NrDroppedEPGPid*100/NrPackets, NrDroppedTxtPid, NrDroppedTxtPid*100/NrPackets, NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedAdaptation+NrDroppedNullPid+NrDroppedEPGPid+NrDroppedTxtPid, (NrDroppedFillerNALU+NrDroppedZeroStuffing+NrDroppedAdaptation+NrDroppedNullPid+NrDroppedEPGPid+NrDroppedTxtPid)*100/NrPackets);
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
