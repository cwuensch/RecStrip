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
bool                    DoStrip = FALSE, RemoveEPGStream = FALSE, RemoveTeletext = FALSE, RebuildNav = FALSE, RebuildInf = FALSE;
int                     DoCut = 0;

TYPE_Bookmark_Info     *BookmarkInfo = NULL;
tSegmentMarker2        *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;
dword                   InfDuration = 0, NewDurationMS = 0, NewStartTimeOffset = 0, CutTimeOffset = 0;

// Lokale Variablen
static char             NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], NavFileOld[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], InfFileOld[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE];
static FILE            *fIn = NULL;  // dirty Hack: erreichbar machen für InfProcessor
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


static bool HDD_FileExist(const char *AbsFileName)
{
  return (AbsFileName && (access(AbsFileName, 0) == 0));
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

static inline time_t TF2UnixTime(dword TFTimeStamp)
{ 
  return ((TFTimeStamp >> 16) - 0x9e8b) * 86400 + ((TFTimeStamp >> 8 ) & 0xff) * 3600 + (TFTimeStamp & 0xff) * 60;
}

static bool HDD_SetFileDateTime(char const *AbsFileName, time_t NewDateTime)
{
  struct stat64         statbuf;
  struct utimbuf        timebuf;

  if(NewDateTime == 0)
    NewDateTime = time(NULL);

  if(AbsFileName && (NewDateTime > 0xd0790000))
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

static void GetNextFreeCutName(const char *AbsSourceFileName, char *const OutCutFileName)
{
  char                  CheckFileName[2048];
  size_t                NameLen, ExtStart;
  int                   FreeIndices = 0, i = 0;

  TRACEENTER;
  if(OutCutFileName) OutCutFileName[0] = '\0';

  if (AbsSourceFileName && OutCutFileName)
  {
    const char *p = strrchr(AbsSourceFileName, '.');  // ".rec" entfernen
    NameLen = ExtStart = ((p) ? (size_t)(p - AbsSourceFileName) : strlen(AbsSourceFileName));
//    if((p = strstr(&SourceFileName[NameLen - 10], " (Cut-")) != NULL)
//      NameLen = p - SourceFileName;        // wenn schon ein ' (Cut-xxx)' vorhanden ist, entfernen
    strncpy(CheckFileName, AbsSourceFileName, NameLen);

    do
    {
      i++;
      snprintf(&CheckFileName[NameLen], sizeof(CheckFileName) - NameLen, " (Cut-%d)%s", i, &AbsSourceFileName[ExtStart]);
    } while (HDD_FileExist(CheckFileName));

    strcpy(OutCutFileName, CheckFileName);
  }
  TRACEEXIT;
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

    memset(&SegmentMarker[NrSegmentMarker - 1], 0, sizeof(tSegmentMarker2));
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


// --------------------------------------------------
// *****  Öffnen / Schließen der Output-Files  *****
// --------------------------------------------------

static bool OpenOutputFiles(void)
{
  TRACEENTER;

  // ggf. Output-File öffnen
  if (*RecFileOut)
  {
    printf("\nOutput rec: %s\n", RecFileOut);
    fOut = fopen(RecFileOut, "wb");
    if (fOut)
      setvbuf(fOut, NULL, _IOFBF, BUFSIZE);
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

  // ggf. Output-nav öffnen
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

  if (*NavFileOut && LoadNavFileOut(NavFileOut))
    printf("Nav output: %s\n", NavFileOut);

  // CutFileOut ermitteln
  if (*CutFileIn && *RecFileOut)
  {
    GetCutNameFromRec(RecFileOut, CutFileOut);
    printf("Cut output: %s\n", CutFileOut);
  }
  else CutFileOut[0] = '\0';

  printf("\n");
  TRACEEXIT;
  return TRUE;
}

bool CloseOutputFiles(void)
{
  TRACEENTER;

  if (!CloseNavFileOut())
    printf("  WARNING: Failed closing the nav file.\n");

  if ((DoCut || RebuildInf) && LastTimems)
    NewDurationMS = LastTimems;
  if (DoCut && NrSegmentMarker >= 2)
  {
    SegmentMarker[NrSegmentMarker-1].Position = CurrentPosition - PositionOffset;
    SegmentMarker[NrSegmentMarker-1].Timems = NewDurationMS;
  }
  if (BookmarkInfo && BookmarkInfo->Resume >= CalcBlockSize(CurrentPosition - PositionOffset))
    BookmarkInfo->Resume = 0;

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
      SaveInfFile(InfFileOut, InfFileIn);
      CutProcessor_Free();
      InfProcessor_Free();
      free(PendingBuf); PendingBuf = NULL;
      TRACEEXIT;
      return FALSE;
    }
    fOut = NULL;
  }

  if (*CutFileOut && !CutFileSave(CutFileOut))
    printf("  WARNING: Cannot create cut %s.\n", CutFileOut);

  if (*InfFileOut && !SaveInfFile(InfFileOut, InfFileIn))
    printf("  WARNING: Cannot create inf %s.\n", InfFileOut);


  if (*RecFileOut)
    HDD_SetFileDateTime(RecFileOut, TF2UnixTime(RecHeaderInfo->StartTime));
  if (*InfFileOut)
    HDD_SetFileDateTime(InfFileOut, TF2UnixTime(RecHeaderInfo->StartTime));
  if (*NavFileOut)
    HDD_SetFileDateTime(NavFileOut, TF2UnixTime(RecHeaderInfo->StartTime));
  if (*CutFileOut)
    HDD_SetFileDateTime(CutFileOut, TF2UnixTime(RecHeaderInfo->StartTime));


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
  bool                  ResumeSet = FALSE;
  int                   CurSeg = 0, i = 0, j = 0, n = 0;
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

  // Eingabe-Parameter prüfen
  while ((argc > 1) && (argv && argv[1] && argv[1][0] == '-'))
  {
    switch (argv[1][1])
    {
      case 'n':   RebuildNav = TRUE;      break;
      case 'i':   RebuildInf = TRUE;      break;
      case 'r':   if(!DoCut) DoCut = 1;   break;
      case 'c':   DoCut = 2;              break;
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
  printf("\nParameters:\nDoCut=%d, DoStrip=%s, RmEPG=%s, RmTxt=%s, RbldNav=%s, RbldInf=%s, PkSize=%hhu\n", DoCut, (DoStrip ? "yes" : "no"), (RemoveEPGStream ? "yes" : "no"), (RemoveTeletext ? "yes" : "no"), (RebuildNav ? "yes" : "no"), (RebuildInf ? "yes" : "no"), OutPacketSize);

  // Eingabe-Dateinamen lesen
  if (argc > 1)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    RecFileIn[sizeof(RecFileIn)-1] = '\0';
  }
  else RecFileIn[0] = '\0';
  if (DoCut == 2)
    GetNextFreeCutName(RecFileIn, RecFileOut);
  else if (argc > 2)
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
    printf("  -r:        Cut the recording according to cut-file. (if OutFile specified)\n"
           "             Copies only the selected segments into the new rec.\n\n");
    printf("  -c:        Copy the selected segments from cut-file. (OutFiles auto-named)\n"
           "             Copies each selected segment into a single new rec.\n\n");
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


  // Input-File öffnen
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
      printf("  File size: %llu, packet size: %hhu\n", RecFileSize, PACKETSIZE);

      LoadInfFromRec(RecFileIn);
    }
    else
    {
      printf("  ERROR: Ivalid TS packet size.\n");
      fclose(fIn); fIn = NULL;
      CutProcessor_Free();
      InfProcessor_Free();
      free(PendingBuf); PendingBuf = NULL;      
      TRACEEXIT;
      exit(4);
    }
  }
  else
  {
    printf("  ERROR: Cannot open %s.\n", RecFileIn);
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    TRACEEXIT;
    exit(3);
  }

  // ggf. inf-File einlesen
  snprintf(InfFileIn, sizeof(InfFileIn), "%s.inf", RecFileIn);
  printf("\nInf file: %s\n", InfFileIn);
  InfFileOld[0] = '\0';

  if (!LoadInfFile(InfFileIn))
  {
    fclose(fIn); fIn = NULL;
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    TRACEEXIT;
    exit(5);
  }

  if (InfDuration)
    BlocksOneSecond = RecFileBlocks / InfDuration;

  if (AlreadyStripped)
  {
    printf("  INFO: File has already been stripped.\n");
/*    fclose(fIn); fIn = NULL;
    CloseInfFile(NULL, NULL, FALSE);
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    TRACEEXIT;
    exit(0); */
  }
  if (VideoPID == 0)
  {
    printf("  ERROR: No video PID determined.\n");
    fclose(fIn); fIn = NULL;
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    TRACEEXIT;
    exit(6);
  }

  if (HumaxSource && fOut)
  {
    printf("  Generate new PAT/PMT for Humax recording.\n");
    if (fwrite(&PATPMTBuf[(OutPacketSize==192) ? 0 : 4], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;
    if (fwrite(&PATPMTBuf[((OutPacketSize==192) ? 0 : 4) + 192], OutPacketSize, 1, fOut))
      PositionOffset -= OutPacketSize;
  }

  // ggf. nav-File öffnen
  snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);
  printf("\nNav file: %s\n", NavFileIn);
  if (!LoadNavFileIn(NavFileIn))
  {
    printf("  WARNING: Cannot open nav file %s.\n", NavFileIn);
    NavFileIn[0] = '\0';
  }
  
  // ggf. cut-File einlesen
  GetCutNameFromRec(RecFileIn, CutFileIn);
  printf("\nCut file: %s\n", CutFileIn);

  if (!CutFileLoad(CutFileIn))
    CutFileIn[0] = '\0';
  printf("\n");


  // Output-Files öffnen
  if (DoCut < 2 && !OpenOutputFiles())
  {
    fclose(fIn); fIn = NULL;
    CloseNavFileIn();
    CutProcessor_Free();
    InfProcessor_Free();
    free(PendingBuf); PendingBuf = NULL;      
    printf("ERROR: Cannot create %s.\n", RecFileOut);
    TRACEEXIT;
    exit(7);
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
    if (DoCut && (NrSegmentMarker > 2) && (CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position))
    {
      // Wir sind am Sprung zu einem neuen Segment CurSeg angekommen

      // TEILE KOPIEREN: Output-Files schließen
      if (fOut && DoCut == 2)
      {
        int NrSegmentMarker_bak = NrSegmentMarker;
        TYPE_Bookmark_Info BookmarkInfo_bak;
        memcpy(&BookmarkInfo_bak, BookmarkInfo, sizeof(TYPE_Bookmark_Info));

        // Alle SegmentMarker und Bookmarks bis CurSeg ausgeben
        NrSegmentMarker = 2;
        SegmentMarker[0].Position = 0;
        SegmentMarker[1].Percent = 100;
        ActiveSegment = 0;
        BookmarkInfo->NrBookmarks = j;

        // aktuelle Output-Files schließen
        if (!CloseOutputFiles())
          exit(10);

        NrSegmentMarker = NrSegmentMarker_bak;
        memcpy(BookmarkInfo, &BookmarkInfo_bak, sizeof(TYPE_Bookmark_Info));
      }

      // SEGMENT ÜBERSPRINGEN (wenn nicht-markiert)
      while ((CurSeg < NrSegmentMarker-1) && (CurrentPosition >= SegmentMarker[CurSeg].Position) && !SegmentMarker[CurSeg].Selected)
      {
        if (OutCutVersion >= 4)
          printf("[Segment %d]  -%12llu %12lld-%-12lld %s\n", n++, CurrentPosition, SegmentMarker[CurSeg].Position+PositionOffset, SegmentMarker[CurSeg+1].Position, SegmentMarker[CurSeg].pCaption);
        else
          printf("[Segment %d]  -%12llu %10u-%-10u %s\n",     n++, CurrentPosition, CalcBlockSize(SegmentMarker[CurSeg].Position+PositionOffset), CalcBlockSize(SegmentMarker[CurSeg+1].Position), SegmentMarker[CurSeg].pCaption);
        CutTimeOffset += SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems;
        DeleteSegmentMarker(CurSeg, TRUE);

        if (CurSeg < NrSegmentMarker-1)
        {
          long long SkippedBytes = (((SegmentMarker[CurSeg].Position) /* / PACKETSIZE) * PACKETSIZE */) ) - CurrentPosition;
          fseeko64(fIn, ((SegmentMarker[CurSeg].Position) /* / PACKETSIZE) * PACKETSIZE */), SEEK_SET);
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
            while ((j > 0) && (BookmarkInfo->Bookmarks[j-1] + 3*BlocksOneSecond >= CalcBlockSize(CurrentPosition-SkippedBytes)))
              j--;

            // Bookmarks im weggeschnittenen Bereich (bzw. kurz nach Schnittstelle) löschen
            while ((j < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[j] < CurPosBlocks + 3*BlocksOneSecond))
              DeleteBookmark(j);

            // neues Bookmark an Schnittstelle setzen
            if (DoCut == 1)
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

      // Wir sind am nächsten (zu erhaltenden) SegmentMarker angekommen
      if (CurSeg < NrSegmentMarker-1)
      {
        if (OutCutVersion >= 4)
          printf("[Segment %d]  *%12llu %12lld-%-12lld %s\n", n++, CurrentPosition, SegmentMarker[CurSeg].Position, SegmentMarker[CurSeg+1].Position, SegmentMarker[CurSeg].pCaption);
        else
          printf("[Segment %d]  *%12llu %10u-%-10u %s\n",     n++, CurrentPosition, CalcBlockSize(SegmentMarker[CurSeg].Position), CalcBlockSize(SegmentMarker[CurSeg+1].Position), SegmentMarker[CurSeg].pCaption);
        SegmentMarker[CurSeg].Selected = FALSE;
        SegmentMarker[CurSeg].Percent = 0;

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
          // bisher ausgegebene SegmentMarker / Bookmarks löschen
          while (CurSeg > 0)
          {
            DeleteSegmentMarker(--CurSeg, TRUE);
            i--;
          }
          while (BookmarkInfo && (j > 0))
            DeleteBookmark(--j);

          // neue Output-Files öffnen
          GetNextFreeCutName(RecFileIn, RecFileOut);
          if (!OpenOutputFiles())
          {
            fclose(fIn); fIn = NULL;
            CloseNavFileIn();
            CutProcessor_Free();
            InfProcessor_Free();
            free(PendingBuf); PendingBuf = NULL;      
            printf("ERROR: Cannot create %s.\n", RecFileOut);
            TRACEEXIT;
            exit(7);
          }
          NavProcessor_Init();
          LastTimems = 0;
          LastPCR = 0;
          LastTimeStamp = 0;

          // Positionen anpassen
          PositionOffset = CurrentPosition;
          CutTimeOffset = SegmentMarker[CurSeg].Timems;
          NewStartTimeOffset = SegmentMarker[CurSeg].Timems;
          NewDurationMS = (SegmentMarker[CurSeg+1].Timems - SegmentMarker[CurSeg].Timems);
        }
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
                // PendingPacket soll nicht gelöscht werden
                PendingBufStart = 0;
//                break;

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

        // SEGMENTMARKER ANPASSEN
//        ProcessCutFile(CurPosBlocks, PosOffsetBlocks);
        while ((i < NrSegmentMarker) && (CurrentPosition >= SegmentMarker[i].Position))
        {
          SegmentMarker[i].Position -= PositionOffset;
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
          long long CurPCR = 0;
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
              fclose(fIn); fIn = NULL;
              fclose(fOut); fOut = NULL;
              CloseNavFileIn();
              CloseNavFileOut();
              CutProcessor_Free();
              InfProcessor_Free();
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
            CloseNavFileIn();
            CloseNavFileOut();
            CutFileSave(CutFileOut);
            SaveInfFile(InfFileOut, InfFileIn);
            CutProcessor_Free();
            InfProcessor_Free();
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
  printf("\n");

  if (fOut && !CloseOutputFiles())
    exit(10);

  if (fIn)
  {
    fclose(fIn); fIn = NULL;
  }
  if (*InfFileIn)
  {
    struct stat64 statbuf;
    time_t OldInfTime = 0;
    if (stat64(RecFileIn, &statbuf) == 0)
      OldInfTime = statbuf.st_mtime;
    SetInfStripFlags(InfFileIn, TRUE, DoStrip);
    if (OldInfTime)
      HDD_SetFileDateTime(InfFileIn, statbuf.st_mtime);
  }
  CloseNavFileIn();
  CutProcessor_Free();
  InfProcessor_Free();
  free(PendingBuf); PendingBuf = NULL;

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
