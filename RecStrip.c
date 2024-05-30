/*
  RecStrip for Topfield PVR
  (C) 2016-2023 Christian Wünsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan Pöschel)
  Concepts from telxcc (Forers)
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
  #include <windows.h>
#else
  #include <unistd.h>
  #include <utime.h>
  #include <dirent.h>
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
#include "SrtProcessor.h"
#include "RebuildInf.h"
#include "NALUDump.h"
#include "HumaxHeader.h"
#include "EycosHeader.h"

#include "PESProcessor.h"


/*#if defined(LINUX)
  const bool TMS = TRUE;
  const bool WINDOWS = FALSE;
#elif defined(_WIN32)
  const bool TMS = FALSE;
  const bool WINDOWS = TRUE;
#else
  const bool TMS = FALSE;
  const bool WINDOWS = FALSE;
#endif*/

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
const char             *ExePath;
byte                   *PATPMTBuf = NULL, *EPGPacks = NULL;
unsigned long long      RecFileSize = 0;
time_t                  RecFileTimeStamp = 0;
SYSTEM_TYPE             SystemType = ST_UNKNOWN;
byte                    PACKETSIZE = 192, PACKETOFFSET = 4, OutPacketSize = 0;
word                    VideoPID = (word) -1, TeletextPID = (word) -1, SubtitlesPID = (word) -1, TeletextPage = 0;
tAudioTrack             AudioPIDs[MAXCONTINUITYPIDS];
word                    ContinuityPIDs[MAXCONTINUITYPIDS], NrContinuityPIDs = 1;
bool                    isHDVideo = FALSE, AlreadyStripped = FALSE, HumaxSource = FALSE, EycosSource = FALSE, DVBViewerSrc = FALSE;
bool                    DoStrip = FALSE, DoSkip = FALSE, RemoveScrambled = FALSE, RemoveEPGStream = FALSE, RemoveTeletext = FALSE, ExtractTeletext = FALSE, ExtractAllTeletext = FALSE, RebuildNav = FALSE, RebuildInf = FALSE, RebuildSrt = FALSE, DoInfoOnly = FALSE, DoFixPMT = FALSE, DemuxAudio = FALSE, MedionMode = FALSE, MedionStrip = FALSE, WriteDescPackets = TRUE, PMTatStart = FALSE;
int                     DoCut = 0, DoMerge = 0, DoInfFix = 0;  // DoCut: 1=remove_parts, 2=copy_separate, DoMerge: 1=append, 2=merge  // DoInfFix: 1=enable, 2=inf to be fixed
int                     curInputFile = 0, NrInputFiles = 1, NrEPGPacks = 0;
int                     dbg_DelBytesSinceLastVid = 0;

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker2        *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NavFrames = 0;
int                     NavDurationMS = 0;
int                     NewStartTimeOffset = -1;
dword                   TtxPTSOffset = 0;

// Lokale Variablen
static char             InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], InfFileFirstIn[FBLIB_DIR_SIZE], /*InfFileOld[FBLIB_DIR_SIZE],*/ NavFileOut[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE]; // TeletextOut[FBLIB_DIR_SIZE];
static bool             HasNavIn, HasNavOld, HasInfOld;
static FILE            *fIn = NULL;
static FILE            *fOut = NULL, *fAudioOut = NULL;
static tPSBuffer        AudioPES;
static int              LastAudBuffer = 0;
static byte            *PendingBuf = NULL;
static int              PendingBufLen = 0, PendingBufStart = 0;
static bool             isPending = FALSE;

long long               CurrentPosition = 0;
static unsigned int     RecFileBlocks = 0;
static long long        PositionOffset = 0, NrPackets = 0;
static unsigned int     CurPosBlocks = 0, CurBlockBytes = 0, BlocksOneSecond = 250, BlocksOnePercent;
static unsigned int     NrSegments = 0, NrCopiedSegments = 0;
static int              CutTimeOffset = 0;
long long               NrDroppedZeroStuffing=0;
static long long        NrDroppedFillerNALU=0, NrDroppedAdaptation=0, NrDroppedNullPid=0, NrDroppedEPGPid=0, NrDroppedTxtPid=0, NrScrambledPackets=0, CurScrambledPackets=0, NrIgnoredPackets=0;
static dword            LastPCR = 0, LastTimeStamp = 0, CurTimeStep = 1200, *pGetPCR = NULL;
static signed char      ContinuityCtrs[MAXCONTINUITYPIDS];
static bool             ResumeSet = FALSE, AfterFirstEPGPacks = FALSE, DoOutputHeaderPacks = FALSE;

// Continuity Statistik
static tContinuityError FileDefect[MAXCONTINUITYPIDS];
static long long        LastContErrPos = 0;
static dword            LastGoodPCR = 0, FirstPCRAfter = 0;
static int              NrContErrsInFile = 0;
char                   *ExtEPGText = NULL;


/* #ifdef _WIN32
static LPWSTR winMbcsToUnicode(const char *zText){
  int nByte;
  LPWSTR zMbcsText;

  nByte = MultiByteToWideChar(CP_ACP, 0, zText, -1, NULL, 0) * sizeof(WCHAR);  // CP_OEMCP
  if (nByte > 0)
    if ((zMbcsText = (LPWSTR) malloc(nByte * sizeof(WCHAR))))
    {
      if (MultiByteToWideChar(CP_ACP, 0, zText, -1, zMbcsText, nByte) > 0)  // CP_OEMCP
        return zMbcsText;
      else
        free(zMbcsText);
    }
  return NULL;
}
#endif */

bool HDD_FileExist(const char *AbsFileName)
{
#if defined(_WIN32) // && defined(_MSC_VER)
//  LPWSTR wAbsFileName = winMbcsToUnicode(AbsFileName);
  DWORD dwAttrib = GetFileAttributesA(AbsFileName);
//  free(wAbsFileName);
  return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
#else
  struct stat statbuf = {0};
  return (stat(AbsFileName, &statbuf) == 0);
#endif
}

static bool HDD_DirExist(const char *AbsDirName)
{
#if defined(_WIN32) // && defined(_MSC_VER)
//  LPWSTR wAbsDirName = winMbcsToUnicode(AbsDirName);
  DWORD dwAttrib = GetFileAttributesA(AbsDirName);
//  free(wAbsDirName);
  return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
#else
  struct stat statbuf = {0};
  return ((stat(AbsDirName, &statbuf) == 0) && (statbuf.st_mode & S_IFDIR));
#endif
}

bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize)
{
  bool ret = FALSE;

  TRACEENTER;
  if(AbsFileName)
  {
#if defined(_WIN32) // && defined(_MSC_VER)
//    LPWSTR wAbsFileName = winMbcsToUnicode(AbsFileName);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ret = (GetFileAttributesExA(AbsFileName, GetFileExInfoStandard, &fad));
//    free(wAbsFileName);
    if (ret && OutFileSize)
      *OutFileSize = (((unsigned long long)fad.nFileSizeHigh) << 32) + fad.nFileSizeLow;
#else
    struct stat64 statbuf = {0};
    ret = (stat64(AbsFileName, &statbuf) == 0);
    if (ret && OutFileSize)
      *OutFileSize = statbuf.st_size;
#endif
  }
  TRACEEXIT;
  return ret;
}

// Zeit-Angaben in UTC (Achtung! Windows führt ggf. eine eigene DST-Anpassung durch)
static time_t HDD_GetFileDateTime(char const *AbsFileName)
{
  struct stat64         statbuf = {0};
  time_t                Result = 0;

  TRACEENTER;
  if(AbsFileName)
  {
#if defined(_WIN32) // && defined(_MSC_VER)
//    WIN32_FILE_ATTRIBUTE_DATA fad;
//    LPWSTR wAbsFileName = winMbcsToUnicode(AbsFileName);
    HANDLE hFile = CreateFileA(AbsFileName, 0/*GENERIC_READ*/, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    FILETIME Result2;
    time_t Result3 = 0;

//    if (GetFileAttributesEx(wAbsFileName, GetFileExInfoStandard, &fad))
//      Result3 = (time_t) ((((unsigned long long)fad.ftLastWriteTime.dwHighDateTime) << 32) + fad.ftLastWriteTime.dwLowDateTime) / 10000000 - 11644473600LL;  // Convert Windows ticks to Unix Timestamp
    if (GetFileTime(hFile, NULL, NULL, &Result2))
      Result3 = (time_t) ((((long long)Result2.dwHighDateTime) << 32) + Result2.dwLowDateTime) / 10000000 - 11644473600LL;  // Convert Windows ticks to Unix Timestamp
    CloseHandle(hFile);
//    free(wAbsFileName);
#endif

    if(stat64(AbsFileName, &statbuf) == 0)
    {
      Result = statbuf.st_mtime;

      #ifdef _WIN32
      {
        struct tm timeinfo = {0}, timeinfo_sys = {0};
        time_t now = time(NULL);

        localtime_s(&timeinfo, &Result);
        localtime_s(&timeinfo_sys, &now);
        Result -= (3600 * (timeinfo_sys.tm_isdst - timeinfo.tm_isdst));  // Windows-eigene DST-Korrektur ausgleichen
//        Result += _timezone - (timeinfo.tm_isdst ? 3600 : 0);           // Windows-Datum (local) in UTC umwandeln
//        Result3+= _timezone - (timeinfo.tm_isdst ? 3600 : 0);
if (Result3 != Result)
  printf("ASSERTION ERROR! Windows API date (%llu) does not match 'correct' stat date (%llu).\n", (unsigned long long)Result3, (unsigned long long)Result);
      }
      #endif
    }
  }
  TRACEEXIT;
  return Result;
}

static bool HDD_SetFileDateTime(char const *AbsFileName, time_t NewDateTime)
{
  struct stat64         statbuf = {0};
  struct utimbuf        timebuf = {0};

  TRACEENTER;
  if(NewDateTime == 0)
    NewDateTime = time(NULL);

  if(AbsFileName && ((unsigned long)NewDateTime < 0xD0790000))
  {
    if(stat64(AbsFileName, &statbuf) == 0)
    {
      #ifdef _WIN32
        struct tm timeinfo = {0}, timeinfo_sys = {0};
        time_t now = time(NULL);

        localtime_s(&timeinfo, &NewDateTime);
        localtime_s(&timeinfo_sys, &now);
        NewDateTime += (3600 * (timeinfo_sys.tm_isdst - timeinfo.tm_isdst));  // Windows-eigene DST-Korrektur ausgleichen
      #endif

      timebuf.actime = statbuf.st_atime;
      timebuf.modtime = NewDateTime;
      utime(AbsFileName, &timebuf);
      TRACEEXIT;
#ifndef _WIN32
      return TRUE;
#endif
    }

#if defined(_WIN32) // && defined(_MSC_VER)
    {
      bool ret = FALSE;
      FILETIME NewWriteTime;
//      WIN32_FILE_ATTRIBUTE_DATA fad;
//      LPWSTR wAbsFileName = winMbcsToUnicode(AbsFileName);
      HANDLE hFile = CreateFileA(AbsFileName, 0/*GENERIC_READ*/, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
//      if (GetFileAttributesEx(wAbsFileName, GetFileExInfoStandard, &fad))
      if (hFile)
      {
//        FILE_BASIC_INFO b;
        NewDateTime = (NewDateTime + 11644473600LL) * 10000000;  // Convert Unix Timestamp to Windows ticks
        NewWriteTime.dwHighDateTime = ((long long) NewDateTime) >> 32;
        NewWriteTime.dwLowDateTime =  ((long long) NewDateTime) & 0x00FFll;
//        b.LastWriteTime.QuadPart  = NewDateTime;
//        b.LastAccessTime.HighPart = fad.ftLastAccessTime.dwHighDateTime;
//        b.LastAccessTime.LowPart  = fad.ftLastAccessTime.dwLowDateTime;
//        b.FileAttributes          = GetFileAttributes(wAbsFileName);
//        SetFileInformationByHandle(hFile, FileBasicInfo, &b, sizeof(b));
        SetFileTime(hFile, NULL, NULL, &NewWriteTime);
        CloseHandle(hFile);
      }
//      free(wAbsFileName);
      TRACEEXIT;
      return ret;
    }
#endif
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
// *****  Statistik Continuity Errors  *****
// ----------------------------------------------

static int GetPidId(word PID)
{
  int i;
  TRACEENTER;
  for (i = 0; i < NrContinuityPIDs; i++)
  {
    if (ContinuityPIDs[i] == PID)
    {
      TRACEEXIT;
      return i;
    }
  }
  TRACEEXIT;
  return -1;
}

static bool PrintFileDefect()
{
  double percent = 0.0;
  int i;
  TRACEENTER;
  if (FileDefect[0].PID || FileDefect[GetPidId(18)].PID)
  {
    // CONTINUITY ERROR:  Nr.  Position(Prozent)  { PID;  Position }
    for (i = 0; i < NrContinuityPIDs; i++)
    {
      if (FileDefect[i].Position > 0)
        { percent = (double)FileDefect[i].Position*100/RecFileSize; break; }
    }
    printf("TSCheck: Continuity Error:\t%d.\t%.2f%%", NrContErrsInFile+1, percent);
    printf((FirstPCRAfter ? "\t%u->%u (%.3f sec)" : "\t%u->?"), LastGoodPCR, FirstPCRAfter, (float)((int)(FirstPCRAfter-LastGoodPCR)) / 1000);
    for (i = 0; i < NrContinuityPIDs; i++)
      printf("\t%hd\t%lld", FileDefect[i].PID, FileDefect[i].Position);
    printf("\n");
    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}

void AddContinuityPids(word newPID, bool first)
{
  int k;
  if (first || (NrContinuityPIDs < MAXCONTINUITYPIDS))
  {
    for (k = 1; (k < NrContinuityPIDs) && (ContinuityPIDs[k] != newPID); k++);
    
    if (k >= NrContinuityPIDs)
    {
      if (first)
      {
        if (NrContinuityPIDs < MAXCONTINUITYPIDS)
          NrContinuityPIDs++;
        for (k = NrContinuityPIDs-1; k > 1; k--)
          ContinuityPIDs[k] = ContinuityPIDs[k-1];
        ContinuityPIDs[1] = newPID;
      }
      else
        ContinuityPIDs[NrContinuityPIDs++] = newPID;
    }
  }
}

void AddContinuityError(word CurPID, long long CurrentPosition, byte CountShould, byte CountIs)
{
  int PidID;
  TRACEENTER;

  // add error to array
  if ((PidID = GetPidId(CurPID)) >= 0)
  {
    if ((FileDefect[PidID].PID && CurPID != 18) || (LastContErrPos == 0) || (CurrentPosition - LastContErrPos > CONT_MAXDIST))
    {
      // neuer Fehler
      if (PrintFileDefect())
        NrContErrsInFile++;
      memset(FileDefect, 0, MAXCONTINUITYPIDS * sizeof(tContinuityError));
      LastGoodPCR = global_timestamp;
      FirstPCRAfter = 0;
      pGetPCR = &FirstPCRAfter;
    }
    FileDefect[PidID].PID         = CurPID;
    FileDefect[PidID].Position    = CurrentPosition;
    FileDefect[PidID].CountIs     = (byte) CountIs;
    FileDefect[PidID].CountShould = (byte) CountShould;
    LastContErrPos = CurrentPosition;
  }
  else
    printf("ERROR: Too many PIDs! (cannot happen)\n");
  TRACEEXIT;
}


// ----------------------------------------------
// *****  Analyse von REC-Files  *****
// ----------------------------------------------

dword CalcBlockSize(long long Size)
{
  // Workaround für die Division durch BLOCKSIZE (9024)
  // Primfaktorenzerlegung: 9024 = 2^6 * 3 * 47
  // max. Dateigröße: 256 GB (dürfte reichen...)
  if (Size >= 0)
    return (dword)(Size >> 6) / 141;
  else
  {
//return 0;
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
    fseeko64(RecFile, 9024, SEEK_SET);
    if (fread(Buffer, 1, 5573, RecFile) == 5573)
    {
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
bool isPacketStart_Rev(const byte PacketArray[], int ArrayLen)  // braucht 9*192+5 = 1733 / 3*192+5 = 581
{
  int                   i;
  bool                  ret = TRUE;

  TRACEENTER;
  for (i = 1; i <= 10; i++)
  {
    if (i * PACKETSIZE > ArrayLen + PACKETOFFSET)
    {
      if (i < 3) ret = FALSE;
      break;
    }
    if (PacketArray[ArrayLen - (i * PACKETSIZE) + PACKETOFFSET] != 'G')
    {
      ret = FALSE;
      break;
    }
  }
  TRACEEXIT;
  return ret;
}

int FindNextPacketStart(const byte PacketArray[], int ArrayLen)  // braucht [ 20*192 = 3840 / 10*188 + 1184 = 3064 ] + 1733
{
  int ret = -1;
  int i;

  TRACEENTER;
  for (i = 0; i <= 20; i++)   // 20*192 = 3840
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
    for (i = 1; i <= 3064; i++)   // 10*188 + 1184 = 3064
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
int FindPrevPacketStart(const byte PacketArray[], int ArrayLen)  // braucht [ 20*192 = 3840 / 10*188 + 1184 = 3064 ] + 1733
{
  int ret = -1;
  int i;

  TRACEENTER;
  for (i = 0; i <= 20; i++)   // 20*192 = 3840
  {
    if ((i+1) * PACKETSIZE > ArrayLen + PACKETOFFSET)
      break;

    if (PacketArray[ArrayLen - ((i+1) * PACKETSIZE) + PACKETOFFSET] == 'G')
    {
      if (isPacketStart_Rev(PacketArray, ArrayLen - i*PACKETSIZE))
      {
        ret = ArrayLen - (i+1) * PACKETSIZE;
        break;
      }
    }
  }
  
  if (ret < 0)
  {
    for (i = 1; i <= 3064; i++)   // 10*188 + 1184 = 3064
    {
      if (i + PACKETSIZE > ArrayLen + PACKETOFFSET)
        break;

      if (PacketArray[ArrayLen - PACKETSIZE - i + PACKETOFFSET] == 'G')
      {
        if (isPacketStart_Rev(PacketArray, ArrayLen - i))
        {
          ret = ArrayLen - i - PACKETSIZE;
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
// *****  Öffnen / Schließen der Output-Files  *****
// --------------------------------------------------

static bool OpenInputFiles(char *RecFileIn, bool FirstTime)
{
  bool                  ret = TRUE;
  char                  AddFileIn[FBLIB_DIR_SIZE], MDTtxName[FBLIB_DIR_SIZE];
//  byte                 *InfBuf_tmp = NULL;
  tSegmentMarker2      *Segments_tmp = NULL;

  // dirty hack: aktuelle Pointer für InfBuffer und SegmentMarker speichern
  byte                 *InfBuffer_bak = InfBuffer;
  TYPE_RecHeader_Info  *RecHeaderInfo_bak = RecHeaderInfo;
  TYPE_Bookmark_Info   *BookmarkInfo_bak = BookmarkInfo;
  tPVRTime              OrigStartTime_bak = OrigStartTime;
  byte                  OrigStartSec_bak = OrigStartSec;
//  dword                 InfDuration_bak = InfDuration;

  tSegmentMarker2      *SegmentMarker_bak = SegmentMarker;
  int                   NrSegmentMarker_bak = NrSegmentMarker;
  int                   k;
  char                 *p;

  TRACEENTER;
//  CurrentStartTime = 0;
//  CurrentStartSec = 0;

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

    // dirty hack: InfBuffer und SegmentMarker auf temporäre Buffer umbiegen
/*    InfBuffer = InfBuf_tmp;
    memset(InfBuffer, 0, 32768);
    RecHeaderInfo = (TYPE_RecHeader_Info*) InfBuffer;
    BookmarkInfo = &(((TYPE_RecHeader_TMSS*)InfBuffer)->BookmarkInfo); */
    SegmentMarker = Segments_tmp;
    NrSegmentMarker = 0;
  }

  PMTatStart = FALSE;
  AfterFirstEPGPacks = FALSE;
  for (k = 0; k < MAXCONTINUITYPIDS; k++)
  {
    ContinuityPIDs[k] = (word) -1;
    ContinuityCtrs[k] = -1;
  }
  NrContinuityPIDs = 1;
  memset(AudioPIDs, 0, MAXCONTINUITYPIDS * sizeof(tAudioTrack));
  for (k = 0; k < MAXCONTINUITYPIDS; k++)
    AudioPIDs[k].streamType = STREAMTYPE_UNKNOWN;
  memset(FileDefect, 0, MAXCONTINUITYPIDS * sizeof(tContinuityError));
  NrContErrsInFile = 0;
  LastContErrPos = 0;

  // Detektion Sonderformate
  p = strrchr(RecFileIn, '.');
  if (p && strcmp(p, ".vid") == 0)
    HumaxSource = TRUE;
  else if (p && strcmp(p, ".trp") == 0)
    EycosSource = TRUE;
  else if (p && strcmp(p, ".TS4") == 0)
    isHDVideo = TRUE;
  else if (p && !MedionMode && strcmp(p, ".pes") == 0)
  {
    p = strrchr(RecFileIn, '_');
    if (p && strcmp(p, "_video.pes") == 0)
    {
      MedionMode = 1;
      if(DoStrip) { MedionStrip = TRUE; DoStrip = FALSE; }
    }
  }
#ifndef LINUX
  if (HumaxSource || EycosSource)  OutCutVersion = 4;
#endif

  // Spezialanpassung Medion
  if (MedionMode)
  {
    char               *p;
    size_t              len;

    strcpy(AddFileIn, RecFileIn);
    if((p = strrchr(AddFileIn, '.'))) *p = '\0';
    if(((len = strlen(AddFileIn)) > 6) && (strncmp(&AddFileIn[len-6], "_video", 6) == 0)) AddFileIn[len-6] = '\0';
    if ((len = strlen(AddFileIn)) < sizeof(AddFileIn) - 11)
    {
      snprintf(MDTtxName, sizeof(MDTtxName), "%s_ttx.pes", AddFileIn);
      strcat (AddFileIn, "_audio1.pes");
    }
    else
      ret = FALSE;

    VideoPID = 101;          // vorher: 100, künftig: 101   // TODO
    AudioPIDs[0].pid = 102;  // vorher: 101, künftig: 102
    TeletextPID = 104;       // vorher: 102, künftig: 104
  }

  printf("\nInput file: %s\n", RecFileIn);

  //Get the time stamp of the .rec. We assume that this is the time when the recording has finished
  RecFileTimeStamp = HDD_GetFileDateTime(RecFileIn);

  if (HDD_GetFileSize(RecFileIn, &RecFileSize))
    fIn = fopen(RecFileIn, "rb");

  if (fIn)
  {
    int FileOffset = 0, PS;
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);

    if (MedionMode == 1)
    {
      unsigned long long AddSize = 0;
      if(HDD_GetFileSize(AddFileIn, &AddSize))  RecFileSize += AddSize;
      if(HDD_GetFileSize(MDTtxName, &AddSize))  RecFileSize += AddSize;
    }

    RecFileBlocks = CalcBlockSize(RecFileSize);
    BlocksOnePercent = (RecFileBlocks * NrInputFiles) / 100;

    if ((PS = GetPacketSize(fIn, &FileOffset)) || MedionMode)
    {
      byte FileSec;
      tPVRTime FileDate;

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
      FileDate = Unix2TFTime(RecFileTimeStamp, &FileSec, TRUE);
      printf("  File date: %s (local)\n", TimeStrTF(FileDate, FileSec));

      LoadInfFromRec(RecFileIn);
      if (FirstFilePTS && LastFilePTS && !RecHeaderInfo->DurationSec)  // gute Idee??
      {
        int dPTS = DeltaPCR(FirstFilePTS, LastFilePTS) / 45;
        RecHeaderInfo->DurationMin = (word)(dPTS / 60000);
        RecHeaderInfo->DurationSec = (word)abs((dPTS/1000) % 60);
      }

      if (EycosSource)
      {
        char               EycosPartFile[FBLIB_DIR_SIZE];
        unsigned long long AddSize = 0;
        int                EycosNrParts = EycosGetNrParts(RecFileIn);
        for (k = 1; k < EycosNrParts; k++)
          if(HDD_GetFileSize(EycosGetPart(EycosPartFile, RecFileIn, k), &AddSize))  RecFileSize += AddSize;
        RecFileBlocks = CalcBlockSize(RecFileSize);
        BlocksOnePercent = (RecFileBlocks * NrInputFiles) / 100;
      }

//      if(DoStrip) ContinuityPIDs[0] = (word) -1;
      printf("  PIDs to be checked for continuity: [0] %s%hd%s", (DoStrip ? "[" : ""), ContinuityPIDs[0], (DoStrip ? "]" : ""));
      for (k = 1; k < NrContinuityPIDs; k++)
        printf(", [%d] %hd", k, ContinuityPIDs[k]);
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

    if (LoadInfFile(InfFileIn, FirstTime))
    {
      if (InfDuration)
        BlocksOneSecond = RecFileBlocks / InfDuration;
      if (FirstTime && AlreadyStripped)
        printf("  INFO: File has already been stripped.\n");
    }
    else
    {
      fclose(fIn); fIn = NULL;
      if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
      ret = FALSE;
    }

    if (ret && (MedionMode == 1))
      ret = SimpleMuxer_Open(fIn, AddFileIn, MDTtxName);

    // ggf. nav-File öffnen
    snprintf(AddFileIn, sizeof(AddFileIn), "%s.nav", RecFileIn);
    printf("\nNav file: %s\n", AddFileIn);
    HasNavIn = LoadNavFileIn(AddFileIn);
    if (!HasNavIn)
      printf("  WARNING: Cannot open nav file %s.\n", AddFileIn);
    else if (NavDurationMS && !RecHeaderInfo->DurationSec)  // gute Idee??
    {
      RecHeaderInfo->DurationMin = (word)(NavDurationMS / 60000);
      RecHeaderInfo->DurationSec = (word)(abs(NavDurationMS/1000) % 60);
    }
    if (!ret)
      { fclose(fNavIn); fNavIn = NULL; }
  }

  // ggf. cut-File einlesen
  if (ret)
  {
    if (NrSegmentMarker <= 2 || (!EycosSource && !HumaxSource))
    {
      GetFileNameFromRec(RecFileIn, ".cut", AddFileIn);
      printf("\nCut file: %s\n", AddFileIn);
      if (!CutFileLoad(AddFileIn))
      {
//        HasCutIn = FALSE;
        AddDefaultSegmentMarker();
        if(!FirstTime && DoCut)
          SegmentMarker[0].Selected = TRUE;
//        DoCut = 0;
      }
    }
  }

  // ggf. srt-File laden
  if (ret)
  {
    GetFileNameFromRec(RecFileIn, ".srt", AddFileIn);
    RebuildSrt = (!ExtractTeletext && LoadSrtFileIn(AddFileIn));
  }

  if (!FirstTime)
  {
    int i;

    // neu ermittelte Bookmarks kopieren
    for (i = 0; i < (int)BookmarkInfo->NrBookmarks; i++)
      BookmarkInfo_bak->Bookmarks[BookmarkInfo_bak->NrBookmarks++] = BookmarkInfo->Bookmarks[i];

    // neu ermittelte SegmentMarker kopieren
//    if (NrSegmentMarker_bak > 2 || NrSegmentMarker > 2 || (SegmentMarker && SegmentMarker[0].pCaption))
    {
      // letzten SegmentMarker der ersten Aufnahme löschen (wird ersetzt durch Segment 0 der zweiten)
      if (NrSegmentMarker_bak >= 2)
        free(SegmentMarker_bak[--NrSegmentMarker_bak].pCaption);

      // neue SegmentMarker kopieren
      for (i = 0; i < NrSegmentMarker; i++)
        SegmentMarker_bak[NrSegmentMarker_bak++] = SegmentMarker[i];
    }
/*    else if (NrSegmentMarker_bak >= 2)
    {
      // beide ohne cut-File -> letzten SegmentMarker anpassen
      SegmentMarker_bak[1].Position = RecFileBlocks;
      SegmentMarker_bak[1].Timems = InfDuration * 1000;
    } */

    // dirty hack: vorherige Pointer für InfBuffer und SegmentMarker wiederherstellen
//    free(InfBuf_tmp); InfBuf_tmp = NULL;
    InfProcessor_Free();
    if(Segments_tmp) { free(Segments_tmp); Segments_tmp = NULL; }
    InfBuffer = InfBuffer_bak;
    RecHeaderInfo = RecHeaderInfo_bak;
    BookmarkInfo = BookmarkInfo_bak;
    SegmentMarker = SegmentMarker_bak;
    NrSegmentMarker = NrSegmentMarker_bak;
    OrigStartTime = OrigStartTime_bak;
    OrigStartSec = OrigStartSec_bak;
//    InfDuration = InfDuration + InfDuration_bak;  // eigentlich unnötig
  }

  printf("\n");
  TRACEEXIT;
  return ret;
}

static bool OpenOutputFiles(void)
{
  unsigned long long OutFileSize = 0;
  int k = 0;
  TRACEENTER;

  // ggf. Output-File öffnen
  if (*RecFileOut && (DoStrip || OutPacketSize!=PACKETSIZE || DoCut || DoMerge || RemoveEPGStream || RemoveTeletext || (!ExtractTeletext && !ExtractAllTeletext && !DemuxAudio)))
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

  HasInfOld = FALSE;
  if (*RecFileOut)
  {
    if (RebuildInf || *InfFileIn)
    {  
      if (DoMerge == 1)
      {
        HasInfOld = TRUE;
        snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileOut);  // *CW: InfFileIn wird für die Backup-inf verwendet (statt InfFileOld), um Speicher zu sparen
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf_new", RecFileOut);
      }
      else
        snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
      if(!*InfFileIn && !HumaxSource && !EycosSource) WriteCutInf = TRUE;
    }
    else
      InfFileOut[0] = '\0';
  }
  else
  {
    if (RebuildInf /*|| !*InfFileIn*/)
    {
//      RebuildInf = TRUE;
      if(!*InfFileIn) WriteCutInf = TRUE;
//      InfFileIn[0] = '\0';  // *CW: Zeile entfernt damit InfFileIn für DoInfFix zur Verfügung steht (vorheriger Grund der Zeile unbekannt)
      HasInfOld = TRUE;
      snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
      snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf_new", RecFileIn);
    }
    else
    {
      InfFileOut[0] = '\0';
    }
  }
  if (*InfFileOut)
    printf("\nInf output: %s\n", InfFileOut);

  // ggf. Output-nav öffnen
/*
WENN OutFile nicht existiert
  -> nav = in
SONST
  WENN nav existiert oder Rebuildinf
    -> nav = out
  SONST
    -> nav = NULL */

  HasNavOld = FALSE;
  if (*RecFileOut)
  {
    if (RebuildNav || HasNavIn)
    {
      if (DoStrip || RemoveEPGStream || RemoveTeletext || RemoveScrambled || OutPacketSize != PACKETSIZE) RebuildNav = TRUE;
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
    }
    else
      NavFileOut[0] = '\0';
  }
  else
  {
    if (RebuildNav /*|| !HasNavIn*/)
    {
//      RebuildNav = TRUE;
      HasNavOld = TRUE;
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
  if (ExtractTeletext || RebuildSrt)
  {
    char AbsFileName[FBLIB_DIR_SIZE];
    if (*RecFileOut)
      GetFileNameFromRec(RecFileOut, ".srt", AbsFileName);
    else
      GetFileNameFromRec(RecFileIn, ".srt", AbsFileName);

    if (ExtractTeletext)
    {
      if (LoadTeletextOut(AbsFileName))
        printf("Teletext output: %s\n", AbsFileName);
      else
        ExtractTeletext = FALSE;
    }
    else if (*RecFileOut && RebuildSrt)
    {
      if (LoadSrtFileOut(AbsFileName))
        printf("\nSrt output: %s\n", AbsFileName);
      else
        RebuildSrt = FALSE;
    }
  }

  // Demux Audio Outfile ermitteln
  if (DemuxAudio)
  {
    char AbsFileName[FBLIB_DIR_SIZE];
    if (*RecFileOut)
      GetFileNameFromRec(RecFileOut, "_audio.pes", AbsFileName);
    else
      GetFileNameFromRec(RecFileIn, "_audio.pes", AbsFileName);

    fAudioOut = fopen(AbsFileName, ((DoMerge==1) ? "ab" : "wb"));

    PSBuffer_Init(&AudioPES, AudioPIDs[0].pid, 65536, FALSE);
  }

  // Header-Pakete ausgeben
  if ((HumaxSource || EycosSource || MedionMode==1 || (WriteDescPackets && (CurrentPosition >= 384 || !PMTatStart))) && fOut && DoMerge != 1)
  {
//    DoOutputHeaderPacks = TRUE;
    for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
      if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192], OutPacketSize, 1, fOut))
        PositionOffset -= OutPacketSize;
    if (MedionMode == 1)
      SimpleMuxer_DoEITOutput();
    else if (WriteDescPackets && EPGPacks)
    {
      for (k = 0; k < NrEPGPacks; k++)
        if (fwrite(&EPGPacks[((OutPacketSize==192) ? 0 : 4) + k*192], OutPacketSize, 1, fOut))
          PositionOffset -= OutPacketSize;
    }
  }
  AfterFirstEPGPacks = FALSE;
  printf("\n");
  TRACEEXIT;
  return TRUE;
}

static void CloseInputFiles(bool PrintErrors, bool SetStripFlags, bool SetStartTime)
{
  TRACEENTER;

  if (PrintErrors)
  {
    if(PrintFileDefect())  NrContErrsInFile++;
    printf("TSCheck: %d continuity errors found.\n", NrContErrsInFile);
  }

  if (fIn)
  {
    fclose(fIn); fIn = NULL;
  }
  if (*InfFileIn && SetStripFlags)
  {
    time_t OldInfTime = HDD_GetFileDateTime(InfFileIn);
    SetInfStripFlags(InfFileIn, !DoCut, DoStrip && !DoMerge && DoCut!=2, SetStartTime);
    if (OldInfTime)
      HDD_SetFileDateTime(InfFileIn, OldInfTime);
  }
  CloseNavFileIn();
  CloseSrtFileIn();
  if(MedionMode == 1) SimpleMuxer_Close();
  if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }

  TRACEEXIT;
}

static bool CloseOutputFiles(void)
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
      SaveInfFile(InfFileOut, InfFileFirstIn);
      CloseTeletextOut();
      CloseSrtFileOut();
      PSBuffer_Reset(&AudioPES);
      TRACEEXIT;
      return FALSE;
    }
    fOut = NULL;
  }

  if (EycosSource && (NrSegmentMarker > 2))
  {
    // SegmentMarker aus TimeStamps importieren (weil Eycos die Bookmarks scheinbar in Millisekunden speichert)
//    if (CutImportFromTimeStamps(3, OutPacketSize))
//      CutExportToBM(BookmarkInfo);
    SegmentMarker[0].Position = 0;
    SegmentMarker[0].Timems = 0;
  }

  if ((*CutFileOut || (*InfFileOut && WriteCutInf)) && !CutFileSave(CutFileOut))
    printf("  WARNING: Cannot create cut %s.\n", CutFileOut);

  if (*InfFileOut && !SaveInfFile(InfFileOut, InfFileFirstIn))
    printf("  WARNING: Cannot create inf %s.\n", InfFileOut);

  if (ExtractTeletext && !CloseTeletextOut())
    printf("  WARNING: Cannot create teletext files.\n");

  if (!CloseSrtFileOut())
    printf("  WARNING: Cannot create srt output file.\n");

  if (fAudioOut)
  {
    PSBuffer_Reset(&AudioPES);
    fclose(fAudioOut);
    fAudioOut = NULL;
  }

  if (*RecFileOut)
    HDD_SetFileDateTime(RecFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
  if (*InfFileOut)
    HDD_SetFileDateTime(InfFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
  if (*NavFileOut)
    HDD_SetFileDateTime(NavFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
//  if (*CutFileOut)
//    HDD_SetFileDateTime(CutFileOut, TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));


  if (HasNavOld)
  {
    char NavFileOld[FBLIB_DIR_SIZE], NavFileBak[FBLIB_DIR_SIZE];
    snprintf(NavFileOld, sizeof(NavFileOut), "%s.nav", RecFileIn);
    snprintf(NavFileBak, sizeof(NavFileBak), "%s_bak", NavFileOld);
    remove(NavFileBak);
    rename(NavFileOld, NavFileBak);
    rename(NavFileOut, NavFileOld);
  }
  if (HasInfOld)
  {
    char InfFileBak[FBLIB_DIR_SIZE];
    snprintf(InfFileBak, sizeof(InfFileBak), "%s_bak", InfFileIn);
    remove(InfFileBak);
    rename(InfFileIn, InfFileBak);
    rename(InfFileOut, InfFileIn);
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
  printf("(C) 2016-2023 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- based on MovieCutter 3.6 -\n");
  printf("- portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)\n");
#ifdef _DEBUG
  printf("(long long: %d, long: %d, int: %d, dword: %d, word: %d, short: %d, byte: %d, char: %d)\n", sizeof(long long), sizeof(long), sizeof(int), sizeof(dword), sizeof(word), sizeof(short), sizeof(byte), sizeof(char));
#endif

#ifndef LINUX
  {
    char RealExePath[FBLIB_DIR_SIZE];
    struct tm timeinfo = {0};
    time_t curTime = time(NULL);

    if (FindExePath(argv[0], RealExePath, sizeof(RealExePath)))
      printf("\nExecutable path: %s\n", RealExePath);
    #ifdef _WIN32
      localtime_s(&timeinfo, &curTime);
      _tzset();
      printf("\nExecution time: %s (local)\n", TimeStr(curTime));
      printf("Local timezone: %s%s (GMT%+ld)\n", _tzname[0], (timeinfo.tm_isdst ? " + DST" : ""), -_timezone/3600 + timeinfo.tm_isdst);
    #else
      localtime_r(&curTime, &timeinfo);
      tzset();
      printf("\nExecution time: %s (local)\n", TimeStr(curTime));
      printf("Local timezone: %s%s (GMT%+ld)\n", tzname[0], (timeinfo.tm_isdst ? " + DST" : ""), -timezone/3600 + timeinfo.tm_isdst);
    #endif
  }
#endif

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
  const char *Types[4]       = { "video", "audio1", "ttx", "epg" };
  const char *DemuxInFile = argv[1];

  char        DemuxOut[FBLIB_DIR_SIZE];
  tPSBuffer   Streams[4];
  FILE       *out[4], *in;
  int         PIDs[4];
  int         LastBuffer[4]  = { 0, 0, 0, 0 };
  int         FileOffset, i;
  int         ret = 0;
  const char *p;

//  const char *p = strrchr(DemuxInFile, '/');
//  if(!p)      p = strrchr(DemuxInFile, '\\');

  printf("\n- TS-to-PES converter -\n");
  if (argc <= 1)
  {
    printf("\nUsage: ts2pes <Input.ts> [<VideoPID>, <AudioPID>, <TeletextPID>]\n");
    exit(1);
  }

  p = strrchr(DemuxInFile, '.');
  if(!p)  p = &DemuxInFile[strlen(DemuxInFile)];

  memset(DemuxOut, 0, sizeof(DemuxOut));
  strncpy(DemuxOut, DemuxInFile, min(p-DemuxInFile, sizeof(DemuxOut)-1));
  printf("\nInput File: %s\n", DemuxInFile);
  printf("Output Dir: %s\n", DemuxOut);

  PIDs[3] = 18;
  if (argc > 2)
  {
    for (i = 0; i < 3; i++)
      PIDs[i] = strtol(argv[i+2], NULL, 10);
  }
  else
  {
    InfProcessor_Init();
    HDD_GetFileSize(DemuxInFile, &RecFileSize);
    LoadInfFromRec(DemuxInFile);
    if (VideoPID && VideoPID != (word)-1)
    {
      PIDs[0] = VideoPID, PIDs[1] = ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.AudioPID, PIDs[2] = TeletextPID; PIDs[3] = 18;
    }
    else
    {
      PIDs[0] = 100, PIDs[1] = 101, PIDs[2] = 102; PIDs[3] = 18;
    } 
    InfProcessor_Free();
  }
  printf("\nPIDS: Video=%hd, Audio=%hd, Teletext=%hd, EPG=%hd\n\n", PIDs[0], PIDs[1], PIDs[2], PIDs[3]);

  in = fopen(DemuxInFile, "rb");
  for (i = 0; i < 4; i++)
  {
    char OutFile[FBLIB_DIR_SIZE];
    snprintf(OutFile, sizeof(OutFile), "%s_%s.pes", DemuxOut, Types[i]);
    out[i] = fopen(OutFile, "wb");
  }

  GetPacketSize(in, &FileOffset);
  rewind(in);

  PSBuffer_Init(&Streams[0], PIDs[0], VIDEOBUFSIZE, FALSE);
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
#ifdef _DEBUG
    printf("Max. PES length (PID=%hd): %u\n", Streams[i].PID, Streams[i].maxPESLen);
#endif
    fclose(out[i]);
    if(Streams[i].ErrorFlag) ret = 2;
    PSBuffer_Reset(&Streams[i]);
  }
  fclose(in);
  exit(ret);
} */

/* // Humax-Fix (falsche PMT-Pid und ServiceInfo in inf-Datei und Timestamps)
{
  const char           *RecFile = argv[1];
  FILE                 *fInf, *fRec, *fHumax;
  byte                  Buffer1[192], *Buffer2 = NULL;
  tTSPacket            *Packet = NULL;
  tTSPAT               *PAT = NULL;
  TYPE_RecHeader_TMSS  *RecInf = NULL;
  struct stat64         statbuf = {0};
  word                  PMTPid = 0;
  int                   InfSize = 0;
  time_t                RecDate = 0;
  char                  InfFile[FBLIB_DIR_SIZE], NavFile[FBLIB_DIR_SIZE], SrtFile[FBLIB_DIR_SIZE], CutFile[FBLIB_DIR_SIZE], HumaxFile[FBLIB_DIR_SIZE];
  char                  ServiceName[32], ServiceNameF[32], *p;
  bool                  ChangedInf = FALSE;
  bool                  ret = FALSE;

  Buffer2 = (byte*)malloc(1048576);
  memset(Buffer1, 0, sizeof(Buffer1));
  memset(Buffer2, 0x77, 1048576);
  memset(ServiceName, 0, sizeof(ServiceName));

  printf("\n- Time- & Humax-Fix -\n");
  if (argc <= 1)
  {
    printf("\nUsage: TimeFix <Input.ts>\n");
    exit(1);
  }
  printf("\nInput File: %s\n", RecFile);

  strcpy(InfFile, RecFile); strcpy(NavFile, RecFile); strcpy(CutFile, RecFile); strcpy(SrtFile, RecFile); strcpy(HumaxFile, RecFile);
  if ((p = strrchr(CutFile, '.')))  p[0] = '\0';
  if ((p = strrchr(SrtFile, '.')))  p[0] = '\0';
  if ((p = strrchr(HumaxFile, '.')))  p[0] = '\0';
  strcat(InfFile, ".inf"); strcat(NavFile, ".nav"); strcat(CutFile, ".cut"); strcat(SrtFile, ".srt"); strcat(HumaxFile, ".humax"); 

  if ((fInf = fopen(InfFile, "rb")))
  {
    if ((InfSize = fread(Buffer2, 1, 1048576, fInf)))
      RecInf = (TYPE_RecHeader_TMSS*) Buffer2;
    fclose(fInf);
  }
  else
    printf("ERROR reading inf file!\n");

  if (RecInf)
  {
/*    if(stat64(RecFile, &statbuf) == 0)
    {
      if(statbuf.st_ctime > 1598961600)  // 01.09.2020 12:00 GMT
      {
        if ((fRec = fopen(RecFile, "rb")))
        {
          if (fread(Buffer1, sizeof(tTSPAT) + 9, 1, fRec))
          {
            Packet = (tTSPacket*) &Buffer1[4];
            PAT = (tTSPAT*) &Packet->Data[1];
            PMTPid = 256 * PAT->PMTPID1 + PAT->PMTPID2;
          }
          fclose(fRec);
        }
        else
          printf("ERROR reading rec file!\n");
      }
    } *//*

    if ((fHumax = fopen(HumaxFile, "rb")))
    {
      fseek(fHumax, 96, SEEK_SET);
      if (fread(ServiceNameF, 1, sizeof(ServiceName), fHumax))
      {
        p = strrchr(ServiceNameF, '_');
        if(p) *p = '\0';
      }
      fseek(fHumax, HumaxHeaderLaenge + 96, SEEK_SET);
      if (fread(ServiceName, 1, sizeof(ServiceName), fHumax))
      {
        p = strrchr(ServiceName, '_');
        if(p) *p = '\0';
      }
      if (strcmp(ServiceName, ServiceNameF) == 0)
        ServiceName[0] = '\0';
      fclose(fHumax);
    }

    if (PAT)
    {
      if (RecInf->ServiceInfo.PMTPID == 256 && PMTPid == 64)
      {
        printf("  Change PMTPid in inf: %hd to %hd", RecInf->ServiceInfo.PMTPID, PMTPid);
        RecInf->ServiceInfo.PMTPID = PMTPid;
        ChangedInf = TRUE;
      }
    }
    if (*ServiceName)
    {
      if (*ServiceName && strcmp(RecInf->ServiceInfo.ServiceName, ServiceName) != 0)
      {
        printf("  Change ServiceName in inf: '%s' to '%s'", RecInf->ServiceInfo.ServiceName, ServiceName);
        strncpy(RecInf->ServiceInfo.ServiceName, ServiceName, sizeof(RecInf->ServiceInfo.ServiceName));
        RecInf->ServiceInfo.ServiceName[sizeof(RecInf->ServiceInfo.ServiceName)-1] = '\0';
        ChangedInf = TRUE;
      }
/*      if (*ServiceNameF && !*RecInf->EventInfo.EventNameDescription)
      {
        printf("  Change EventName in inf to: '%s'", ServiceNameF);
        strncpy(RecInf->EventInfo.EventNameDescription, ServiceNameF, sizeof(RecInf->EventInfo.EventNameDescription) - 1);
        RecInf->EventInfo.EventNameLength = strlen(RecInf->EventInfo.EventNameDescription);
        ChangedInf = TRUE;
      } *//*
    }
    if (RecInf->ExtEventInfo.TextLength > 1024)
    {
      printf("  Change ExtEventText length in inf: %hd to %hd", RecInf->ExtEventInfo.TextLength, 1024);
      memset(&Buffer2[0x570], 0, InfSize - 0x570);
      RecInf->ExtEventInfo.TextLength = 1024;
      ChangedInf = TRUE;
    }

/*    // Convert EPG Event to UTC
    {
      tPVRTime EvtStart = RecInf->EventInfo.StartTime;
      tPVRTime EvtEnd   = RecInf->EventInfo.EndTime;
      
      if (EvtStart != 0 && EvtEnd != 0)
      {
        printf("  Change EvtStart from %s", TimeStrTF(EvtStart));
        EvtStart = Unix2TFTime(TF2UnixTime(EvtStart, 0, TRUE), 0, FALSE);
        EvtEnd   = Unix2TFTime(TF2UnixTime(EvtEnd, 0, TRUE), 0, FALSE);
        printf(" to %s\n", TimeStrTF(EvtStart));

        RecInf->EventInfo.StartTime = EvtStart;
        RecInf->EventInfo.EndTime = EvtEnd;
        ChangedInf = TRUE;
      }
    } *//*

    if (ChangedInf)
    {
      remove(InfFile);
      if ((fInf = fopen(InfFile, "wb")))
      {
        if (fwrite(Buffer2, 1, InfSize, fInf) == InfSize)
          printf(" -> SUCCESS\n");
        else
          printf(" -> ERROR\n");
        fclose(fInf);
        ChangedInf = TRUE;
      }
      else
      {
        ChangedInf = FALSE;
        printf(" -> ERROR\n");
      }
    }

    RecDate = TF2UnixTime(RecInf->RecHeaderInfo.StartTime, RecInf->RecHeaderInfo.StartTimeSec, TRUE);
//    if (stat64(RecFile, &statbuf) == 0)
    {
      if (ChangedInf || HDD_GetFileDateTime(RecFile) != RecDate)
      {
        printf("  Change file timestamp to: %s\n", TimeStrTF(RecInf->RecHeaderInfo.StartTime, RecInf->RecHeaderInfo.StartTimeSec));
//        HDD_SetFileDateTime(RecFile, RecDate);
        HDD_SetFileDateTime(InfFile, RecDate);
//        HDD_SetFileDateTime(NavFile, RecDate);
//        HDD_SetFileDateTime(SrtFile, RecDate);
      }
    }
    ret = TRUE;
  }
  printf("Finished.\n");
  free(Buffer2);
  exit(0);
} */


/* {
  byte Buffer[192], Buffer2[384];
  FILE *fPMT = NULL;
  TYPE_RecHeader_TMSS RecInf;
  int i;
  memset(AudioPIDs, 0, MAXCONTINUITYPIDS * sizeof(tAudioTrack));
  InitInfStruct(&RecInf);

//  if ((fPMT = fopen("D:/Test/pmts/28396_1601_1602_2008-12-25_17-00_EinsFestivalHD_out.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/28385_1201_1202_2022-12-01_13-09_Radio Bremen TV.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/17501_511_33_2022-11-28_10-17_ProSieben.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/28724_401_402_2022-12-01_14-19_arte.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/10302_5111_5112_2020-02-23_19-27_arte HD.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/28006_110_120_2022-11-28_11-09_ZDF.pmt", "rb")))
  if ((fPMT = fopen("D:/Test/pmts/pmts2/28006_110_120.pmt", "rb")))
//  if ((fPMT = fopen("D:/Test/pmts/28006_110_120_2022-11-28_11-09_ZDF_out.pmt", "rb")))
  {
    fseek(fPMT, 192, SEEK_SET);
    fread(Buffer, 1, 192, fPMT);
    AnalysePMT(&Buffer[9], 192, &RecInf);
    fclose(fPMT);
  }

  for (i = 0; i < MAXCONTINUITYPIDS; i++)
    if(AudioPIDs[i].pid) AudioPIDs[i].scanned = TRUE; else break;

  GeneratePatPmt(Buffer2, RecInf.ServiceInfo.ServiceID, 100, VideoPID, VideoPID, AudioPIDs[0].pid, TeletextPID, SubtitlesPID, AudioPIDs, FALSE);
//  if ((fPMT = fopen("D:/Test/pmts/28396_1601_1602_2008-12-25_17-00_EinsFestivalHD_out2.pmt", "wb")))
//  if ((fPMT = fopen("D:/Test/pmts/28385_1201_1202_2022-12-01_13-09_Radio Bremen TV_out.pmt", "wb")))
//  if ((fPMT = fopen("D:/Test/pmts/17501_511_33_2022-11-28_10-17_ProSieben_out.pmt", "wb")))
//  if ((fPMT = fopen("D:/Test/pmts/28724_401_402_2022-12-01_14-19_arte_out.pmt", "wb")))
//  if ((fPMT = fopen("D:/Test/pmts/10302_5111_5112_2020-02-23_19-27_arte HD_out.pmt", "wb")))
  if ((fPMT = fopen("D:/Test/pmts/28006_110_120_2022-11-28_11-09_ZDF_out2.pmt", "wb")))
  {
    fwrite(Buffer2, 1, 384, fPMT);
    fclose(fPMT);
  }
  exit(1);
} */


  // Eingabe-Parameter prüfen
  if (argc <= 1)  AbortProcess = TRUE;
  ExePath = argv[0];
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
      case 't':   if(argv[1][2] == 't')
                  {
                    int newPage = 0;
                    ExtractTeletext = TRUE;
                    if ((argc > 2) && (argv[2][0] != '-') && (strlen(argv[2]) <= 3) && ((newPage = strtol(argv[2], NULL, 16))))
                    {
                      TeletextPage = (word)newPage;
                      argv[1] = argv[0];
                      argv++;
                      argc--;
                    }
                  }
                  else if(argv[1][2] == 'x')
                    ExtractAllTeletext = TRUE;
                  else RemoveTeletext = TRUE;
                  break;
      case 'x':   RemoveScrambled = TRUE; break;
      case 'o':   OutPacketSize   = (argv[1][2] == '1') ? 188 : 192; break;
      case 'M':   MedionMode = TRUE;      break;
      case 'f':   DoInfFix = 1;           break;
      case 'p':   DoFixPMT = TRUE;        break;
      case 'd':   DemuxAudio = TRUE;      break;
      case 'v':   DoInfoOnly = TRUE;      break;
      case 'h':
      case '?':   ret = FALSE; AbortProcess = TRUE; break;
      default:    printf("\nUnknown argument: -%c\n", argv[1][1]);
                  ret = FALSE; AbortProcess = TRUE;  // Show help text
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
    if (*RecFileOut)
    {
      if (HDD_DirExist(RecFileOut))
        strncpy(OutDir, RecFileOut, sizeof(OutDir));
      else
        { printf("\nOutput folder '%s' does not exist.\nPlease specify an existing folder, or omit it to use current directory!\n", RecFileOut);  ret = FALSE; }
    }
    else
      OutDir[0] = '\0';  // use current dir
    GetNextFreeCutName(RecFileIn, RecFileOut, OutDir);
  }
  else OutDir[0] = '\0';

  if (!*RecFileIn)
    { printf("\nNo input file specified!\n");  ret = FALSE; }
  else if ((!DoInfoOnly || DoFixPMT) && (DoCut || DoStrip || DoMerge || DoFixPMT || OutPacketSize) && (!*RecFileOut || strcmp(RecFileIn, RecFileOut)==0))
    { printf("\nNo output file specified or output same as input!\n");  ret = FALSE; }
  else if (DoMerge && DoCut==2)
    { printf("\nMerging cannot be used together with cut mode (single segment copy)!\n");  ret = FALSE; }
  else if (DoMerge==1 && OutPacketSize)
    { printf("\nPacketSize cannot be changed when appending to an existing recording!\n");  ret = FALSE; }
  else if (DoSkip && (DoCut || DoMerge))
    { printf("\nSkipping of stripped recordings cannot be combined with -r, -c, -a, -m!\n");  DoSkip = FALSE; }
  if (DoInfoOnly && (DoStrip || DoCut || DoMerge || RebuildNav || RebuildInf || ExtractTeletext))
    { printf("\nView info only (-v) disables any other option!\n"); }
  if (!DoInfoOnly && DoFixPMT && (DoStrip || DoCut || DoMerge || RebuildNav || RebuildInf || ExtractTeletext))
    { printf("\nFix PAT/PMT (-p) disables any other option!\n"); }
  if (MedionMode==1 && DoStrip)
    { MedionStrip = TRUE; DoStrip = FALSE; }
//  if (ExtractTeletext && DoStrip)
//    { RemoveTeletext = TRUE; }
  #ifndef LINUX
    if (DoStrip || DoMerge || RemoveEPGStream || RemoveTeletext || OutPacketSize)  OutCutVersion = 4;
  #endif

  if (!ret)
  {
    if (AbortProcess)
    {
      printf("\nUsage:\n------\n");
      printf(" RecStrip <RecFile>           Scan the rec file and set Crypt- and RbN-Flag and\n"
             "                              StartTime (seconds) in the source inf.\n"
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
      printf("  -tt <page> Extract subtitles from teletext. (combine with -t to remove ttx)\n\n");
      printf("  -tx        Extract all teletext pages as text. (requires 10 MB of RAM)\n\n");
      printf("  -x:        Remove packets marked as scrambled. (flag could be wrong!)\n\n");
      printf("  -o1/-o2:   Change the packet size for output-rec: \n"
             "             1: PacketSize = 188 Bytes, 2: PacketSize = 192 Bytes.\n\n");
      printf("  -v:        View rec information only. Disables any other option.\n\n");
      printf("  -p:        Fix PAT/PMT of output file. Disables any other option.\n\n");
      printf("  -f:        Fix start time in source inf. Set source-file timestamps.\n\n");
      printf("  -d:        Demux first audio track to OutFile_audio.pes (not with -M)\n\n");
      printf("  -M:        Medion Mode: Multiplexes 4 separate PES-Files into output.\n");
      printf("             (With InFile=<name>_video.pes, _audio1, _ttx, _epg are used.)\n");
      printf("\nExamples:\n---------\n");
      printf("  RecStrip 'RecFile.rec'                     RebuildNav.\n\n");
      printf("  RecStrip -s -e InFile.rec OutFile.rec      Strip recording.\n\n");
      printf("  RecStrip -n -i -o2 InFile.ts OutFile.rec   Convert TS to Topfield rec.\n\n");
      printf("  RecStrip -r -s -e -o1 InRec.rec OutMpg.ts  Strip & cut rec and convert to TS.\n\n");
    }
    else if (DoInfoOnly)
    {
      fprintf(stderr, "RecFile\tRecSize\tFileDate\tStartTime\tDuration\tNrFrames\tFirstPCR\tLastPCR\tFirstPTS\tLastPTS\tisStripped\t");
      fprintf(stderr, "InfType\tSender\tServiceID\tPMTPid\tVideoPid\tAudioPid\tTtxPid\tVideoType\tAudioType\tAudioTypeFlag\tHD\tResolution\tFPS\tAspectRatio\tTtxSubPage\t");
      fprintf(stderr, "SegmentMarker\tBookmarks\t");
      fprintf(stderr, "EventName\tEventDesc\tEventStart\tEventEnd\tEventDuration\tExtEventText\n");
    }
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

  PATPMTBuf = (byte*) malloc(4*192 + 5);
  if (!PATPMTBuf)
  {
    printf("ERROR: Memory allocation failed.\n");
    CutProcessor_Free();
    InfProcessor_Free();
    TRACEEXIT;
    exit(2);
  }
  memset(PATPMTBuf, 0, 4*192 + 5);

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
      free(PATPMTBuf); PATPMTBuf = NULL;
      TRACEEXIT;
      exit(2);
    }
  }

  if (ExtractAllTeletext)
    TtxProcessor_Init(0);

  // Variablen initialisieren
  if (DoMerge)
  {
    char Temp[FBLIB_DIR_SIZE];
    strcpy(Temp, RecFileIn);
    strcpy(RecFileIn, RecFileOut);
    strcpy(RecFileOut, Temp);
    NrInputFiles = argc;
  }
//  InfFileOld[0] = '\0';  // Müssten nicht alle OutFiles mit initialisiert werden?
//  HasNavOld = FALSE;  // So muss sichergestellt sein, dass CloseOutputFiles() nur nach OpenOutputFiles() aufgerufen wird!

  // Prüfen, ob Aufnahme bereits gestrippt
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
      TtxProcessor_Free();
      if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
      if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
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
      TtxProcessor_Free();
      if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
      if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
      if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
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
//      CutTimeOffset = -(int)InfDuration * 1000;
    if ((int)LastTimems > -CutTimeOffset)
      CutTimeOffset = -(int)LastTimems;
    if(ExtractTeletext) last_timestamp = -CutTimeOffset;
    NewStartTimeOffset = 0;
    CloseInputFiles(FALSE, FALSE, FALSE);
  }

  // Input-Files öffnen
  if (!OpenInputFiles(RecFileIn, (DoMerge != 1)))
  {
    CutProcessor_Free();
    InfProcessor_Free();
    TtxProcessor_Free();
    if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
    if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    if(ExtEPGText) { free(ExtEPGText); ExtEPGText = NULL; }
    printf("ERROR: Cannot open input %s.\n", RecFileIn);
    TRACEEXIT;
    exit(4);
  }

  if (!VideoPID || VideoPID == (word)-1)
  {
    printf("Warning: No video PID determined.\n");
/*    CloseInputFiles(FALSE, FALSE, FALSE);
    CutProcessor_Free();
    InfProcessor_Free();
    TtxProcessor_Free();
    if(PendingBuf) { free(PendingBuf); PendingBuf = NULL };
    if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
    if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    if(ExtEPGText) { free(ExtEPGText); ExtEPGText = NULL; }
    TRACEEXIT;
    exit(6);  */
  }

  // Spezialanpassung Humax / Medion
  if ((HumaxSource || EycosSource || MedionMode==1 || (WriteDescPackets && (DoFixPMT || PATPMTBuf[4]!='G'))) && (!DoInfoOnly || DoFixPMT) /*&& fOut && DoMerge != 1*/)
  {
    bool pmt_used = FALSE;

// Lese Original-PMT aus "Datenbank" ein (experimentell)
#if defined(_WIN32) && defined(_DEBUG)
//    if (!DoFixPMT)
    {
      FILE *fPMT = NULL;
      char ServiceString[128], *p;

      strncpy(ServiceString, ExePath, sizeof(ServiceString) - 1);
      ServiceString[sizeof(ServiceString) - 1] = '\0';
      if ((p = strrchr(ServiceString, '/'))) p[1] = '\0';
      else if ((p = strrchr(ServiceString, '\\'))) p[1] = '\0';
      else ServiceString[0] = 0;
      n = (int)strlen(ServiceString);

      snprintf(&ServiceString[n], sizeof(ServiceString) - n, "pmts/%hu_%hd_%hd.pmt", ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.ServiceID, VideoPID, GetMinimalAudioPID(AudioPIDs));
      if (HDD_FileExist(ServiceString))
      {
        printf("\nUsing PAT/PMT %s from database.\n", ServiceString);
        if ((fPMT = fopen(ServiceString, "rb")))
        {
          if (fread(PATPMTBuf, 1, 4*192, fPMT) >= 2*192)
          {
            for (k = 0; k < 4; k++)
              ((tTSPacket*)(&PATPMTBuf[k*192 + 4]))->ContinuityCount = (k==0 ? 0 : k-1);
            ((tTSPMT*)(&PATPMTBuf[201]))->CurNextInd = 1;
            ((tTSPMT*)(&PATPMTBuf[201]))->VersionNr = 1;
//            ((tTSPMT*)(&PATPMTBuf[200 + (((tTSPacket*)(&PATPMTBuf[196]))->Adapt_Field_Exists ? PATPMTBuf[200] : 0) + 1]))->CurNextInd = 1;
//            ((tTSPMT*)(&PATPMTBuf[200 + (((tTSPacket*)(&PATPMTBuf[196]))->Adapt_Field_Exists ? PATPMTBuf[200] : 0) + 1]))->VersionNr = 1;
          
            ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.PMTPID = ((tTSPacket*)(&PATPMTBuf[196]))->PID1 * 256 + ((tTSPacket*)(&PATPMTBuf[196]))->PID2;

            pmt_used = TRUE;
            DoInfFix = TRUE;
          }
          fclose(fPMT);
        }
      }
    }
#endif
    if (/*DoFixPMT ||*/ MedionMode || !pmt_used)
    {
      printf("Generate new %s for Humax/Medion/Eycos recording.\n", ((PATPMTBuf[192+4]=='G') ? "PAT" : "PAT/PMT"));
//      GeneratePatPmt(PATPMTBuf, ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.ServiceID, 256, VideoPID, VideoPID, 101, TeletextPID, AudioPIDs);
      GeneratePatPmt(PATPMTBuf, ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.ServiceID, ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.PMTPID, VideoPID, VideoPID, AudioPIDs, (PATPMTBuf[192+4]=='G'));
    }

/*    if (MedionMode == 1)
    {
      AnalysePMT(&PATPMTBuf[201], sizeof(PATPMTBuf) - 201, (TYPE_RecHeader_TMSS*)InfBuffer);
      NrContinuityPIDs = 0;
    } */
    printf("\n");

    if (HumaxSource && !DoInfoOnly)
    {
      char HumaxFile[FBLIB_DIR_SIZE + 6], *p;
      strcpy(HumaxFile, RecFileOut);
      if((p = strrchr(HumaxFile, '.'))) *p = '\0';  // ".rec" entfernen
      strcat(HumaxFile, ".humax");
      SaveHumaxHeader(RecFileIn, HumaxFile);
    }
  }

  // Hier beenden, wenn View Info Only
  if (DoInfoOnly)
  {
    TYPE_RecHeader_TMSS *Inf_TMSS = (TYPE_RecHeader_TMSS*)InfBuffer;
    char                EventName[257], DurationStr[16], FileDateStr[20], StartTimeStr[20];
    byte                FileSec;
    tPVRTime            FileDate = Unix2TFTime(RecFileTimeStamp, &FileSec, TRUE);

    // Print out details to STDERR
    memset(EventName, 0, sizeof(EventName));

    if (NavDurationMS)
      snprintf(DurationStr, sizeof(DurationStr), "%02d:%02u:%02u.%03u", NavDurationMS/3600000, abs(NavDurationMS/60000) % 60, abs(NavDurationMS/1000) % 60, abs(NavDurationMS) % 1000);
    else if (FirstFilePTS && LastFilePTS)
    {
      int dPTS = DeltaPCR(FirstFilePTS, LastFilePTS) / 45;
      snprintf(DurationStr, sizeof(DurationStr), "%02d:%02u:%02u.%03u", dPTS/3600000, abs(dPTS/60000) % 60, abs(dPTS/1000) % 60, abs(dPTS) % 1000);
    }
    else
      snprintf(DurationStr, sizeof(DurationStr), "%02hu:%02hu:%02hu", Inf_TMSS->RecHeaderInfo.DurationMin/60, Inf_TMSS->RecHeaderInfo.DurationMin % 60, Inf_TMSS->RecHeaderInfo.DurationSec);
    strncpy(EventName, Inf_TMSS->EventInfo.EventNameDescription, Inf_TMSS->EventInfo.EventNameLength);

    // REC:    RecFileIn;  RecSize;  FileDate;  StartTime (DateTime);  Duration (nav=hh:mm:ss.xxx, TS=hh:mm:ss);  NrFrames;  FirstPCR;  LastPCR;  FirstPTS;  LastPTS;  isStripped
    strncpy(FileDateStr, TimeStr_DB(FileDate, FileSec), sizeof(FileDateStr));
    strncpy(StartTimeStr, TimeStr_DB(Inf_TMSS->RecHeaderInfo.StartTime, Inf_TMSS->RecHeaderInfo.StartTimeSec), sizeof(StartTimeStr));
    fprintf(stderr, "%s\t%llu\t%s\t%s\t%s\t%u\t%lld\t%lld\t%u\t%u\t%s\t",  RecFileIn,  RecFileSize,  FileDateStr,  StartTimeStr,  DurationStr,  NavFrames,  FirstFilePCR,  LastFilePCR,  FirstFilePTS,  LastFilePTS,  (Inf_TMSS->RecHeaderInfo.rs_HasBeenStripped ? "yes" : "no"));

    // SERVICE:  InfType;   Sender;   ServiceID;  PMTPid;  VideoPid;  AudioPid;  TtxPid;  VideoType;  AudioType;  AudioTypeFlag;  HD;  VideoWidth x VideoHeight;  VideoFPS;  VideoDAR;  TtxSubtPage; 
    fprintf(stderr, "ST_TMS%c\t%s\t%hu\t%hd\t%hd\t%hd\t%hd\t0x%hx\t0x%hx\t0x%hx\t%s\t%dx%d\t%.1f fps\t%.3f\t%hx\t",  (SystemType==ST_TMSS ? 's' : ((SystemType==ST_TMSC) ? 'c' : ((SystemType==ST_TMST) ? 't' : '?'))),  Inf_TMSS->ServiceInfo.ServiceName,  Inf_TMSS->ServiceInfo.ServiceID,  Inf_TMSS->ServiceInfo.PMTPID,  Inf_TMSS->ServiceInfo.VideoPID,  Inf_TMSS->ServiceInfo.AudioPID,  TeletextPID,  Inf_TMSS->ServiceInfo.VideoStreamType,  Inf_TMSS->ServiceInfo.AudioStreamType,  Inf_TMSS->ServiceInfo.AudioTypeFlag,  (isHDVideo ? "yes" : "no"),  VideoWidth,  VideoHeight,  (VideoFPS ? VideoFPS : (NavFrames ? NavFrames/((double)NavDurationMS/1000) : 0)),  VideoDAR,  TeletextPage);

    // SEGMENTMARKERS (getrennt durch ; und |)
    if (NrSegmentMarker > 2)
    {
      int p; char *c;

      fprintf(stderr, "{");
      for (p = 0; p < NrSegmentMarker; p++)
      {
        float Percent = (float)(((float)SegmentMarker[p].Position / RecFileSize) * 100.0);
        snprintf(DurationStr, sizeof(DurationStr), "%u:%02u:%02u.%03u", SegmentMarker[p].Timems/3600000, SegmentMarker[p].Timems/60000 % 60, SegmentMarker[p].Timems/1000 % 60, SegmentMarker[p].Timems % 1000);

        // Ersetze eventuelles '\t', ' | ' in der Caption
        if (SegmentMarker[p].pCaption)
          for (c = SegmentMarker[p].pCaption; *c != '\0'; c++)
          {
            if (c[0]=='\t' /*&& c[1]=='}'*/) c[0] = ' ';
            else if (c[0]==' ' && c[1]=='|' && c[2]==' ') c[1] = ',';
          }
        fprintf(stderr, "%s%s; %lld; %u; %s; %.1f%%; %s", ((p > 0) ? " | " : ""), (SegmentMarker[p].Selected ? "*" : "-"), ((OutCutVersion>=4) ? SegmentMarker[p].Position : 0), ((OutCutVersion<=3) ? CalcBlockSize(SegmentMarker[p].Position) : 0), DurationStr, Percent, (SegmentMarker[p].pCaption ? SegmentMarker[p].pCaption : ""));
      }
      fprintf(stderr, "}");
    }
    fprintf(stderr, "\t");

    // BOOKMARKS (TimeStamps berechnen)
    if (BookmarkInfo->NrBookmarks > 0)
    {
      int NrTimeStamps, p;
      tTimeStamp2 *TimeStamps = NULL;

      TimeStamps = NavLoad(RecFileIn, &NrTimeStamps, PACKETSIZE);  // Erzeugt Fehlermeldung, wenn nav-File nicht existiert!
      fprintf(stderr, "{");

      for (p = 0; p < (int)BookmarkInfo->NrBookmarks; p++)
      {
        if (TimeStamps)
        {
          dword Timems = NavGetPosTimeStamp(TimeStamps, NrTimeStamps, BookmarkInfo->Bookmarks[p] * 9024LL);
          snprintf(DurationStr, sizeof(DurationStr), "%u:%02u:%02u.%03u", Timems/3600000, Timems/60000 % 60, Timems/1000 % 60, Timems % 1000);
          fprintf(stderr, ((p > 0) ? " | %u; %s" : "%u; %s"), BookmarkInfo->Bookmarks[p], DurationStr);
        }
        else
          fprintf(stderr, ((p > 0) ? " | %u" : "%u"), BookmarkInfo->Bookmarks[p]);
      }

      if(TimeStamps) free(TimeStamps);
      fprintf(stderr, "}");
    }
    fprintf(stderr, "\t");

    // EPG:    EventName;  EventDesc;  EventStart (DateTime);  EventEnd (DateTime);  EventDuration (hh:mm);  ExtEventText (inkl. ItemizedItems, ohne '\n', '\t')
    Inf_TMSS->EventInfo.EventNameDescription[sizeof(Inf_TMSS->EventInfo.EventNameDescription) - 1] = '\0';
    Inf_TMSS->ExtEventInfo.Text[max(Inf_TMSS->ExtEventInfo.TextLength, sizeof(Inf_TMSS->ExtEventInfo.Text) - 1)] = '\0';
    fprintf(stderr, "%s\t%s\t", EventName,  &Inf_TMSS->EventInfo.EventNameDescription[Inf_TMSS->EventInfo.EventNameLength]);
    if (Inf_TMSS->EventInfo.StartTime != 0)
    {
      fprintf(stderr, "%s\t", TimeStr_DB(EPG2TFTime(Inf_TMSS->EventInfo.StartTime, NULL), 0));
      fprintf(stderr, "%s\t%02hhu:%02hhu\t%s\n", TimeStr_DB(EPG2TFTime(Inf_TMSS->EventInfo.EndTime, NULL), 0),  Inf_TMSS->EventInfo.DurationHour,  Inf_TMSS->EventInfo.DurationMin,  (ExtEPGText ? ExtEPGText : Inf_TMSS->ExtEventInfo.Text));
    }
    else
      fprintf(stderr, "\t\t\t%s\n",  (ExtEPGText ? ExtEPGText : ""));
  }
  if(ExtEPGText) { free(ExtEPGText); ExtEPGText = NULL; }


  // SPECIAL FEATURE: Fix PAT/PMT of output file (-p)
  if (DoFixPMT && (HumaxSource || EycosSource || MedionMode==1 || WriteDescPackets))
  {
    byte PMTPacket[192];
    time_t OldOutTimestamp = HDD_GetFileDateTime(RecFileOut);

    if (*RecFileOut)
    {
      fclose(fIn); fIn = NULL;
      CloseNavFileIn();
      printf("\nOutput rec: %s\n", RecFileOut);
      if ((fOut = fopen(RecFileOut, "r+b")))
      {
        OutPacketSize = GetPacketSize(fOut, NULL);
        fseeko64(fOut, 0, SEEK_SET);

        for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
        {
          if (fread(PMTPacket, OutPacketSize, 1, fOut) == 1)
          {
/*            int m;
            for (m = 0; m < OutPacketSize; m++) {
              printf("k=%d:  New PAT: 0x%2.2x - 0x%2.2x Cur PAT\n", k, PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192 + m], PMTPacket[m]);
              if (PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192 + m] != PMTPacket[m])
                DoFixPMT = 2;
            } */

            if (memcmp(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192], PMTPacket, OutPacketSize) != 0)
              DoFixPMT = 2;
          }
        }

        if (DoFixPMT == 2)
        {
          printf("\nFixing PAT/PMT packets of output rec.\n");
          fseeko64(fOut, 0, SEEK_SET);
          for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
            fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192], OutPacketSize, 1, fOut);
          fclose(fOut); fOut = NULL;
          HDD_SetFileDateTime(RecFileOut, OldOutTimestamp);
        }
        else
          printf("\nNo fix of PAT/PMT (output) necessary.\n");
      }
      else
      {
        printf("ERROR: Output file does not exist %s.\n", RecFileOut);
        if(MedionMode == 1) SimpleMuxer_Close();
        CutProcessor_Free();
        InfProcessor_Free();
        TtxProcessor_Free();
        if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
        if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
        if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
        if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
        if(ExtEPGText) { free(ExtEPGText); ExtEPGText = NULL; }
        TRACEEXIT;
        exit(7);
      }
    }
  }

  // SPECIAL UNDOCUMENTED FEATURE: Fix start-time in source inf (-f)?
  if (DoInfFix || DoFixPMT)
  {
    char                  NavFileIn[FBLIB_DIR_SIZE];
    time_t                RecDate;
    bool                  InfModified = FALSE, BookmarkFix = FALSE;

    snprintf(InfFileOut, sizeof(InfFileOut), "%s.inf", RecFileOut);
    snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
    RecDate = TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE);

    // Prüfe/Repariere (nur!) Startzeit in der inf
    if ((DoInfFix == 2) && (DoFixPMT ? *InfFileOut : *InfFileIn))
    {
      printf("INF FIX (%s): Changing StartTime to: %s\n", (DoFixPMT ? "output" : "source"), TimeStrTF(OrigStartTime, OrigStartSec));
//      SetInfStripFlags(InfFileIn, FALSE, FALSE, TRUE);
      SetInfStripFlags((DoFixPMT ? InfFileOut : InfFileIn), FALSE, FALSE, TRUE);
    }
    else if (!DoFixPMT)
      printf("No fix of (%s) inf necessary.\n", (DoFixPMT ? "output" : "source"));

    // Prüfe/Repariere ServiceID, ServiceName, AudioTypes und Bookmarks in der inf
    if (DoInfFix && (DoFixPMT ? *InfFileOut : *InfFileIn))
    {
      FILE                 *fInfOut, *fInfIn;
      char                  InfFileStripped[FBLIB_DIR_SIZE];
      TYPE_RecHeader_Info   RecHeaderInfo_out;
      TYPE_Service_Info     ServiceInfo_out;
      TYPE_Event_Info       EventInfo_out;
      TYPE_ExtEvent_Info    ExtEventInfo_out;
      TYPE_Bookmark_Info    BookmarkInfo_out;
      TYPE_RecHeader_TMSS*  RecHeader = ((TYPE_RecHeader_TMSS*)InfBuffer);

      if ((fInfOut = fopen((DoFixPMT ? InfFileOut : InfFileIn), "rb")))
      {
        if ((fread(&RecHeaderInfo_out, sizeof(TYPE_RecHeader_Info), 1, fInfOut)) && (fread(&ServiceInfo_out, sizeof(TYPE_Service_Info), 1, fInfOut)) && (fread(&EventInfo_out, sizeof(TYPE_Event_Info), 1, fInfOut)) && (fread(&ExtEventInfo_out, sizeof(TYPE_ExtEvent_Info), 1, fInfOut))
          && ((strncmp(RecHeaderInfo_out.Magic, "TFrc", 4) == 0) && (RecHeaderInfo_out.Version == 0x8000)))
        {
          if ((RecHeaderInfo_out.StartTime != OrigStartTime) || (OrigStartSec && (RecHeaderInfo_out.StartTimeSec != OrigStartSec)))
          {
            printf("INF FIX (%s): Fixing StartTime to %s\n", (DoFixPMT ? "output" : "source"), TimeStrTF(OrigStartTime, OrigStartSec));
            RecHeaderInfo_out.StartTime = OrigStartTime;
            RecHeaderInfo_out.StartTimeSec = OrigStartSec;
            InfModified = TRUE;
          }
          if (*RecHeader->ServiceInfo.ServiceName && (strncmp(ServiceInfo_out.ServiceName, RecHeader->ServiceInfo.ServiceName, sizeof(ServiceInfo_out.ServiceName)) != 0))
          {
            printf("INF FIX (%s): Fixing ServiceName %s -> %s\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.ServiceName, RecHeader->ServiceInfo.ServiceName);
            strncpy(ServiceInfo_out.ServiceName, RecHeader->ServiceInfo.ServiceName, sizeof(ServiceInfo_out.ServiceName));
            InfModified = TRUE;
          }
          if (RecHeader->ServiceInfo.ServiceID && (ServiceInfo_out.ServiceID != RecHeader->ServiceInfo.ServiceID))
          {
            printf("INF FIX (%s): Fixing ServiceID %hu -> %hu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.ServiceID, RecHeader->ServiceInfo.ServiceID);
            ServiceInfo_out.ServiceID = RecHeader->ServiceInfo.ServiceID;
            InfModified = TRUE;
          }
          if (RecHeader->ServiceInfo.PMTPID && (ServiceInfo_out.PMTPID != RecHeader->ServiceInfo.PMTPID))
          {
            printf("INF FIX (%s): Fixing PMTPID %hu -> %hu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.PMTPID, RecHeader->ServiceInfo.PMTPID);
            ServiceInfo_out.PMTPID = RecHeader->ServiceInfo.PMTPID;
            InfModified = TRUE;
          }

          if (!MedionMode)
          {
            /* if (RecHeader->ServiceInfo.VideoPID && (ServiceInfo_out.VideoPID != RecHeader->ServiceInfo.VideoPID))
            {
              printf("INF FIX (%s): Fixing VideoPID %hu -> %hu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.VideoPID, RecHeader->ServiceInfo.VideoPID);
              ServiceInfo_out.VideoPID = RecHeader->ServiceInfo.VideoPID;
              InfModified = TRUE;
            } */
            if (RecHeader->ServiceInfo.AudioPID && (ServiceInfo_out.AudioPID != RecHeader->ServiceInfo.AudioPID))
            {
              printf("INF FIX (%s): Fixing AudioPID %hu -> %hu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.AudioPID, RecHeader->ServiceInfo.AudioPID);
              ServiceInfo_out.AudioPID = RecHeader->ServiceInfo.AudioPID;
              InfModified = TRUE;
            }
          }
          if (RecHeader->ServiceInfo.VideoStreamType && (RecHeader->ServiceInfo.VideoStreamType != 0xff) && (ServiceInfo_out.VideoStreamType != RecHeader->ServiceInfo.VideoStreamType))
          {
            printf("INF FIX (%s): Fixing VideoStreamType %hhu -> %hhu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.VideoStreamType, RecHeader->ServiceInfo.VideoStreamType);
            ServiceInfo_out.VideoStreamType = RecHeader->ServiceInfo.VideoStreamType;
            InfModified = TRUE;
          }
          if (/*RecHeader->ServiceInfo.AudioStreamType &&*/ (RecHeader->ServiceInfo.AudioStreamType != 0xff) && (ServiceInfo_out.AudioStreamType != RecHeader->ServiceInfo.AudioStreamType))
          {
            printf("INF FIX (%s): Fixing AudioStreamType %hhu -> %hhu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.AudioStreamType, RecHeader->ServiceInfo.AudioStreamType);
            ServiceInfo_out.AudioStreamType = RecHeader->ServiceInfo.AudioStreamType;
            InfModified = TRUE;
          }
          if ((RecHeader->ServiceInfo.AudioTypeFlag != 3) && (ServiceInfo_out.AudioTypeFlag != RecHeader->ServiceInfo.AudioTypeFlag))
          {
            printf("INF FIX (%s): Fixing AudioTypeFlag %hu -> %hu\n", (DoFixPMT ? "output" : "source"), ServiceInfo_out.AudioTypeFlag, RecHeader->ServiceInfo.AudioTypeFlag);
            ServiceInfo_out.AudioTypeFlag = RecHeader->ServiceInfo.AudioTypeFlag;
            InfModified = TRUE;
          }

          if ((RecHeader->EventInfo.StartTime != 0) && (RecHeader->EventInfo.StartTime != EventInfo_out.StartTime))
          {
            printf("INF FIX (%s): Fixing EventInfo ('%s')\n", (DoFixPMT ? "output" : "source"), RecHeader->EventInfo.EventNameDescription);
            EventInfo_out.ServiceID       = RecHeader->EventInfo.ServiceID;
            EventInfo_out.EventID         = RecHeader->EventInfo.EventID;
            EventInfo_out.RunningStatus   = RecHeader->EventInfo.RunningStatus;
            EventInfo_out.StartTime       = RecHeader->EventInfo.StartTime;
            EventInfo_out.DurationHour    = RecHeader->EventInfo.DurationHour;
            EventInfo_out.DurationMin     = RecHeader->EventInfo.DurationMin;
            EventInfo_out.EventNameLength = RecHeader->EventInfo.EventNameLength;
            strncpy(EventInfo_out.EventNameDescription, RecHeader->EventInfo.EventNameDescription, sizeof(EventInfo_out.EventNameDescription));
            
            if (RecHeader->ExtEventInfo.TextLength > 0)
            {
              ExtEventInfo_out.ServiceID  = RecHeader->ExtEventInfo.ServiceID;
              ExtEventInfo_out.TextLength = RecHeader->ExtEventInfo.TextLength;
              ExtEventInfo_out.NrItemizedPairs = RecHeader->ExtEventInfo.NrItemizedPairs;
              strncpy(ExtEventInfo_out.Text, RecHeader->ExtEventInfo.Text, sizeof(ExtEventInfo_out.Text));
            }
            InfModified = TRUE;
          }

          // If Stripped inf provided -> read Bookmark and SegmentMarker area from stripped inf
          if (argc > 1)
          {
            if (strstr(argv[1], ".inf"))
              strncpy(InfFileStripped, argv[1], sizeof(InfFileStripped));
            else
              snprintf(InfFileStripped, sizeof(InfFileStripped), "%s.inf", argv[1]);
            if ((fInfIn = fopen(InfFileStripped, "rb")))
            {
              int Start = 0, End = 0, End_out = 0;

              if ((fseek(fInfIn, sizeof(TYPE_RecHeader_Info) + sizeof(TYPE_Service_Info) + sizeof(TYPE_Event_Info) + sizeof(TYPE_ExtEvent_Info) + sizeof(TYPE_TpInfo_TMSS), SEEK_SET) == 0)
               && fread(BookmarkInfo, sizeof(TYPE_Bookmark_Info), 1, fInfIn))
              {
                if ((BookmarkInfo->NrBookmarks > 0) && (BookmarkInfo->NrBookmarks <= NRBOOKMARKS))
                  printf("Read %u Bookmarks from stripped inf.\n", BookmarkInfo->NrBookmarks);

                if (BookmarkInfo->Bookmarks[NRBOOKMARKS - 2] == 0x8E0A4247)
                  End = NRBOOKMARKS - 2;
                else if (BookmarkInfo->Bookmarks[NRBOOKMARKS - 1] == 0x8E0A4247)
                  End = NRBOOKMARKS - 1;
                if (End && (BookmarkInfo->Bookmarks[End - 1] > 0))
                {
                  NrSegmentMarker = BookmarkInfo->Bookmarks[End - 1];
                  Start = End - NrSegmentMarker - 5;
                  printf("Read %u SegmentMarker from stripped inf.\n", NrSegmentMarker);
                }
              }
              fclose(fInfIn);

              // Read current Bookmarks and SegmentMarkers from to-be-fixed inf (optional)
              if ((fseek(fInfOut, sizeof(TYPE_RecHeader_Info) + sizeof(TYPE_Service_Info) + sizeof(TYPE_Event_Info) + sizeof(TYPE_ExtEvent_Info) + sizeof(TYPE_TpInfo_TMSS), SEEK_SET) == 0)
               && fread(&BookmarkInfo_out, sizeof(TYPE_Bookmark_Info), 1, fInfOut))
              {
                // If to-be-fixed inf has Bookmarks -> compare if they match with stripped inf (and fix if necessary)
                if ((BookmarkInfo_out.NrBookmarks > 0) && (BookmarkInfo_out.NrBookmarks <= NRBOOKMARKS) && (BookmarkInfo->NrBookmarks > 0))
                {
                  if (BookmarkInfo_out.NrBookmarks != BookmarkInfo->NrBookmarks)
                  {
                    printf("INF FIX (%s): Fixing number of bookmarks %u -> %u\n", (DoFixPMT ? "output" : "source"), BookmarkInfo_out.NrBookmarks, BookmarkInfo->NrBookmarks);
                    BookmarkFix = 1;
                  }
                  for (k = 0; k < (int)BookmarkInfo->NrBookmarks; k++)
                    if (BookmarkInfo_out.Bookmarks[k] != BookmarkInfo->Bookmarks[k])
                    {
                      printf("INF FIX (%s): Fixing Bookmark %d: %u -> %u\n", (DoFixPMT ? "output" : "source"), k+1, BookmarkInfo_out.Bookmarks[k], BookmarkInfo->Bookmarks[k]);
                      BookmarkFix = 1;
                    }
                }

                // If to-be-fixed inf has SegmentMarkers -> compare if they match with stripped inf (and fix if necessary)
                if (BookmarkInfo_out.Bookmarks[NRBOOKMARKS - 2] == 0x8E0A4247)
                  End_out = NRBOOKMARKS - 2;
                else if (BookmarkInfo_out.Bookmarks[NRBOOKMARKS - 1] == 0x8E0A4247)
                  End_out = NRBOOKMARKS - 1;
                if (End_out && (BookmarkInfo_out.Bookmarks[End_out - 1] > 0) && (NrSegmentMarker > 2))
                {
                  if ((End_out != End) || ((int)BookmarkInfo_out.Bookmarks[End_out - 1] != NrSegmentMarker) || (End_out - NrSegmentMarker - 5 != Start))
                  {
                    printf("INF FIX (%s): Rewriting SegmentMarker area %u -> %u\n", (DoFixPMT ? "output" : "source"), BookmarkInfo_out.Bookmarks[End_out - 1], NrSegmentMarker);
                    BookmarkFix = 2;
                  }
                  else
                  {
                    for (k = 0; k < NrSegmentMarker; k++)
                      if (BookmarkInfo_out.Bookmarks[Start + k] != BookmarkInfo->Bookmarks[Start + k])
                      {
                        printf("INF FIX (%s): Fixing Segment %d: %u -> %u\n", (DoFixPMT ? "output" : "source"), k+1, BookmarkInfo_out.Bookmarks[Start + k], BookmarkInfo->Bookmarks[Start + k]);
                        BookmarkFix = 2;
                      }
                  }
                }
              }
            }
          }
        }
        fclose(fInfOut);
      }

      // Write the actual fixes to inf
      if ((InfModified || BookmarkFix) && ((fInfOut = fopen((DoFixPMT ? InfFileOut : InfFileIn), "r+b"))))
      {
        if (InfModified)
        {
          fseek(fInfOut, 0, SEEK_SET);
          fwrite(&RecHeaderInfo_out, 1, 12, fInfOut);
          fseek(fInfOut, sizeof(TYPE_RecHeader_Info), SEEK_SET);
          fwrite(&ServiceInfo_out, 1, sizeof(TYPE_Service_Info), fInfOut);
          fwrite(&EventInfo_out, 1, sizeof(TYPE_Event_Info), fInfOut);
          fwrite(&ExtEventInfo_out, 1, sizeof(TYPE_ExtEvent_Info), fInfOut);
        }
        if (BookmarkFix)
        {
          fseek(fInfOut, sizeof(TYPE_RecHeader_Info) + sizeof(TYPE_Service_Info) + sizeof(TYPE_Event_Info) + sizeof(TYPE_ExtEvent_Info) + sizeof(TYPE_TpInfo_TMSS), SEEK_SET);
          fwrite(BookmarkInfo, 1, (BookmarkFix>=2) ? sizeof(TYPE_Bookmark_Info) : (BookmarkInfo->NrBookmarks+1) * 4, fInfOut);
        }
        fclose(fInfOut);
      }
    }

    if (DoInfFix == 2 || RecFileTimeStamp != RecDate || InfModified || BookmarkFix)
    {
      printf("INF FIX (%s): Changing file timestamp to: %s\n", (DoFixPMT ? "output" : "source"), TimeStrTF(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec));
      snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);

      if ((DoFixPMT ? *RecFileOut : *RecFileIn))
        HDD_SetFileDateTime((DoFixPMT ? RecFileOut : RecFileIn), TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
      if ((DoFixPMT ? *InfFileOut : *InfFileIn))
        HDD_SetFileDateTime((DoFixPMT ? InfFileOut : InfFileIn), TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
      if ((DoFixPMT ? *NavFileOut : *NavFileIn))
        HDD_SetFileDateTime((DoFixPMT ? NavFileOut : NavFileIn), TF2UnixTime(RecHeaderInfo->StartTime, RecHeaderInfo->StartTimeSec, TRUE));
    }
  }

  if (ExtractAllTeletext)
  {
    char AbsOutFile[FBLIB_DIR_SIZE], *p;
    int len;
    strncpy(AbsOutFile, (*RecFileOut) ? RecFileOut : RecFileIn, sizeof(AbsOutFile));
    if ((p = strrchr(AbsOutFile, '.')) != NULL)
      len = (int)(p - AbsOutFile);
    else
      len = (int)strlen(AbsOutFile);
    snprintf(&AbsOutFile[len], sizeof(AbsOutFile)-len, "%s", ".txt");
    WriteAllTeletext(AbsOutFile);
    TtxProcessor_Free();
    ExtractAllTeletext = FALSE;
  }

  // Hier beenden, wenn View Info Only
  if (DoInfoOnly || DoFixPMT)
  {
    if(fIn) { fclose(fIn); fIn = NULL; }
    CloseNavFileIn();
    if(MedionMode == 1) SimpleMuxer_Close();
    CutProcessor_Free();
    InfProcessor_Free();
    TtxProcessor_Free();
    if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
    if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
    if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    printf("\nRecStrip finished. View information / fix PMT only.\n");
    TRACEEXIT;
    exit(0);
  }

  TtxProcessor_Init(TeletextPage);
  if (ExtractTeletext && !MedionMode && TeletextPID == (word)-1)
  {
    printf("Warning: No teletext PID determined.\n");
    ExtractTeletext = FALSE;
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


  // Output-Files öffnen
  if (DoCut < 2 && !OpenOutputFiles())
  {
    fclose(fIn); fIn = NULL;
    CloseNavFileIn();
    if(MedionMode == 1) SimpleMuxer_Close();
    CutProcessor_Free();
    InfProcessor_Free();
    TtxProcessor_Free();
    if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
    if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
    if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    printf("ERROR: Cannot write output %s.\n", RecFileOut);
    TRACEEXIT;
    exit(7);
  }

  // Wenn Appending, ans Ende der nav-Datei springen
  if(DoMerge == 1) GoToEndOfNav(NULL);

  if(!RebuildNav && *RecFileOut && HasNavIn)  SetFirstPacketAfterBreak();


  // -----------------------------------------------
  // Datei paketweise einlesen und verarbeiten
  // -----------------------------------------------
  printf("\n");
  time(&startTime);
  memset(Buffer, 0, sizeof(Buffer));

  for (curInputFile = 0; curInputFile < NrInputFiles; curInputFile++)
  {
    int EycosCurPart = 0, EycosNrParts = 0;
    if (DoMerge && BookmarkInfo)
    {
      // Bookmarks kurz vor der Schnittstelle löschen
      while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))
        DeleteBookmark(--j);

      // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) löschen
      while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CalcBlockSize(CurrentPosition) + 3*BlocksOneSecond))
        DeleteBookmark(j);

      // neues Bookmark an Schnittstelle setzen
      if (DoCut == 1 || DoMerge)
        if (CurrentPosition-PositionOffset > 4512)
          AddBookmark(j++, CalcBlockSize(CurrentPosition-PositionOffset + 9023 - ((2 + ((MedionMode != 1) ? NrEPGPacks : EPGLen/183)) * OutPacketSize) /* + 4512 */ ));
    }

    while (fIn)
    {
      // SCHNEIDEN
      if ((DoCut && NrSegmentMarker > 2) && (CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position))
      {
        // Wir sind am Sprung zu einem neuen Segment CurSeg angekommen

        // TEILE KOPIEREN: Output-Files schließen
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

          // ### SRT-Subtitles bis hierher ausgeben
          if (RebuildSrt)
            SrtProcessCaptions(0, SegmentMarker[CurSeg].Timems, NewStartTimeOffset, TRUE);

          // aktuelle Output-Files schließen
          if (!CloseOutputFiles())
          {
            CloseInputFiles(!MedionMode, FALSE, FALSE);
            CutProcessor_Free();
            InfProcessor_Free();
            TtxProcessor_Free();
            if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
            if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
            if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
            if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
            PSBuffer_Reset(&AudioPES);
            exit(10);
          }

          NrPackets += (SegmentMarker[1].Position / OutPacketSize);
          NrSegmentMarker = NrSegmentMarker_bak;
          SegmentMarker[1] = LastSegmentMarker_bak;
          if (BookmarkInfo)
          {
            memcpy(BookmarkInfo, &BookmarkInfo_bak, sizeof(TYPE_Bookmark_Info));  // (Resume wird auch zurückkopiert)
            if(ResumeSet) BookmarkInfo->Resume = 0;
          }
        }

        // SEGMENT ÜBERSPRINGEN (wenn nicht-markiert)
        while ((DoCut && NrSegmentMarker >= 2) && (CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position) && !SegmentMarker[CurSeg].Selected)
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
            LastPCR = 0;
//            LastTimeStamp = 0;

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

          // ### SRT-Subtitles im Segment überspringen
          if (RebuildSrt)
            SrtProcessCaptions(0, SegmentMarker[CurSeg].Timems, 0, FALSE);
        }

        // Wir sind am nächsten (zu erhaltenden) SegmentMarker angekommen
        if (CurSeg < NrSegmentMarker-1)
        {
          if (OutCutVersion >= 4)
            printf("[Segment %d]  *%12llu %12lld-%-12lld %s\n", ++n, CurrentPosition, SegmentMarker[CurSeg].Position, SegmentMarker[CurSeg+1].Position, SegmentMarker[CurSeg].pCaption);
          else
            printf("[Segment %d]  *%12llu %10u-%-10u %s\n",     ++n, CurrentPosition, CalcBlockSize(SegmentMarker[CurSeg].Position), CalcBlockSize(SegmentMarker[CurSeg+1].Position), SegmentMarker[CurSeg].pCaption);
          SegmentMarker[CurSeg].Selected = FALSE;
          SegmentMarker[CurSeg].Percent = 0;

          if (BookmarkInfo && DoCut)
          {
            // Bookmarks kurz vor der Schnittstelle löschen
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-PositionOffset)))  // CurPos - SkippedBytes ?
              DeleteBookmark(--j);

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) löschen
//            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CalcBlockSize(CurrentPosition) + 3*BlocksOneSecond))
            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CurPosBlocks + 3*BlocksOneSecond))
              DeleteBookmark(j);

            // neues Bookmark an Schnittstelle setzen
            if (DoCut == 1 || (DoMerge && CurrentPosition == 0))
//              if ((CurrentPosition-PositionOffset > 0) && (CurrentPosition + 3*9024*BlocksOneSecond < (long long)RecFileSize))
              if ((CurrentPosition-PositionOffset > 4512) && (CurPosBlocks + 3*BlocksOneSecond < RecFileBlocks))
                AddBookmark(j++, CalcBlockSize(CurrentPosition-PositionOffset + 9023 - ((2 + ((MedionMode != 1) ? NrEPGPacks : EPGLen/183)) * OutPacketSize) /* + 4512 */ ));
          }

          if (DoCut == 1)
          {
            // SCHNEIDEN: Zeit neu berechnen
            if (NewStartTimeOffset < 0)
              NewStartTimeOffset = SegmentMarker[CurSeg].Timems;
            NewDurationMS += (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);

            // ### bisherige SRT-Subtitles ausgeben
            if (RebuildSrt)
              SrtProcessCaptions(SegmentMarker[CurSeg].Timems, SegmentMarker[CurSeg+1].Timems, CutTimeOffset, TRUE);

            // Header-Pakete ausgeben 2 (experimentell)
            if (CurrentPosition-PositionOffset > (2 + NrEPGPacks) * OutPacketSize)
              DoOutputHeaderPacks = TRUE;
            AfterFirstEPGPacks = FALSE;
          }
          else if (DoCut == 2)
          {
            // TEILE KOPIEREN
            // bisher ausgegebene SegmentMarker / Bookmarks löschen
            while (CurSeg > 0)
            {
              DeleteSegmentMarker(--CurSeg, TRUE);
              i--;
            }
            while (BookmarkInfo && (j > 0))
              DeleteBookmark(--j);

            // Positionen anpassen
            PositionOffset = CurrentPosition;
            CutTimeOffset = SegmentMarker[CurSeg].Timems;
            NewStartTimeOffset = SegmentMarker[CurSeg].Timems;
            NewDurationMS = (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);

            // neue Output-Files öffnen
            GetNextFreeCutName(RecFileIn, RecFileOut, OutDir);
            if (!OpenOutputFiles())
            {
              fclose(fIn); fIn = NULL;
              CloseNavFileIn();
              CloseTeletextOut();
              CloseSrtFileIn();
              CloseSrtFileOut();
              if(MedionMode == 1) SimpleMuxer_Close();
              CutProcessor_Free();
              InfProcessor_Free();
              TtxProcessor_Free();
              if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }      
              if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
              if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
              if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
              printf("ERROR: Cannot create %s.\n", RecFileOut);
              TRACEEXIT;
              exit(7);
            }
            NavProcessor_Init();
            if(!RebuildNav && *RecFileOut && HasNavIn)  SetFirstPacketAfterBreak();
            if(DoStrip) NALUDump_Init();
            LastPCR = 0;
            LastTimems = 0;
            LastTimeStamp = 0;
            dbg_DelBytesSinceLastVid = 0;

            // Caption in inf schreiben
            SetInfEventText(SegmentMarker[CurSeg].pCaption);
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
//          DoOutputHeaderPacks = TRUE;
          int NrPMTPacks = 0;
          for (k = 1; (PATPMTBuf[4 + k*192] == 'G'); NrPMTPacks = k++);
          for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
          {
            ((tTSPacket*) &PATPMTBuf[4 + k*192])->ContinuityCount += (k==0) ? 1 : NrPMTPacks;  // PAT/PMT Continuity Counter setzen
            if (OutPacketSize == 192)
              fwrite(&Buffer[0], 4, 1, fOut);  // 4 Byte Timecode schreiben
            fwrite(&PATPMTBuf[4 + k*192], 188, 1, fOut);
            PositionOffset -= OutPacketSize;
          }
          SimpleMuxer_DoEITOutput();
          PMTCounter = 0;
        }
        if ((ReadBytes = SimpleMuxer_NextTSPacket((tTSPacket*) &Buffer[4])))
        {
          if (ReadBytes < 0)
            AbortProcess = TRUE;
          ReadBytes = PACKETSIZE;
        }
      }
      else
        ReadBytes = (int)fread(&Buffer[4-PACKETOFFSET], 1, PACKETSIZE, fIn);

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
                {
                  fprintf(stderr, "PID check: TS continuity mismatch (PID=%hd, pos=%lld, expect=%hhu, found=%hhu, missing=%hhd)\n", CurPID, CurrentPosition, *CurCtr, ((tTSPacket*) &Buffer[4])->ContinuityCount, (((tTSPacket*) &Buffer[4])->ContinuityCount + 16 - *CurCtr) % 16);
                  AddContinuityError(CurPID, CurrentPosition, *CurCtr, ((tTSPacket*) &Buffer[4])->ContinuityCount);
                }
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

          // auf verschlüsselte Pakete prüfen
          if (((tTSPacket*) &Buffer[4])->Scrambling_Ctrl >= 2)
          {
            if (((tTSPacket*) &Buffer[4])->Payload_Exists)
            {
              CurScrambledPackets++;
              if (CurScrambledPackets <= 100)
              {              
                printf("WARNING [%s]: Scrambled packet bit at pos %lld, PID %u -> %s\n", ((((tTSPacket*) &Buffer[4])->Adapt_Field_Exists && Buffer[8] >= 182) ? "ok" : "!!"), CurrentPosition, CurPID, (RemoveScrambled ? "packet removed" : "ignored"));
                if (CurScrambledPackets == 100)
                  printf("There were scrambled packets. No more scrambled warnings will be shown...\n");
              }
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
            if (ExtractTeletext /*&& fTtxOut*/)
              ProcessTtxPacket((tTSPacket*) &Buffer[4]);

            // Remove Teletext packets
            if (RemoveTeletext)
            {
              NrDroppedTxtPid++;
              DropCurPacket = TRUE;
            }
            else if (TtxPTSOffset && ((tTSPacket*) &Buffer[4])->Payload_Unit_Start)
            {
              tTSPacket* curPacket = (tTSPacket*) &Buffer[4];
              byte *p = &curPacket->Data[curPacket->Adapt_Field_Exists ? curPacket->Data[0] : 0];
              dword oldPTS = 0;
              if ((p = FindPTS(p, 192 - (int)(p-Buffer), &oldPTS)) && oldPTS)
                SetPTS2(p, oldPTS + TtxPTSOffset);
            }
          }

          // Remove EPG stream
          else if (CurPID == 0x12 && RemoveEPGStream)
          {
            if ((!PMTatStart && MedionMode!=1) || AfterFirstEPGPacks || (CurPosBlocks > 0) || (CurBlockBytes >= 10*(unsigned int)PACKETSIZE))
            {
              NrDroppedEPGPid++;
              DropCurPacket = TRUE;
            }
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
                  // PendingPacket soll nicht gelöscht werden
                  PendingBufStart = 0;  // fall-through
//                  break;  (fall-through)

                default:
                  // ein Paket wird behalten -> vorher PendingBuffer in Ausgabe schreiben, ggf. PendingPacket löschen
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

          // Demux first audio track
          if (DemuxAudio && (CurPID == AudioPIDs[0].pid) && fAudioOut)
          {
//            tTSPacket* curPacket = (tTSPacket*) &Buffer[4];
//            byte *p = &curPacket->Data[curPacket->Adapt_Field_Exists ? curPacket->Data[0] : 0];
//            fwrite(p, 1, 192 - (int)(p-Buffer), fAudioOut);
            PSBuffer_ProcessTSPacket(&AudioPES, (tTSPacket*)(&Buffer[4]));

            if((AudioPES.ValidBuffer != LastAudBuffer) && (AudioPES.ValidBuffer > 0))
            {
              fwrite(((AudioPES.ValidBuffer == 1) ? AudioPES.Buffer1 : AudioPES.Buffer2), 1, AudioPES.ValidBufLen, fAudioOut);
              LastAudBuffer = AudioPES.ValidBuffer;
            }
          }

          // SEGMENTMARKER ANPASSEN
//          ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
          while ((i < NrSegmentMarker) && (CurrentPosition >= SegmentMarker[i].Position))
          {
            SegmentMarker[i].Position -= PositionOffset;
            if (SegmentMarker[i].Position < 9024) SegmentMarker[i].Position = 0;
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
                BookmarkInfo->Bookmarks[j] -= (dword)((PositionOffset + 9023) / 9024);  // CalcBlockSize(PositionOffset)
                j++;
              }
            }

            if (!ResumeSet && (DoMerge != 1) && (CurPosBlocks >= BookmarkInfo->Resume))
            {
              if ((PositionOffset + 9023) / 9024 <= BookmarkInfo->Resume)
                BookmarkInfo->Resume -= (dword)((PositionOffset + 9023) / 9024);  // CalcBlockSize(PositionOffset)
              else
                BookmarkInfo->Resume = 0;
              ResumeSet = TRUE;
            }
          }

          // Arrival Timestamps (m2ts)
          if (OutPacketSize > PACKETSIZE)  // OutPacketSize==192 and PACKETSIZE==188
          {
            long long CurPCRfull = 0;
            
            if (GetPCR(&Buffer[4], &CurPCRfull))
            {
              dword CurPCR = (CurPCRfull & 0xffffffff);
              
              if (LastPCR /*&& (CurPCR > LastPCR)*/ && (CurPCR - LastPCR <= 1080000))  // 40 ms
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
//              GetPCRms(&Buffer[4], &global_timestamp);  // oder global_timestamp = (CurPCRfull / 27000)
            }
            else if (CurPID == VideoPID && LastTimeStamp)
              LastTimeStamp += CurTimeStep;
            Buffer[0] = (((byte*)&LastTimeStamp)[3] & 0x3f);
            Buffer[1] = ((byte*)&LastTimeStamp)[2];
            Buffer[2] = ((byte*)&LastTimeStamp)[1];
            Buffer[3] = ((byte*)&LastTimeStamp)[0];
          }
//          else if (ExtractTeletext)
//          {
            if (GetPCRms(&Buffer[4], &global_timestamp))
            {
              if (pGetPCR)
              {
                *pGetPCR = global_timestamp;
/*                for (k = 0; k < NrContinuityPIDs; k++)
                {
                  if (FileDefect[k].Position && !FileDefect[k].FirstPCRAfter)
                    FileDefect[k].FirstPCRAfter = global_timestamp;
                } */
                pGetPCR = NULL;
              }
            }
//          }

          // Nach Übernahme der ersten EPG-Packs, EPG-Counter zurücksetzen
          if (!AfterFirstEPGPacks && (CurPID != 0) && (CurPID != ((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo.PMTPID) && (CurPID != 18))
          {
            AfterFirstEPGPacks = TRUE;
            for (k = 0; k < NrContinuityPIDs; k++)
              if (ContinuityPIDs[k] == 18)
                { ContinuityCtrs[k] = -1; break; }
          }

          // Header-Pakete ausgeben 2 (experimentell!)
          if (DoOutputHeaderPacks)
          {
            int NrPMTPacks = 0;

            if ((HumaxSource || EycosSource || MedionMode==1 || (WriteDescPackets && (CurrentPosition > (2+NrEPGPacks)*PACKETSIZE || !PMTatStart))) && fOut /*&& DoMerge != 1*/)
            {
              for (k = 1; (PATPMTBuf[4 + k*192] == 'G'); NrPMTPacks = k++);
              for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
              {
                ((tTSPacket*) &PATPMTBuf[4 + k*192])->ContinuityCount += ((k==0) ? 1 : NrPMTPacks);  // PAT/PMT Continuity Counter setzen
                if (OutPacketSize == 192)
                  memcpy(&PATPMTBuf[k*192], (byte*)&Buffer[0], 4);
                if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + k*192], OutPacketSize, 1, fOut))
                  PositionOffset -= OutPacketSize;
              }
              if (MedionMode == 1)
                SimpleMuxer_DoEITOutput();
              else if (WriteDescPackets && EPGPacks)
              {
                for (k = 0; k < NrEPGPacks; k++)
                {
                  ((tTSPacket*) &EPGPacks[4 + k*192])->ContinuityCount += NrEPGPacks;  // PMT Continuity Counter setzen
                  if (OutPacketSize == 192)
                    memcpy(&EPGPacks[k*192], &Buffer[0], 4);
                  if (fwrite(&EPGPacks[((OutPacketSize==192) ? 0 : 4) + k*192], OutPacketSize, 1, fOut))
                    PositionOffset -= OutPacketSize;
                }
              }
            }
            DoOutputHeaderPacks = FALSE;
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
            else if (*RecFileOut && HasNavIn)
              QuickNavProcess(CurrentPosition, PositionOffset);

            // PACKET AUSGEBEN
            if (fOut)
            {
              // Wenn PendingPacket -> Daten in Puffer schreiben
              if (isPending)
              {
                // Wenn PendingBuffer voll ist -> alle Pakete außer PendingPacket in Ausgabe schreiben
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
          if(MedionMode==1 && !MedionStrip) PMTCounter++;
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
            if (!DoStrip) PMTCounter++;
            if (fOut && !DoStrip && (PMTCounter >= 29))
            {
              // Wiederhole PAT/PMT und EIT Information (außer wenn Strip aktiv)
              DoOutputHeaderPacks = TRUE;
/*              int NrPMTPacks = 0;
              for (k = 1; (PATPMTBuf[4 + k*192] == 'G'); NrPMTPacks = k++);
              for (k = 0; (PATPMTBuf[4 + k*192] == 'G'); k++)
              {
                ((tTSPacket*) &PATPMTBuf[4 + k*192])->ContinuityCount += (k==0) ? 1 : NrPMTPacks;  // PAT/PMT Continuity Counter setzen
                if (OutPacketSize == 192)
                  fwrite(&Buffer[0], 4, 1, fOut);  // 4 Byte Timecode schreiben
                fwrite(&PATPMTBuf[4 + k*192], 188, 1, fOut);
                PositionOffset -= OutPacketSize;
              } */
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
          if(fOut) { fclose(fOut); fOut = NULL; }
          CloseNavFileIn();
          CloseNavFileOut();
          if (ret)
          {
            CutFileSave(CutFileOut);
            SaveInfFile(InfFileOut, InfFileFirstIn);
          }
          CloseTeletextOut();
          CloseSrtFileIn();
          CloseSrtFileOut();
          if(MedionMode == 1) SimpleMuxer_Close();
          CutProcessor_Free();
          InfProcessor_Free();
          TtxProcessor_Free();
          if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
          if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
          if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
          if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
          PSBuffer_Reset(&AudioPES);
          printf("\n RecStrip aborted.\n");
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
      {
        if (EycosSource)
        {
          if(!EycosNrParts) EycosNrParts = EycosGetNrParts(RecFileIn);
          if(EycosCurPart < EycosNrParts - 1)
          {
            char EycosPartFile[FBLIB_DIR_SIZE];
            fclose(fIn);
            fIn = fopen(EycosGetPart(EycosPartFile, RecFileIn, ++EycosCurPart), "rb");
            if(fIn) continue;
          }
        }
        break;
      }
    }
//    NrPackets += ((RecFileSize + PACKETSIZE-1) / PACKETSIZE);
    if ((DoCut == 2) && (NrSegmentMarker == 2) /* && (CurrentPosition + PACKETSIZE > SegmentMarker[1].Position) */)
      NrPackets += (CurrentPosition-PositionOffset) / OutPacketSize;  // (SegmentMarker[1].Position / OutPacketSize);
    NrScrambledPackets += CurScrambledPackets;

    // ### restliche SRT-Subtitles noch ausgeben
    if (RebuildSrt)
      SrtProcessCaptions(0, 0xffffffff, CutTimeOffset, TRUE);

    if (DoMerge && (curInputFile < NrInputFiles-1))
    {
      CloseInputFiles(!MedionMode, TRUE, FALSE);

      // nächstes Input-File aus Parameter-String ermitteln
      strncpy(RecFileIn, argv[curInputFile+1], sizeof(RecFileIn));
      RecFileIn[sizeof(RecFileIn)-1] = '\0';

      PositionOffset -= CurrentPosition;
      CurSeg = NrSegmentMarker - 1;
      if (NrSegmentMarker >= 2)
        CutTimeOffset -= (int)SegmentMarker[NrSegmentMarker-1].Timems;
//      else
//        CutTimeOffset -= (int)InfDuration * 1000;
      if (-(int)LastTimems < CutTimeOffset)
        CutTimeOffset = -(int)LastTimems;
      SetFirstPacketAfterBreak();
      SetTeletextBreak(TRUE, TeletextPage);
      if(DoStrip)  NALUDump_Init();  // NoContinuityCheck = TRUE;
      LastPCR = 0;
      LastTimeStamp = 0;

      if (!OpenInputFiles(RecFileIn, FALSE))
      {
        CloseOutputFiles();
        CutProcessor_Free();
        InfProcessor_Free();
        TtxProcessor_Free();
        if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
        if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
        if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
        if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
        printf("ERROR: Cannot open input %s.\n", RecFileIn);
        TRACEEXIT;
        exit(5);
      }

      // Header-Pakete ausgeben 3 (experimentell!)
      DoOutputHeaderPacks = TRUE;

      if (DoCut && NrSegments == 0)
      {
        NrCopiedSegments = 1; NrSegments = 1;
      }
      if(NewStartTimeOffset < 0) NewStartTimeOffset = 0;
      CurPosBlocks = 0;
      CurBlockBytes = 0;
      BlocksSincePercent = 0;
      CurScrambledPackets = 0;
      n = 0;
    }
  }
  printf("\n");

#ifdef _DEBUG
  if (MedionMode)
    printf("Max. Video PES length: %u\n", PESVideo.maxPESLen);
#endif

  CloseInputFiles(!MedionMode, TRUE, (!*RecFileOut));
  if ((fOut || (DoCut != 2)) && !CloseOutputFiles())
  {
    CutProcessor_Free();
    InfProcessor_Free();
    TtxProcessor_Free();
    if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
    if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
    if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    exit(10);
  }
  
  CutProcessor_Free();
  InfProcessor_Free();
  TtxProcessor_Free();
  if(PendingBuf) { free(PendingBuf); PendingBuf = NULL; }
  if(PATPMTBuf) { free(PATPMTBuf); PATPMTBuf = NULL; }
  if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }

  if (NrCopiedSegments > 0)
    printf("\nSegments: %d of %d segments copied.\n", NrCopiedSegments, NrSegments);
  if (MedionMode == 1)
    NrDroppedZeroStuffing = NrDroppedZeroStuffing / 184;

  {
    long long NrDroppedAll = NrDroppedFillerNALU + NrDroppedZeroStuffing + NrDroppedAdaptation + NrDroppedNullPid + NrDroppedEPGPid + NrDroppedTxtPid + (RemoveScrambled ? NrScrambledPackets : 0);
    if(DoCut != 2) NrPackets = (CurrentPosition-PositionOffset) / OutPacketSize;
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
//  if(!ret || AbortProcess) exit(9);  // (kann nicht passieren)
  exit(0);
}
