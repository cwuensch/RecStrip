/*
  RecStrip for Topfield PVR
  (C) 2016-2020 Christian W�nsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan P�schel)
  Concepts from telxcc (Forers)
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
  #define inline
#endif

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
  #include <io.h>
  #include <sys/utime.h>
#else
  #include <unistd.h>
  #include <utime.h>
#endif

#include <sys/stat.h>
#include <time.h>
#include "type.h"
#include "RecStrip.h"
#include "PESFileLoader.h"
#include "InfProcessor.h"
#include "NavProcessor.h"
#include "CutProcessor.h"
#include "TtxProcessor.h"
#include "RebuildInf.h"
#include "NALUDump.h"
#include "HumaxHeader.h"

//#include "PESProcessor.h"


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
char                    RecFileIn[FBLIB_DIR_SIZE], RecFileOut[FBLIB_DIR_SIZE], OutDir[FBLIB_DIR_SIZE];
char                    MDEpgName[FBLIB_DIR_SIZE], MDTtxName[FBLIB_DIR_SIZE], MDAudName[FBLIB_DIR_SIZE];
byte                    PATPMTBuf[2*192];
unsigned long long      RecFileSize = 0;
SYSTEM_TYPE             SystemType = ST_UNKNOWN;
byte                    PACKETSIZE = 192, PACKETOFFSET = 4, OutPacketSize = 0;
word                    VideoPID = (word) -1, TeletextPID = (word) -1, TeletextPage = 0;
word                    ContinuityPIDs[MAXCONTINUITYPIDS], NrContinuityPIDs = 1;
bool                    isHDVideo = FALSE, AlreadyStripped = FALSE, HumaxSource = FALSE;
bool                    DoStrip = FALSE, DoSkip = FALSE, RemoveScrambled = FALSE, RemoveEPGStream = FALSE, RemoveTeletext = FALSE, ExtractTeletext = FALSE, RebuildNav = FALSE, RebuildInf = FALSE, DoInfoOnly = FALSE, MedionMode = FALSE, MedionStrip = FALSE;
int                     DoCut = 0, DoMerge = 0;  // DoCut: 1=remove_parts, 2=copy_separate, DoMerge: 1=append, 2=merge
int                     curInputFile = 0, NrInputFiles = 1;
int                     dbg_DelBytesSinceLastVid = 0;

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker2        *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NewStartTimeOffset = 0;
int                     CutTimeOffset = 0;

// Lokale Variablen
static char             NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], NavFileOld[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], InfFileOld[FBLIB_DIR_SIZE], InfFileFirstIn[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE], TeletextOut[FBLIB_DIR_SIZE];
static FILE            *fIn = NULL;
static FILE            *fOut = NULL;
static byte            *PendingBuf = NULL;
static int              PendingBufLen = 0, PendingBufStart = 0;
static bool             isPending = FALSE;

static unsigned int     RecFileBlocks = 0;
static long long        CurrentPosition = 0, PositionOffset = 0, NrPackets = 0;
static unsigned int     CurPosBlocks = 0, CurBlockBytes = 0, BlocksOneSecond = 250, BlocksOnePercent;
static unsigned int     NrSegments = 0, NrCopiedSegments = 0;
long long               NrDroppedZeroStuffing=0;
static long long        NrDroppedFillerNALU=0, NrDroppedAdaptation=0, NrDroppedNullPid=0, NrDroppedEPGPid=0, NrDroppedTxtPid=0, NrScrambledPackets=0, CurScrambledPackets=0, NrIgnoredPackets=0;
static dword            LastPCR = 0, LastTimeStamp = 0, CurTimeStep = 1200;
static signed char      ContinuityCtrs[MAXCONTINUITYPIDS];
static bool             ResumeSet = FALSE;


static bool HDD_FileExist(const char *AbsFileName)
{
  return (AbsFileName && (access(AbsFileName, 0) == 0));
}

static bool HDD_DirExist(const char *AbsDirName)
{
  struct stat           statbuf;
  return ((stat(AbsDirName, &statbuf) == 0) && (statbuf.st_mode & S_IFDIR));
}

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

static inline time_t TF2UnixTime(tPVRTime TFTimeStamp, byte TFTimeSec)
{ 
  return (MJD(TFTimeStamp) - 0x9e8b) * 86400 + HOUR(TFTimeStamp) * 3600 + MINUTE(TFTimeStamp) * 60 + TFTimeSec;
}

static bool HDD_SetFileDateTime(char const *AbsFileName, time_t NewDateTime)
{
  struct stat64         statbuf;
  struct utimbuf        timebuf;

  if(NewDateTime == 0)
    NewDateTime = time(NULL);

  if(AbsFileName && ((unsigned long)NewDateTime < 0xD0790000))
  {
    if(stat64(AbsFileName, &statbuf) == 0)
    {
      timebuf.actime = statbuf.st_atime;
      timebuf.modtime = NewDateTime;
      utime(AbsFileName, &timebuf);
      TRACEEXIT;
      return TRUE;
    }
  }
  TRACEEXIT;
  return FALSE;
}

static void GetNextFreeCutName(const char *SourceFileName, char *const OutCutFileName, const char* AbsDirectory)
{
  char                  CheckFileName[2048];
  size_t                NameLen, ExtStart, NameStart=0, DirLen=0;
  int                   i = 0;

  TRACEENTER;
  if(OutCutFileName) OutCutFileName[0] = '\0';

  if (SourceFileName && OutCutFileName)
  {
    const char *p = strrchr(SourceFileName, '.');  // ".rec" entfernen
    NameLen = ExtStart = ((p) ? (size_t)(p - SourceFileName) : strlen(SourceFileName));
//    if((p = strstr(&SourceFileName[NameLen - 10], " (Cut-")) != NULL)
//      NameLen = p - SourceFileName;        // wenn schon ein ' (Cut-xxx)' vorhanden ist, entfernen

    if (AbsDirectory && *AbsDirectory)
    {
      const char *p = strrchr(SourceFileName, PATH_SEPARATOR);
      DirLen = strlen(AbsDirectory) + 1;
      if(p)
      {
        NameStart = (size_t)(p - SourceFileName + 1);
        NameLen -= NameStart;
      }
      snprintf(CheckFileName, sizeof(CheckFileName), "%s%c", AbsDirectory, PATH_SEPARATOR);
    }
    strncpy(&CheckFileName[DirLen], &SourceFileName[NameStart], min(NameLen, sizeof(CheckFileName)-DirLen));

    do
    {
      i++;
      snprintf(&CheckFileName[DirLen+NameLen], sizeof(CheckFileName)-DirLen-NameLen, " (Cut-%d)%s", i, &SourceFileName[ExtStart]);
    } while (HDD_FileExist(CheckFileName));

    strcpy(OutCutFileName, CheckFileName);
  }
  TRACEEXIT;
}


// ----------------------------------------------
// *****  Analyse von REC-Files  *****
// ----------------------------------------------

static inline dword CalcBlockSize(long long Size)
{
  // Workaround f�r die Division durch BLOCKSIZE (9024)
  // Primfaktorenzerlegung: 9024 = 2^6 * 3 * 47
  // max. Dateigr��e: 256 GB (d�rfte reichen...)
  if (Size >= 0)
    return (dword)(Size >> 6) / 141;
  else
  {
    Size = -Size;
    return (dword)(-((Size >> 6) / 141));
  }
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

void DeleteSegmentMarker(int MarkerIndex, bool FreeCaption)
{
  int i;
  TRACEENTER;

  if((MarkerIndex >= 0) && (MarkerIndex < NrSegmentMarker - 1))
  {
    if (FreeCaption && SegmentMarker[MarkerIndex].pCaption)
      free(SegmentMarker[MarkerIndex].pCaption);

    for(i = MarkerIndex; i < NrSegmentMarker - 1; i++)
      memcpy(&SegmentMarker[i], &SegmentMarker[i + 1], sizeof(tSegmentMarker2));

    memset(&SegmentMarker[NrSegmentMarker-1], 0, sizeof(tSegmentMarker2));
    NrSegmentMarker--;

    if(ActiveSegment >= MarkerIndex && ActiveSegment > 0) ActiveSegment--;
    if(ActiveSegment >= NrSegmentMarker - 1) ActiveSegment = NrSegmentMarker - 2;
  }
  TRACEEXIT;
}

static void DeleteBookmark(dword BookmarkIndex)
{
  dword i;
  TRACEENTER;

  if (BookmarkInfo /*&& (BookmarkIndex >= 0)*/ && (BookmarkIndex < BookmarkInfo->NrBookmarks))
  {
    for(i = BookmarkIndex; i < BookmarkInfo->NrBookmarks - 1; i++)
      BookmarkInfo->Bookmarks[i] = BookmarkInfo->Bookmarks[i + 1];
    BookmarkInfo->Bookmarks[BookmarkInfo->NrBookmarks - 1] = 0;
    BookmarkInfo->NrBookmarks--;
  }
  TRACEEXIT;
}

static void AddBookmark(dword BookmarkIndex, dword BlockNr)
{
  dword i;
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


// --------------------------------------------------
// *****  �ffnen / Schlie�en der Output-Files  *****
// --------------------------------------------------

static bool OpenInputFiles(char *RecFileIn, bool FirstTime)
{
  bool                  ret = TRUE;
//  byte                 *InfBuf_tmp = NULL;
  tSegmentMarker2      *Segments_tmp = NULL;

  // dirty hack: aktuelle Pointer f�r InfBuffer und SegmentMarker speichern
  byte                 *InfBuffer_bak = InfBuffer;
  TYPE_RecHeader_Info  *RecHeaderInfo_bak = RecHeaderInfo;
  TYPE_Bookmark_Info   *BookmarkInfo_bak = BookmarkInfo;
  tPVRTime              OrigStartTime_bak = OrigStartTime;
//  dword                 InfDuration_bak = InfDuration;

  tSegmentMarker2      *SegmentMarker_bak = SegmentMarker;
  int                   NrSegmentMarker_bak = NrSegmentMarker;
  int                   k;

  TRACEENTER;

  if (!FirstTime)
  {
//    InfBuf_tmp = (byte*) malloc(32768);
    Segments_tmp = (tSegmentMarker2*) malloc(NRSEGMENTMARKER * sizeof(tSegmentMarker2));

    if (!Segments_tmp || !InfProcessor_Init())
    {
      InfBuffer = InfBuffer_bak;
//      free(InfBuf_tmp); InfBuf_tmp = NULL;
      free(Segments_tmp); Segments_tmp = NULL;
      printf("  ERROR: Not enough memory!\n");
      TRACEEXIT;
      return FALSE;
    }

    // dirty hack: InfBuffer und SegmentMarker auf tempor�re Buffer umbiegen
/*    InfBuffer = InfBuf_tmp;
    memset(InfBuffer, 0, 32768);
    RecHeaderInfo = (TYPE_RecHeader_Info*) InfBuffer;
    BookmarkInfo = &(((TYPE_RecHeader_TMSS*)InfBuffer)->BookmarkInfo); */
    SegmentMarker = Segments_tmp;
    NrSegmentMarker = 0;
  }

  // Spezialanpassung Medion
  if (MedionMode)
  {
    char                MDBaseName[FBLIB_DIR_SIZE];
    char               *p;
    int                 len;

    strcpy(MDBaseName, RecFileIn);
    if((p = strrchr(MDBaseName, '.'))) *p = '\0';
    if(((len = strlen(MDBaseName)) > 6) && (strncmp(&MDBaseName[len-6], "_video", 6) == 0)) MDBaseName[len-6] = '\0';
    snprintf(MDEpgName, sizeof(MDEpgName), "%s_epg.txt", MDBaseName);
    snprintf(MDTtxName, sizeof(MDTtxName), "%s_ttx.pes", MDBaseName);
    snprintf(MDAudName, sizeof(MDEpgName), "%s_audio1.pes", MDBaseName);
  }

  printf("\nInput file: %s\n", RecFileIn);

  if (HDD_GetFileSize(RecFileIn, &RecFileSize))
    fIn = fopen(RecFileIn, "rb");
  if (fIn)
  {
    int FileOffset = 0, PS;
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);

    if (MedionMode == 1)
    {
      unsigned long long AddSize = 0;
      if(HDD_GetFileSize(MDAudName, &AddSize))  RecFileSize += AddSize;
      if(HDD_GetFileSize(MDTtxName, &AddSize))  RecFileSize += AddSize;
    }
    RecFileBlocks = CalcBlockSize(RecFileSize);
    BlocksOnePercent = (RecFileBlocks * NrInputFiles) / 100;

    if ((PS = GetPacketSize(fIn, &FileOffset)) || MedionMode)
    {
      if (MedionMode)
      {
        if(PS) MedionMode = 2;
        else { PACKETSIZE = 188; PACKETOFFSET = 0; FileOffset = 0; }
      }

      CurrentPosition = FileOffset;
      PositionOffset += FileOffset;
      if (!OutPacketSize || (FirstTime && DoMerge==1))
        OutPacketSize = PACKETSIZE;
      fseeko64(fIn, CurrentPosition, SEEK_SET);
      printf("  File size: %llu, packet size: %hhu\n", RecFileSize, PACKETSIZE);

      LoadInfFromRec(RecFileIn);

//      if(DoStrip) ContinuityPIDs[0] = (word) -1;
      printf("  PIDs to be checked for continuity: [0] %s%hd%s", (DoStrip ? "[" : ""), ContinuityPIDs[0], (DoStrip ? "]" : ""));
      for (k = 1; k < NrContinuityPIDs; k++)
        printf(", [%d] %hu", k, ContinuityPIDs[k]);
      printf("\n");
    }
    else
    {
      fclose(fIn); fIn = NULL;
      printf("  ERROR: Invalid TS packet size.\n");
      ret = FALSE;
    }
  }
  else
  {
//    printf("  ERROR: Cannot open %s.\n", RecFileIn);
    ret = FALSE;
  }

  if (ret)
  {
    // ggf. inf-File einlesen
    snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
    if(FirstTime) strcpy(InfFileFirstIn, InfFileIn);
    printf("\nInf file: %s\n", InfFileIn);
    InfFileOld[0] = '\0';

    if (LoadInfFile(InfFileIn, FirstTime))
    {
      if (InfDuration)
        BlocksOneSecond = RecFileBlocks / InfDuration;
    }
    else
    {
      fclose(fIn); fIn = NULL;
      ret = FALSE;
    }
  }

  if (ret)
  {
    if (AlreadyStripped)
      printf("  INFO: File has already been stripped.\n");

    // ggf. nav-File �ffnen
    snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);
    printf("\nNav file: %s\n", NavFileIn);
    if (!LoadNavFileIn(NavFileIn))
    {
      printf("  WARNING: Cannot open nav file %s.\n", NavFileIn);
      NavFileIn[0] = '\0';
    }
  
    // ggf. cut-File einlesen
    GetFileNameFromRec(RecFileIn, ".cut", CutFileIn);
    printf("\nCut file: %s\n", CutFileIn);
    if (!CutFileLoad(CutFileIn))
    {
      AddDefaultSegmentMarker();
      CutFileIn[0] = '\0';
      DoCut = 0;
    }
  }

  if (ret && (MedionMode == 1))
  {
    ret = SimpleMuxer_Open(fIn, MDAudName, MDTtxName, MDEpgName);
  }

  if (!FirstTime)
  {
    int i;

    // neu ermittelte Bookmarks kopieren
    for (i = 0; i < (int)BookmarkInfo->NrBookmarks; i++)
      BookmarkInfo_bak->Bookmarks[BookmarkInfo_bak->NrBookmarks++] = BookmarkInfo->Bookmarks[i];

    // neu ermittelte SegmentMarker kopieren
    if (NrSegmentMarker_bak > 2 || NrSegmentMarker > 2 || (SegmentMarker && SegmentMarker[0].pCaption))
    {
      // letzten SegmentMarker der ersten Aufnahme l�schen (wird ersetzt durch Segment 0 der zweiten)
      if (NrSegmentMarker_bak >= 2)
        free(SegmentMarker_bak[--NrSegmentMarker_bak].pCaption);

      // neue SegmentMarker kopieren
      for (i = 0; i < NrSegmentMarker; i++)
        SegmentMarker_bak[NrSegmentMarker_bak++] = SegmentMarker[i];
    }
    else if (NrSegmentMarker_bak >= 2)
    {
      // beide ohne cut-File -> letzten SegmentMarker anpassen
      SegmentMarker_bak[1].Position = RecFileBlocks;
      SegmentMarker_bak[1].Timems = InfDuration * 60000;
    }

    // dirty hack: vorherige Pointer f�r InfBuffer und SegmentMarker wiederherstellen
//    free(InfBuf_tmp); InfBuf_tmp = NULL;
    InfProcessor_Free();
    if (Segments_tmp) free(Segments_tmp); Segments_tmp = NULL;
    InfBuffer = InfBuffer_bak;
    RecHeaderInfo = RecHeaderInfo_bak;
    BookmarkInfo = BookmarkInfo_bak;
    SegmentMarker = SegmentMarker_bak;
    NrSegmentMarker = NrSegmentMarker_bak;
    OrigStartTime = OrigStartTime_bak;
//    InfDuration = InfDuration + InfDuration_bak;  // eigentlich unn�tig
  }

  printf("\n");
  TRACEEXIT;
  return ret;
}

static bool OpenOutputFiles(void)
{
  unsigned long long OutFileSize = 0;
  TRACEENTER;

  // ggf. Output-File �ffnen
  if (*RecFileOut)
  {
    printf("\nOutput rec: %s\n", RecFileOut);
    if (DoMerge == 1)
      HDD_GetFileSize(RecFileOut, &OutFileSize);

    fOut = fopen(RecFileOut, ((DoMerge==1) ? "r+b" : "wb"));
    if (fOut)
    {
      setvbuf(fOut, NULL, _IOFBF, BUFSIZE);
      if (DoMerge == 1)
      {
        if (fseeko64(fOut, (OutFileSize/OutPacketSize)*OutPacketSize, SEEK_SET) != 0)  // angefangene Pakete entfernen
        {
          TRACEEXIT;
          return FALSE;
        }
      }
    }
    else
    {
      TRACEEXIT;
      return FALSE;
    }
  }

  // Output-inf ermitteln
/*
WENN OutFile nicht existiert
  -> inf = in
SONST
  WENN inf existiert oder Rebuildinf
    -> inf = out
  SONST
    -> inf = NULL */

  if (*RecFileOut)
  {
    if (RebuildInf || *InfFileIn)
      snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
    else
      InfFileOut[0] = '\0';
  }
  else
  {
    if (RebuildInf /*|| !*InfFileIn*/)
    {
      RebuildInf = TRUE;
      if(!*InfFileIn) WriteCutInf = TRUE;
      InfFileIn[0] = '\0';
      snprintf(InfFileOld, sizeof(InfFileOld), "%s.inf", RecFileIn);
      snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf_new", RecFileIn);
    }
    else
    {
      InfFileOut[0] = '\0';
    }
  }
  if (*InfFileOut)
    printf("\nInf output: %s\n", InfFileOut);

  // ggf. Output-nav �ffnen
/*
WENN OutFile nicht existiert
  -> nav = in
SONST
  WENN nav existiert oder Rebuildinf
    -> nav = out
  SONST
    -> nav = NULL */

  NavFileOld[0] = '\0';
  if (*RecFileOut)
  {
    if (RebuildNav || *NavFileIn)
    {
      if (DoStrip || RemoveEPGStream || RemoveTeletext || RemoveScrambled || OutPacketSize != PACKETSIZE) RebuildNav = TRUE;
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
    }
    else
      NavFileOut[0] = '\0';
  }
  else
  {
    if (RebuildNav /*|| !*NavFileIn*/)
    {
      RebuildNav = TRUE;
      snprintf(NavFileOld, sizeof(NavFileOld), "%s.nav", RecFileIn);
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav_new", RecFileIn);
    }
    else
    {
      NavFileOut[0] = '\0';
    }
  }

  if (*NavFileOut && LoadNavFileOut(NavFileOut))
    printf("\nNav output: %s\n", NavFileOut);

  // CutFileOut ermitteln
  if (*RecFileOut)
  {
    GetFileNameFromRec(RecFileOut, ".cut", CutFileOut);
    printf("\nCut output: %s\n", CutFileOut);
  }
  else
    CutFileOut[0] = '\0';

  // TeletextOut ermitteln
  if (ExtractTeletext)
  {
    if (*RecFileOut)
      GetFileNameFromRec(RecFileOut, ".srt", TeletextOut);
    else
      GetFileNameFromRec(RecFileIn, ".srt", TeletextOut);
    if (LoadTeletextOut(TeletextOut))
      printf("\nTeletext output: %s", TeletextOut);
  }

  printf("\n");
  TRACEEXIT;
  return TRUE;
}

void CloseInputFiles(bool SetStripFlags)
{
  TRACEENTER;

  if (fIn)
  {
    fclose(fIn); fIn = NULL;
  }
  if (*InfFileIn && SetStripFlags)
  {
    struct stat64 statbuf;
    time_t OldInfTime = 0;
    if (stat64(RecFileIn, &statbuf) == 0)
      OldInfTime = statbuf.st_mtime;
    SetInfStripFlags(InfFileIn, !DoCut, DoStrip && !DoMerge && DoCut!=2);
    if (OldInfTime)
      HDD_SetFileDateTime(InfFileIn, statbuf.st_mtime);
  }
  CloseNavFileIn();
  if(MedionMode == 1) SimpleMuxer_Close();

  TRACEEXIT;
}

bool CloseOutputFiles(void)
{
  TRACEENTER;

  if (!CloseNavFileOut())
    printf("  WARNING: Failed closing the nav file.\n");

  if ((DoCut || DoMerge || RebuildInf) && LastTimems)
    NewDurationMS = LastTimems;
  if ((DoCut || DoStrip || DoMerge) && NrSegmentMarker >= 2)
  {
    SegmentMarker[NrSegmentMarker-1].Position = CurrentPosition - PositionOffset;
    SegmentMarker[NrSegmentMarker-1].Timems -= CutTimeOffset;
    if(NewDurationMS)
      SegmentMarker[NrSegmentMarker-1].Timems = NewDurationMS;
  }

  if(fOut)
  {
    if (PendingBufLen > 0)
      if (!fwrite(PendingBuf, PendingBufLen, 1, fOut))
        printf("  WARNING: Pending buffer could not be written.\n");
    isPending = FALSE;
    PendingBufLen = 0;

    if (/*fflush(fOut) != 0 ||*/ fclose(fOut) != 0)
    {
      printf("  ERROR: Failed closing the output file.\n");
      CutFileSave(CutFileOut);
      SaveInfFile(InfFileOut, (DoMerge!=1) ? InfFileFirstIn : NULL);
      CloseTeletextOut();
      CutProcessor_Free();
      InfProcessor_Free();
      free(PendingBuf); PendingBuf = NULL;
      TRACEEXIT;
      return FALSE;
    }
    fOut = NULL;
  }

  if ((*CutFileOut || (*InfFileOut && WriteCutInf)) && !CutFileSave(CutFileOut))
    printf("  WARNING: Cannot create cut %s.\n", CutFileOut);

  if (*InfFileOut && !SaveInfFile(InfFileOut, (DoMerge!=1) ? InfFileFirstIn : NULL))
    printf("  WARNING: Cannot create inf %s.\n", InfFileOut);

  if (ExtractTeletext && !CloseTeletextOut())
    printf("  WARNING: Cannot create teletext %s.\n", TeletextOut);


  if (*RecFileOut)
    HDD_SetFileDateTime(RecFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec));
  if (*InfFileOut)
    HDD_SetFileDateTime(InfFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec));
  if (*NavFileOut)
    HDD_SetFileDateTime(NavFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec));
  if (*CutFileOut)
    HDD_SetFileDateTime(CutFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec));


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

  TRACEEXIT;
  return TRUE;
}


// ----------------------------------------------
// *****  MAIN FUNCTION  *****
// ----------------------------------------------

int main(int argc, const char* argv[])
{
  byte                  Buffer[192];
  int                   ReadBytes;
  bool                  DropCurPacket;
  time_t                startTime, endTime;
  int                   CurSeg = 0, i = 0, n = 0, k;
  dword                 j = 0;
  dword                 PMTCounter = 0, Percent = 0, BlocksSincePercent = 0;
  bool                  AbortProcess = FALSE;
  bool                  ret = TRUE;

  TRACEENTER;
  #ifndef _WIN32
    setvbuf(stdout, NULL, _IOLBF, 4096);  // zeilenweises Buffering, auch bei Ausgabe in Datei
  #endif
  printf("\nRecStrip for Topfield PVR " VERSION "\n");
  printf("(C) 2016-2020 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- based on MovieCutter 3.6 -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)\n");



/*{
  tPESStream PES;
  FILE *in = fopen("C:/Topfield/TAP/SamplesTMS/MovieCutter/NALU/MedionTest/[2008-01-31] Jetzt red i - Europa_video.pes", "rb");
  FILE *out = fopen("C:/Topfield/TAP/SamplesTMS/MovieCutter/NALU/MedionTest/zDebug.pes", "wb");
  int i = 0;

  PESStream_Open(&PES, in, 500000);

  while (PESStream_GetNextPacket(&PES))
  {
    printf("Packet %d: len=%d, PTS=%u, DTS=%u\n", ++i, PES.curPacketLength, PES.curPacketPTS, PES.curPacketDTS);
    fwrite(PES.Buffer, PES.curPacketLength, 1, out);
  }
  exit(16);
}*/

/*{
  #define DEMUXINFILE "H:/MedionTest/mitStrip/Schnauzen2.ts"
  #define DEMUXOUT    "H:/MedionTest/mitStrip"
  int PIDs[4]        = { 100, 101, 102, 18 };
  int i;

  char* OutFiles[4]  = { DEMUXOUT "/Demux_video.pes", DEMUXOUT "/Demux_audio1.pes", DEMUXOUT "/Demux_ttx.pes", DEMUXOUT "/Demux_epg.pes" };
  bool LastBuffer[4] = { 0, 0, 0, 0 };

  tPSBuffer Streams[4];
  FILE *in = fopen(DEMUXINFILE, "rb");
  FILE *out[4] = { fopen(OutFiles[0], "wb"), fopen(OutFiles[1], "wb"), fopen(OutFiles[2], "wb"), fopen(OutFiles[3], "wb") };
  PACKETSIZE = 192;
  PACKETOFFSET = 4;

  PSBuffer_Init(&Streams[0], PIDs[0], 524288, FALSE);
  PSBuffer_Init(&Streams[1], PIDs[1], 65536, FALSE);
  PSBuffer_Init(&Streams[2], PIDs[2], 32768, FALSE);
  PSBuffer_Init(&Streams[3], PIDs[3], 32768, TRUE);

  while (fread(&Buffer[4-PACKETOFFSET], PACKETSIZE, 1, in))
  {
    if (Buffer[4] == 'G')
    {
      for (i = 0; i < 4; i++)
      {
        PSBuffer_ProcessTSPacket(&Streams[i], (tTSPacket*)(&Buffer[4]));

        if(Streams[i].ValidBuffer != LastBuffer[i])
        {
          byte* pBuffer = NULL;
          if (Streams[i].ValidBuffer == 1) pBuffer = Streams[i].Buffer1;
          else if (Streams[i].ValidBuffer == 2) pBuffer = Streams[i].Buffer2;
          if (pBuffer)
          {
            int pes_packet_length = (Streams[i].TablePacket ? (((pBuffer[1] & 0x03) << 8) | pBuffer[2]) : 6 + ((pBuffer[4] << 8) | pBuffer[5]));
//            if(pes_packet_length <= 6)
              pes_packet_length = Streams[i].ValidBufLen;
            fwrite(pBuffer, pes_packet_length, 1, out[i]);
          }
          LastBuffer[i] = Streams[i].ValidBuffer;
        }
      }
    }
  }

  for (i = 0; i < 4; i++)
  {
    if (Streams[i].BufferPtr)
      fwrite(Streams[i].pBuffer-Streams[i].BufferPtr, 1, Streams[i].BufferPtr, out[i]);
    fclose(out[i]);
    PSBuffer_Reset(&Streams[i]);
  }
  fclose(in);
  exit(17);
}*/


  // Eingabe-Parameter pr�fen
  while ((argc > 1) && (argv && argv[1] && argv[1][0] == '-' && (argv[1][2] == '\0' || argv[1][3] == '\0')))
  {
    switch (argv[1][1])
    {
      case 'n':   RebuildNav = TRUE;      break;
      case 'i':   RebuildInf = TRUE;      break;
      case 'r':   if(!DoCut) DoCut = 1;   break;
      case 'c':   DoCut = 2;              break;
      case 'a':   DoMerge = 1;            break;
      case 'm':   DoMerge = 2;            break;
      case 's':   DoStrip = TRUE;
                  DoSkip = (argv[1][2] == 's'); break;
      case 'e':   RemoveEPGStream = TRUE; break;
      case 't':   if(argv[1][2] == 't') ExtractTeletext = TRUE;
                  else RemoveTeletext = TRUE;
                  break;
      case 'x':   RemoveScrambled = TRUE; break;
      case 'o':   OutPacketSize   = (argv[1][2] == '2') ? 188 : 192; break;
      case 'M':   MedionMode = TRUE;      break;
      case 'v':   DoInfoOnly = TRUE;      break;
      default:    printf("\nUnknown argument: -%c\n", argv[1][1]);
                  ret = FALSE;
    }
    argv[1] = argv[0];
    argv++;
    argc--;
  }
  printf("\nParameters:\nDoCut=%d, DoMerge=%d, DoStrip=%s, RmEPG=%s, RmTxt=%s, RmScr=%s, RbldNav=%s, RbldInf=%s, PkSize=%hhu\n", DoCut, DoMerge, (DoStrip ? "yes" : "no"), (RemoveEPGStream ? "yes" : "no"), (RemoveTeletext ? "yes" : "no"), (RemoveScrambled ? "yes" : "no"), (RebuildNav ? "yes" : "no"), (RebuildInf ? "yes" : "no"), OutPacketSize);

  // Eingabe-Dateinamen lesen
  if (argc > 1)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    RecFileIn[sizeof(RecFileIn)-1] = '\0';
    argv[1] = argv[0];
    argv++;
    argc--;
  }
  else RecFileIn[0] = '\0';
  if (argc > 1)
  {
    strncpy(RecFileOut, argv[1], sizeof(RecFileOut));
    RecFileOut[sizeof(RecFileOut)-1] = '\0';
    argv[1] = argv[0];
    argv++;
    argc--;
  }
  else RecFileOut[0] = '\0';
  if (DoCut == 2)
  {
//    const char *p = strrchr(RecFileOut, PATH_SEPARATOR);  // ggf. Dateinamen entfernen
//    memset(OutDir, 0, sizeof(OutDir));
//    strncpy(OutDir, RecFileOut, min((p) ? (size_t)(p - RecFileOut) : strlen(RecFileOut), sizeof(OutDir)-1));
    if (HDD_DirExist(RecFileOut))
      strncpy(OutDir, RecFileOut, sizeof(OutDir));
    else
      OutDir[0] = '\0';
    GetNextFreeCutName(RecFileIn, RecFileOut, OutDir);
  }
  else OutDir[0] = '\0';

  if (!*RecFileIn)
    { printf("\nNo input file specified!\n");  ret = FALSE; }
  else if ((DoCut || DoStrip || DoMerge || OutPacketSize) && (!*RecFileOut || strcmp(RecFileIn, RecFileOut)==0))
    { printf("\nNo output file specified or output same as input!\n");  ret = FALSE; }
  else if (DoMerge && DoCut==2)
    { printf("\nMerging cannot be used together with cut mode (single segment copy)!\n");  ret = FALSE; }
  else if (DoMerge==1 && OutPacketSize)
    { printf("\nPacketSize cannot be changed when appending to an existing recording!\n");  ret = FALSE; }
  else if (DoSkip && (DoCut || DoMerge))
    { printf("\nSkipping of stripped recordings cannot be combined with -r, -c, -a, -m!\n");  DoSkip = FALSE; }
  if (DoInfoOnly && (DoStrip || DoCut || DoMerge || RebuildNav || RebuildInf || ExtractTeletext))
    { printf("\nView info only (-v) disables any other option!\n"); }
  if (ExtractTeletext && DoStrip)
    { RemoveTeletext = TRUE; }
  if (MedionMode==1 && DoStrip)
    { MedionStrip = TRUE; DoStrip = FALSE; }

  if (!ret)
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
    printf("  -r:        Cut the recording according to cut-file. (if OutFile specified)\n"
           "             Copies only the selected segments into the new rec.\n\n");
    printf("  -c:        Copies each selected segment into a single new rec-file.\n"
           "             The output files will be auto-named. OutFile is ignored.\n"
           "             (instead of OutFile an output folder may be specified.)\n\n");
    printf("  -a:        Append second, ... file to the first file. (file1 gets modified!)\n"
           "             If combined with -r, only the selected segments are appended.\n"
           "             If combined with -s, only the copied part will be stripped.\n\n");
    printf("  -m:        Merge file2, file3, ... into a new file1. (file1 is created!)\n"
           "             If combined with -s, all input files will be stripped.\n\n");
    printf("  -s:        Strip the recording. (if OutFile specified)\n"
           "             Removes unneeded filler packets. May be combined with -c, -r, -a.\n\n");
    printf("  -ss:       Strip and skip. Same as -s, but skips already stripped files.\n\n");
    printf("  -e:        Remove also the EPG data. (can be combined with -s)\n\n");
    printf("  -t:        Remove also the teletext data. (can be combined with -s)\n");
    printf("  -tt:       Extract subtitles from teletext. (together with -s: also remove)\n\n");
    printf("  -x:        Remove packets marked as scrambled. (flag could be wrong!)\n\n");
    printf("  -o1/-o2:   Change the packet size for output-rec: \n"
           "             1: PacketSize = 192 Bytes, 2: PacketSize = 188 Bytes.\n\n");
    printf("  -v:        View rec information only. Disables any other option.\n\n");
    printf("  -M:        Medion Mode: Multiplexes 4 separate PES-Files into output.\n");
    printf("             (With InFile=<name>_video.pes, _audio1, _ttx, _epg are used.)\n");
    printf("\nExamples:\n---------\n");
    printf("  RecStrip 'RecFile.rec'                     RebuildNav.\n\n");
    printf("  RecStrip -s -e InFile.rec OutFile.rec      Strip recording.\n\n");
    printf("  RecStrip -n -i -o1 InFile.ts OutFile.rec   Convert TS to Topfield rec.\n\n");
    printf("  RecStrip -r -s -e -o2 InRec.rec OutMpg.ts  Strip & cut rec and convert to TS.\n");
    TRACEEXIT;
    exit(1);
  }

  // Buffer und Sub-Klassen initialisieren
  if (!InfProcessor_Init() || !CutProcessor_Init())
  {
    printf("ERROR: Initialization failed.\n");
    CutProcessor_Free();
    InfProcessor_Free();
    TRACEEXIT;
    exit(2);
  }
  NavProcessor_Init();
  TtxProcessor_Init(TeletextPage);

  // Pending Buffer initialisieren
  if (DoStrip)
  {
    NALUDump_Init();
    PendingBuf = (byte*) malloc(PENDINGBUFSIZE);
    if (!PendingBuf)
    {
      printf("ERROR: Memory allocation failed.\n");
      CutProcessor_Free();
      InfProcessor_Free();
      TRACEEXIT;
      exit(2);
    }
  }

  // Variablen initialisieren
  for (k = 0; k < MAXCONTINUITYPIDS; k++)
  {
    ContinuityPIDs[k] = (word) -1;
    ContinuityCtrs[k] = -1;
  }

  if (DoMerge)
  {
    char Temp[FBLIB_DIR_SIZE];
    strcpy(Temp, RecFileIn);
    strcpy(RecFileIn, RecFileOut);
    strcpy(RecFileOut, Temp);
    NrInputFiles = argc;
  }

  // Pr�fen, ob Aufnahme bereits gestrippt
  if (DoSkip && !DoMerge)
  {
    AlreadyStripped = FALSE;
    snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
    if (GetInfStripFlags(InfFileIn, &AlreadyStripped, NULL) && AlreadyStripped)
    {
      printf("\nInput File: %s\n", RecFileIn);
      printf("--> already stripped.\n");
      CutProcessor_Free();
      InfProcessor_Free();
      free(PendingBuf); PendingBuf = NULL;
      printf("\nRecStrip finished. No files to process.\n");
      TRACEEXIT;
      exit(0);
    }
  }

  // Wenn Appending, dann erstmal Output als Input einlesen
  if (DoMerge == 1)
  {
    if (!OpenInputFiles(RecFileOut, TRUE))
    {
      CutProcessor_Free();
      InfProcessor_Free();
      free(PendingBuf); PendingBuf = NULL;
      printf("ERROR: Cannot read output %s.\n", RecFileOut);
      TRACEEXIT;
      exit(3);
    }
    GoToEndOfNav(fNavIn);
    i = CurSeg = NrSegmentMarker - 1;
    j = BookmarkInfo->NrBookmarks;
    PositionOffset = -(long long)((RecFileSize/OutPacketSize)*OutPacketSize);
    if (NrSegmentMarker >= 2)
      CutTimeOffset = -(int)SegmentMarker[NrSegmentMarker-1].Timems;
//    else
//      CutTimeOffset = -(int)InfDuration;
    if ((int)LastTimems > -CutTimeOffset)
      CutTimeOffset = -(int)LastTimems;
    if(ExtractTeletext) last_timestamp = -CutTimeOffset;
    CloseInputFiles(FALSE);
  }

  // Input-Files �ffnen
  if (!OpenInputFiles(RecFileIn, (DoMerge != 1)))
  {
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;
    printf("ERROR: Cannot open input %s.\n", RecFileIn);
    TRACEEXIT;
    exit(4);
  }

  if (!VideoPID || VideoPID == (word)-1)
  {
    printf("Warning: No video PID determined.\n");
/*    CloseInputFiles(FALSE);
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    TRACEEXIT;
    exit(6);  */
  }
  if (ExtractTeletext && !MedionMode && TeletextPID == (word)-1)
  {
    printf("Warning: No teletext PID determined.\n");
    ExtractTeletext = FALSE;
  }

  // Hier beenden, wenn View Info Only
  if (DoInfoOnly)
  {
    fclose(fIn); fIn = NULL;
    CloseNavFileIn();
    if(MedionMode == 1) SimpleMuxer_Close();
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;
    printf("\nRecStrip finished. View information only.\n");
    TRACEEXIT;
    exit(0);
  }

  // Output-Files �ffnen
  if (DoCut < 2 && !OpenOutputFiles())
  {
    fclose(fIn); fIn = NULL;
    CloseNavFileIn();
    if(MedionMode == 1) SimpleMuxer_Close();
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    printf("ERROR: Cannot write output %s.\n", RecFileOut);
    TRACEEXIT;
    exit(7);
  }

  // Wenn Appending, ans Ende der nav-Datei springen
  if(DoMerge == 1) GoToEndOfNav(NULL);

  // Spezialanpassung Humax / Medion
  if ((HumaxSource || MedionMode==1) && fOut /*&& DoMerge != 1*/)
  {
    printf("  Generate new PAT/PMT for Humax/Medion recording.\n");
    if (!HumaxSource)
      GeneratePatPmt(PATPMTBuf, ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.ServiceID, 0x100, VideoPID, 101, TeletextPID, STREAM_VIDEO_MPEG2, STREAM_AUDIO_MPEG2);

    if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;
    if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;

    if (MedionMode == 1)
    {
      ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.PMTPID = 0x100;
      printf("  TS: PMTPID=%hu", 0x100);
      AnalysePMT(&PATPMTBuf[201], (TYPE_RecHeader_TMSS*)InfBuffer);
    }
    NrContinuityPIDs = 0;
  }

  // Spezialanpassung Medion (Teletext-Extraktion)
/*  if (MedionMode)
  {
    FILE               *fMDIn = NULL;
    byte               *MDBuffer = NULL;

    if (MDBuffer = (byte*) malloc(32768))
    {
      if (fMDIn = fopen(MDTtxName, "rb"))
      {
        int p = 0, pes_packet_length;

        if (fread(MDBuffer, 1, 32768, fMDIn) > 0)
        {
          while ((p < 32760) && (MDBuffer[p] != 0 || MDBuffer[p+1] != 0 || MDBuffer[p+2] != 1))
            p++;
          fseek(fMDIn, p+6, SEEK_SET);
          memmove(MDBuffer, &MDBuffer[p], 6);
          pes_packet_length = 6 + ((MDBuffer[4] << 8) | MDBuffer[5]);

          while ((MDBuffer[0] == 0 && MDBuffer[1] == 0 && MDBuffer[2] == 1) && (fread(&MDBuffer[6], 1, pes_packet_length, fMDIn) == pes_packet_length))
          {
            process_pes_packet(MDBuffer, pes_packet_length);
            memcpy(MDBuffer, &MDBuffer[pes_packet_length], 6);
            pes_packet_length = 6 + ((MDBuffer[4] << 8) | MDBuffer[5]);
          }
        }
        fclose(fMDIn);
        CloseTeletextOut();
      }
    }
  } */

  if(!RebuildNav && *RecFileOut && *NavFileIn)  SetFirstPacketAfterBreak();


  // -----------------------------------------------
  // Datei paketweise einlesen und verarbeiten
  // -----------------------------------------------
  printf("\n");
  time(&startTime);
  memset(Buffer, 0, sizeof(Buffer));

  for (curInputFile = 0; curInputFile < NrInputFiles; curInputFile++)
  {
/*    if (BookmarkInfo)
    {
      // Bookmarks kurz vor der Schnittstelle l�schen
      while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))
        DeleteBookmark(--j);

      // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) l�schen
      while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CalcBlockSize(CurrentPosition) + 3*BlocksOneSecond))
        DeleteBookmark(j);

      // neues Bookmark an Schnittstelle setzen
      if (DoCut == 1 || DoMerge)
        if (CurrentPosition-PositionOffset > 0)
          AddBookmark(j++, CalcBlockSize(CurrentPosition-PositionOffset + 9023));
    } */

    while (fIn)
    {
      // SCHNEIDEN
      if (((DoCut && NrSegmentMarker > 2) || DoMerge) && (CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position))
      {
        // Wir sind am Sprung zu einem neuen Segment CurSeg angekommen

        // TEILE KOPIEREN: Output-Files schlie�en
        if (fOut && DoCut == 2)
        {
          int NrSegmentMarker_bak = NrSegmentMarker;
          tSegmentMarker2 LastSegmentMarker_bak = SegmentMarker[1];
          TYPE_Bookmark_Info BookmarkInfo_bak;

          if (BookmarkInfo)
            memcpy(&BookmarkInfo_bak, BookmarkInfo, sizeof(TYPE_Bookmark_Info));

          // SegmentMarker auf Anfang und Ende setzen und Bookmarks bis CurSeg ausgeben
          NrSegmentMarker = 2;
          SegmentMarker[0].Position = 0;
          SegmentMarker[1].Percent = 100;
          SegmentMarker[1].Selected = FALSE;
          ActiveSegment = 0;
          if (BookmarkInfo)
          {
            BookmarkInfo->NrBookmarks = j;
            if(!ResumeSet) BookmarkInfo->Resume = 0;
          }

          // aktuelle Output-Files schlie�en
          if (!CloseOutputFiles())
            exit(10);

          NrPackets += (SegmentMarker[1].Position / PACKETSIZE);
          NrSegmentMarker = NrSegmentMarker_bak;
          SegmentMarker[1] = LastSegmentMarker_bak;
          if (BookmarkInfo)
          {
            memcpy(BookmarkInfo, &BookmarkInfo_bak, sizeof(TYPE_Bookmark_Info));  // (Resume wird auch zur�ckkopiert)
            if(ResumeSet) BookmarkInfo->Resume = 0;
          }
        }

        // SEGMENT �BERSPRINGEN (wenn nicht-markiert)
        while ((DoCut && NrSegmentMarker > 2) && (CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position) && !SegmentMarker[CurSeg].Selected)
        {
          if (OutCutVersion >= 4)
            printf("[Segment %d]  -%12llu %12lld-%-12lld %s\n", ++n, CurrentPosition, SegmentMarker[CurSeg].Position, SegmentMarker[CurSeg+1].Position, SegmentMarker[CurSeg].pCaption);
          else
            printf("[Segment %d]  -%12llu %10u-%-10u %s\n",     ++n, CurrentPosition, CalcBlockSize(SegmentMarker[CurSeg].Position), CalcBlockSize(SegmentMarker[CurSeg+1].Position), SegmentMarker[CurSeg].pCaption);
          CutTimeOffset += SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems;
          DeleteSegmentMarker(CurSeg, TRUE);
          NrSegments++;

          if (CurSeg < NrSegmentMarker-1)
          {
            long long SkippedBytes = (((SegmentMarker[CurSeg].Position) /* / PACKETSIZE) * PACKETSIZE */) ) - CurrentPosition;
            fseeko64(fIn, ((SegmentMarker[CurSeg].Position) /* / PACKETSIZE) * PACKETSIZE */), SEEK_SET);
            SetFirstPacketAfterBreak();
            SetTeletextBreak(FALSE, TeletextPage);
            for (k = 0; k < NrContinuityPIDs; k++)
              ContinuityCtrs[k] = -1;
            if(DoStrip)  NALUDump_Init();  // NoContinuityCheck = TRUE;

            // Position neu berechnen
            PositionOffset += SkippedBytes;
            CurrentPosition += SkippedBytes;
            CurPosBlocks = CalcBlockSize(CurrentPosition);
            Percent = (100 * curInputFile / NrInputFiles) + ((100 * CurPosBlocks) / (RecFileBlocks * NrInputFiles));
            CurBlockBytes = 0;
            BlocksSincePercent = 0;
          }
          else
          {
            // Bookmarks im verworfenen Nachlauf verwerfen
            while (BookmarkInfo && (j < BookmarkInfo->NrBookmarks))
              DeleteBookmark(j);
            break;
          }
        }

        // Wir sind am n�chsten (zu erhaltenden) SegmentMarker angekommen
        if (CurSeg < NrSegmentMarker-1)
        {
          if (OutCutVersion >= 4)
            printf("[Segment %d]  *%12llu %12lld-%-12lld %s\n", ++n, CurrentPosition, SegmentMarker[CurSeg].Position, SegmentMarker[CurSeg+1].Position, SegmentMarker[CurSeg].pCaption);
          else
            printf("[Segment %d]  *%12llu %10u-%-10u %s\n",     ++n, CurrentPosition, CalcBlockSize(SegmentMarker[CurSeg].Position), CalcBlockSize(SegmentMarker[CurSeg+1].Position), SegmentMarker[CurSeg].pCaption);
          SegmentMarker[CurSeg].Selected = FALSE;
          SegmentMarker[CurSeg].Percent = 0;

          if (BookmarkInfo && (DoCut || (DoMerge && CurrentPosition == 0)))
          {
            // Bookmarks kurz vor der Schnittstelle l�schen
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))  // CurPos - SkippedBytes ?
              DeleteBookmark(--j);

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) l�schen
//            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CalcBlockSize(CurrentPosition) + 3*BlocksOneSecond))
            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CurPosBlocks + 3*BlocksOneSecond))
              DeleteBookmark(j);

            // neues Bookmark an Schnittstelle setzen
            if (DoCut == 1 || (DoMerge && CurrentPosition == 0))
//              if ((CurrentPosition-PositionOffset > 0) && (CurrentPosition + 3*9024*BlocksOneSecond < (long long)RecFileSize))
              if ((CurrentPosition-PositionOffset > 0) && (CurPosBlocks + 3*BlocksOneSecond < RecFileBlocks))
                AddBookmark(j++, CalcBlockSize(CurrentPosition-PositionOffset + 9023));
          }

          if (DoCut == 1)
          {
            // SCHNEIDEN: Zeit neu berechnen
            if (NewStartTimeOffset == 0)
              NewStartTimeOffset = max(SegmentMarker[CurSeg].Timems, 1);
            NewDurationMS += (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);
          }
          else if (DoCut == 2)
          {
            // TEILE KOPIEREN
            // bisher ausgegebene SegmentMarker / Bookmarks l�schen
            while (CurSeg > 0)
            {
              DeleteSegmentMarker(--CurSeg, TRUE);
              i--;
            }
            while (BookmarkInfo && (j > 0))
              DeleteBookmark(--j);

            // neue Output-Files �ffnen
            GetNextFreeCutName(RecFileIn, RecFileOut, OutDir);
            if (!OpenOutputFiles())
            {
              fclose(fIn); fIn = NULL;
              CloseNavFileIn();
              CloseTeletextOut();
              if(MedionMode == 1) SimpleMuxer_Close();
              CutProcessor_Free();
              InfProcessor_Free();
              free(PendingBuf); PendingBuf = NULL;      
              printf("ERROR: Cannot create %s.\n", RecFileOut);
              TRACEEXIT;
              exit(7);
            }
            NavProcessor_Init();
            if(!RebuildNav && *RecFileOut && *NavFileIn)  SetFirstPacketAfterBreak();
            if(DoStrip) NALUDump_Init();
            LastTimems = 0;
            LastPCR = 0;
            LastTimeStamp = 0;
            dbg_DelBytesSinceLastVid = 0;

            // Caption in inf schreiben
            SetInfEventText(SegmentMarker[CurSeg].pCaption);

            // Positionen anpassen
            PositionOffset = CurrentPosition;
            CutTimeOffset = SegmentMarker[CurSeg].Timems;
            NewStartTimeOffset = SegmentMarker[CurSeg].Timems;
            NewDurationMS = (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);
          }
          NrCopiedSegments++;
          NrSegments++;
        }

        CurSeg++;
        if (CurSeg >= NrSegmentMarker)
          break;
      }


      // PACKET EINLESEN
      if (MedionMode == 1)
      {
        if (fOut && !MedionStrip && (PMTCounter >= 4998))
        {
          // Wiederhole PAT/PMT und EIT Information alle 5000 Pakete (verzichte darauf, wenn MedionStrip aktiv)
          ((tTSPacket*) &PATPMTBuf[4])->ContinuityCount++;
          ((tTSPacket*) &PATPMTBuf[196])->ContinuityCount++;
          if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
            PositionOffset -= OutPacketSize;
          if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
            PositionOffset -= OutPacketSize;
          SimpleMuxer_DoEITOutput();
          PMTCounter = 0;
        }
        ReadBytes = (SimpleMuxer_NextTSPacket((tTSPacket*) &Buffer[4])) ? PACKETSIZE : 0;
      }
      else
        ReadBytes = fread(&Buffer[4-PACKETOFFSET], 1, PACKETSIZE, fIn);

      if (ReadBytes == PACKETSIZE)
      {
        if (Buffer[4] == 'G')
        {
          word CurPID = TsGetPID((tTSPacket*) &Buffer[4]);
          signed char *CurCtr = NULL;

          if (!DoStrip || CurPID != VideoPID)
            for (k = DoStrip; k < NrContinuityPIDs; k++)
              if(CurPID == ContinuityPIDs[k])
              {
                CurCtr = &ContinuityCtrs[k];  break;
              }

          // nur Continuity Check
          if (CurCtr)
          {
            if (*CurCtr >= 0)
            {
              if (((tTSPacket*) &Buffer[4])->Payload_Exists) *CurCtr = (*CurCtr + 1) % 16;
              if (((tTSPacket*) &Buffer[4])->ContinuityCount != *CurCtr)
              {
//              if (!DoCut || (i < NrSegmentMarker && CurrentPosition != SegmentMarker[i].Position))
                  printf("TS check: TS continuity mismatch (PID=%hu, pos=%lld, expect=%hhu, found=%hhu, missing=%hhd)\n", CurPID, CurrentPosition, *CurCtr, ((tTSPacket*) &Buffer[4])->ContinuityCount, (((tTSPacket*) &Buffer[4])->ContinuityCount + 16 - *CurCtr) % 16);
                if (CurPID == VideoPID)
                {
                  SetFirstPacketAfterBreak();
//                  SetTeletextBreak(FALSE);
                }
                *CurCtr = ((tTSPacket*) &Buffer[4])->ContinuityCount;
              }
            }
            else
              *CurCtr = ((tTSPacket*) &Buffer[4])->ContinuityCount;
          }

          DropCurPacket = FALSE;

          // auf verschl�sselte Pakete pr�fen
          if (((tTSPacket*) &Buffer[4])->Scrambling_Ctrl >= 2)
          {
            CurScrambledPackets++;
            if (CurScrambledPackets <= 100)
            {              
              if (((tTSPacket*) &Buffer[4])->Payload_Exists)
                printf("WARNING [%s]: Scrambled packet bit at pos %lld, PID %u -> %s\n", ((((tTSPacket*) &Buffer[4])->Adapt_Field_Exists && Buffer[8] >= 182) ? "ok" : "!!"), CurrentPosition, CurPID, (RemoveScrambled ? "packet removed" : "ignored"));
              if (CurScrambledPackets == 100)             
              printf("There where scrambled packets. No more scrambled warnings will be shown...\n");
            }
            if ((CurScrambledPackets > CurPosBlocks) && (CurPosBlocks >= 100))  // ~ 2% der Pakete
            {
              SetInfCryptFlag(InfFileIn);
              printf("ERROR: Too many scrambled packets: %lld -> aborted.\n", CurScrambledPackets);
              AbortProcess = TRUE;
            }

            // entfernen, wenn nav neu berechnet wird
            if (RemoveScrambled  /*&& (RebuildNav || !*NavFileOut)*/)
            {
              DropCurPacket = TRUE;
              if(DoStrip && (CurPID == VideoPID) && ((tTSPacket*) &Buffer[4])->Payload_Exists)
                if (LastContinuityInput >= 0) LastContinuityInput = (LastContinuityInput + 1) % 16;
            }
          }

          if (CurPID == TeletextPID)
          {
            // Extract Teletext Subtitles
            if (/*ExtractTeletext &&*/ fTtxOut)
            {
              dword CurPCR = 0;
              if (GetPCRms(&Buffer[4], &CurPCR))  global_timestamp = CurPCR;
              ProcessTtxPacket((tTSPacket*) &Buffer[4]);
            }
            // Remove Teletext packets
            if (RemoveTeletext)
            {
              NrDroppedTxtPid++;
              DropCurPacket = TRUE;
            }
          }

          // Remove EPG stream
          else if (RemoveEPGStream && (CurPID == 0x12))
          {
            NrDroppedEPGPid++;
            DropCurPacket = TRUE;
          }

          // STRIPPEN
          else if (DoStrip && !DropCurPacket)
          {
            // Remove Null Packets
            if (CurPID == 0x1FFF)
            {
              NrDroppedNullPid++;
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
//                  break;  (fall-through)

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
//          ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
          while ((i < NrSegmentMarker) && (CurrentPosition >= SegmentMarker[i].Position))
          {
            SegmentMarker[i].Position -= PositionOffset;
            if (i > 0 && !SegmentMarker[i].Timems)
              pOutNextTimeStamp = &SegmentMarker[i].Timems;
            SegmentMarker[i].Timems -= CutTimeOffset;
            i++;
          }

          // BOOKMARKS ANPASSEN
//          ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
          if (BookmarkInfo)
          {
            while ((j < min(BookmarkInfo->NrBookmarks, 48)) && (CurPosBlocks >= BookmarkInfo->Bookmarks[j]))
            {
              if ((DoCut == 2) && (BookmarkInfo->Bookmarks[j] + 3*BlocksOneSecond >= CalcBlockSize(SegmentMarker[i].Position)))
                DeleteBookmark(j);
              else
              {
                BookmarkInfo->Bookmarks[j] -= (dword)(PositionOffset / 9024);  // CalcBlockSize(PositionOffset)
                j++;
              }
            }

            if (!ResumeSet && (DoMerge != 1) && (CurPosBlocks >= BookmarkInfo->Resume))
            {
              if (PositionOffset / 9024 <= BookmarkInfo->Resume)
                BookmarkInfo->Resume -= (dword)(PositionOffset / 9024);  // CalcBlockSize(PositionOffset)
              else
                BookmarkInfo->Resume = 0;
              ResumeSet = TRUE;
            }
          }

          // PCR berechnen
          if (OutPacketSize > PACKETSIZE)  // OutPacketSize==192 and PACKETSIZE==188
          {
            long long CurPCRfull = 0;
            
            if (GetPCR(&Buffer[4], &CurPCRfull))
            {
              dword CurPCR = (CurPCRfull & 0xffffffff);
              
              if (LastPCR && CurPCR > LastPCR)
              {
                if (MedionMode == 1)
                  CurTimeStep = (dword)(CurPCR - LastPCR) / ((PESVideo.curPacketLength+8+183) / 184);
//                else
//                  CurTimeStep = (dword)(CurPCR - LastPCR) / ((CurrentPosition-PosLastPCR) / PACKETSIZE);
              }
              else
                CurTimeStep = 1200;
              LastPCR = CurPCR;
              LastTimeStamp = CurPCR;
              GetPCRms(&Buffer[4], &global_timestamp);  // oder global_timestamp = (CurPCRfull / 27000)
            }
            else if (CurPID == VideoPID && LastTimeStamp)
              LastTimeStamp += CurTimeStep;
            Buffer[0] = (((byte*)&LastTimeStamp)[3] & 0x3f);
            Buffer[1] = ((byte*)&LastTimeStamp)[2];
            Buffer[2] = ((byte*)&LastTimeStamp)[1];
            Buffer[3] = ((byte*)&LastTimeStamp)[0];
          }
          else if (ExtractTeletext)
          {
            GetPCRms(&Buffer[4], &global_timestamp);
          }

          // NAV BERECHNEN UND PAKET AUSGEBEN
          if (!DropCurPacket)
          {
            // NAV NEU BERECHNEN
            if (PACKETSIZE > OutPacketSize)       PositionOffset += 4;  // Reduktion auf 188 Byte Packets
            else if (PACKETSIZE < OutPacketSize)  PositionOffset -= 4;

            // nav-Eintrag korrigieren und ausgeben, wenn Position < CurrentPosition ist (um PositionOffset reduzieren)
            if (RebuildNav)
            {
              if (CurPID == VideoPID)
              {
                ProcessNavFile((tTSPacket*) &Buffer[4], CurrentPosition + PACKETOFFSET, PositionOffset);
                dbg_DelBytesSinceLastVid = 0;
              }
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
                AbortProcess = TRUE;
              }
            }
          }
          else
          {
            // Paket wird entfernt
            if(fOut) PositionOffset += ReadBytes;

            if (RebuildNav && (CurPID != VideoPID))
              dbg_DelBytesSinceLastVid += ReadBytes;
          }

          CurrentPosition += ReadBytes;
          CurBlockBytes += ReadBytes;
          if(MedionMode==1) PMTCounter++;
        }

        // KEIN PAKET-SYNCBYTE GEFUNDEN
        else if (MedionMode != 1)
        {
          if (HumaxSource && (*((dword*)&Buffer[4]) == HumaxHeaderAnfang) && ((unsigned int)CurrentPosition % HumaxHeaderIntervall == HumaxHeaderIntervall-HumaxHeaderLaenge))
          {
            fseeko64(fIn, +HumaxHeaderLaenge-ReadBytes, SEEK_CUR);
            PositionOffset += HumaxHeaderLaenge;
            CurrentPosition += HumaxHeaderLaenge;
            CurBlockBytes += HumaxHeaderLaenge;
            PMTCounter++;
            if (fOut && !DoStrip && (PMTCounter >= 29))
            {
              ((tTSPacket*) &PATPMTBuf[4])->ContinuityCount++;
              ((tTSPacket*) &PATPMTBuf[196])->ContinuityCount++;
              if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
                PositionOffset -= OutPacketSize;
              if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
                PositionOffset -= OutPacketSize;
              PMTCounter = 0;
            }
          }
          else if ((unsigned long long) CurrentPosition + 4096 >= RecFileSize)
          {
            printf("INFO: Incomplete TS - Ignoring last %lld bytes.\n", RecFileSize - CurrentPosition);
            fclose(fIn); fIn = NULL;
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
              if(RebuildNav) dbg_DelBytesSinceLastVid += Offset;
              NrIgnoredPackets++;
            }
            else
            {
              printf("ERROR: Incorrect TS - No sync bytes after position %lld -> aborted.\n", CurrentPosition);
              NrIgnoredPackets = 0x0fffffffffffffffLL;
            }

            if (NrIgnoredPackets >= 10)
            {
              if (NrIgnoredPackets < 0x0fffffffffffffffLL)
                printf("ERROR: Too many ignored packets: %lld -> aborted.\n", NrIgnoredPackets);
              AbortProcess = TRUE;
            }
          }
        }

        if (AbortProcess)
        {
          fclose(fIn); fIn = NULL;
          if (fOut) fclose(fOut); fOut = NULL;
          CloseNavFileIn();
          CloseNavFileOut();
          if (ret)
          {
            CutFileSave(CutFileOut);
            SaveInfFile(InfFileOut, (DoMerge!=1) ? InfFileFirstIn : NULL);
          }
          CloseTeletextOut();
          if(MedionMode == 1) SimpleMuxer_Close();
          CutProcessor_Free();
          InfProcessor_Free();
          free(PendingBuf);
          TRACEEXIT;
          exit(8);
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
//    NrPackets += ((RecFileSize + PACKETSIZE-1) / PACKETSIZE);
    NrScrambledPackets += CurScrambledPackets;

    if (DoMerge && (curInputFile < NrInputFiles-1))
    {
      CloseInputFiles(TRUE);

      // n�chstes Input-File aus Parameter-String ermitteln
      strncpy(RecFileIn, argv[curInputFile+1], sizeof(RecFileIn));
      RecFileIn[sizeof(RecFileIn)-1] = '\0';

      PositionOffset -= CurrentPosition;
      if (NrSegmentMarker >= 2)
        CutTimeOffset -= (int)SegmentMarker[NrSegmentMarker-1].Timems;
//      else
//        CutTimeOffset -= (int)InfDuration;
      if (-(int)LastTimems < CutTimeOffset)
        CutTimeOffset = -(int)LastTimems;
      SetFirstPacketAfterBreak();
      SetTeletextBreak(TRUE, TeletextPage);
      for (k = 0; k < NrContinuityPIDs; k++)
        ContinuityCtrs[k] = -1;
      if(DoStrip)  NALUDump_Init();  // NoContinuityCheck = TRUE;

      if (!OpenInputFiles(RecFileIn, FALSE))
      {
        CloseOutputFiles();
        CutProcessor_Free();
        InfProcessor_Free();
        free(PendingBuf); PendingBuf = NULL;
        printf("ERROR: Cannot open input %s.\n", RecFileIn);
        TRACEEXIT;
        exit(5);
      }

      CurPosBlocks = 0;
      CurBlockBytes = 0;
      BlocksSincePercent = 0;
      CurScrambledPackets = 0;
      n = 0;
    }
  }
  printf("\n");

  if ((fOut || (DoCut != 2)) && !CloseOutputFiles())
    exit(10);

  CloseInputFiles(TRUE);

  CutProcessor_Free();
  InfProcessor_Free();
  free(PendingBuf); PendingBuf = NULL;

  if (NrCopiedSegments > 0)
    printf("\nSegments: %d of %d segments copied.\n", NrCopiedSegments, NrSegments);
  if (MedionMode == 1)
    NrDroppedZeroStuffing = NrDroppedZeroStuffing / 184;

  {
    long long NrDroppedAll = NrDroppedFillerNALU + NrDroppedZeroStuffing + NrDroppedAdaptation + NrDroppedNullPid + NrDroppedEPGPid + NrDroppedTxtPid + (RemoveScrambled ? NrScrambledPackets : 0);
    if(DoCut!=2) NrPackets = (CurrentPosition-PositionOffset) / OutPacketSize;
    NrPackets += NrDroppedAll;
    if (NrPackets > 0)
      printf("\nPackets: %lld, FillerNALUs: %lld (%lld%%), ZeroByteStuffing: %lld (%lld%%), AdaptationFields: %lld (%lld%%), NullPackets: %lld (%lld%%), EPG: %lld (%lld%%), Teletext: %lld (%lld%%), Scrambled: %lld (%lld%%), Dropped (all): %lld (%lld%%)\n", NrPackets, NrDroppedFillerNALU, NrDroppedFillerNALU*100/NrPackets, NrDroppedZeroStuffing, NrDroppedZeroStuffing*100/NrPackets, NrDroppedAdaptation, NrDroppedAdaptation*100/NrPackets, NrDroppedNullPid, NrDroppedNullPid*100/NrPackets, NrDroppedEPGPid, NrDroppedEPGPid*100/NrPackets, NrDroppedTxtPid, NrDroppedTxtPid*100/NrPackets, NrScrambledPackets, NrScrambledPackets*100/NrPackets, NrDroppedAll, NrDroppedAll*100/NrPackets);
    else
      printf("\n0 Packets!\n");
  }

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  #ifdef _WIN32
//    getchar();
  #endif
  TRACEEXIT;
  exit(0);
}
