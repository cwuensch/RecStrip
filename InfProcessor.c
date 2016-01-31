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
#include "InfProcessor.h"
#include "RecStrip.h"


// Globale Variablen
static byte            *InfBuffer = NULL;
static TYPE_RecHeader_Info *RecHeaderInfo = NULL;
static TYPE_Bookmark_Info  *BookmarkInfo = NULL;
static SYSTEM_TYPE      SystemType = ST_UNKNOWN;
static size_t           InfSize = 0;


// ----------------------------------------------
// *****  READ AND WRITE INF FILE  *****
// ----------------------------------------------

static SYSTEM_TYPE DetermineInfType(byte *InfBuffer)
{
  int                   PointsS = 0, PointsT = 0, PointsC = 0;
  unsigned long        *pd;
  unsigned short       *pw;
  SYSTEM_TYPE           Result = ST_UNKNOWN;

  TRACEENTER;

  //ST_TMSS: Frequency = dword @ 0x0578 (10000...13000)
  //         SymbolRate = word @ 0x057c (2000...30000)
  //         Flags1 = word     @ 0x0575 (& 0xf000 == 0)
  //
  //ST_TMST: Frequency = dword @ 0x0578 (47000...862000)
  //         Bandwidth = byte  @ 0x0576 (6..8)
  //         LPHP = byte       @ 0x057e (& 0xfe == 0)
  //
  //ST_TMSC: Frequency = dword @ 0x0574 (174000...230000 || 470000...862000)
  //         SymbolRate = word @ 0x0578 (2000...30000)
  //         Modulation = byte @ 0x057e (<= 4)

  pd = (dword*)&InfBuffer[0x0578]; if((*pd >= 10000) && (*pd <= 13000)) PointsS++;
  pw = (word*)&InfBuffer[0x057c]; if((*pw >= 2000) && (*pw <= 30000)) PointsS++;
  pw = (word*)&InfBuffer[0x0575]; if((*pw & 0xf000) == 0) PointsS++;

  pd = (dword*)&InfBuffer[0x0578]; if((*pd >= 47000) && (*pd <= 862000)) PointsT++;
  if((InfBuffer[0x0576] >= 6) && (InfBuffer[0x0576] <= 8)) PointsT++;
  if((InfBuffer[0x057e] && 0xfe) == 0) PointsT++;

  pd = (dword*)&InfBuffer[0x0574]; if(((*pd >= 113000000) && (*pd <= 230000000)) || ((*pd >= 470000000) && (*pd <= 858000000))) PointsC++;
  pw = (word*)&InfBuffer[0x0578]; if((*pw >= 2000) && (*pw <= 30000)) PointsC++;
  if(InfBuffer[0x057e] <= 4) PointsC++;

  printf("Determine SystemType: DVBs = %d, DVBt = %d, DVBc = %d points", PointsS, PointsT, PointsC);

  if(PointsS == 3) Result = ST_TMSS;
  else if(PointsC == 3) Result = ST_TMSC;
  else if(PointsT == 3) Result = ST_TMST;
  else
printf(" -> DEBUG! Assertion error: SystemType not detected!\n");

  printf(" -> SystemType=ST_TMS%c\n", (Result==ST_TMSS ? 'S' : ((Result==ST_TMSC) ? 'C' : 'T')));

  TRACEEXIT;
  return Result;
}

bool LoadInfFile(const char *AbsInfName)
{
  FILE                 *fInfIn = NULL;
  TYPE_Service_Info    *ServiceInfo = NULL;
  unsigned long long    InfFileSize = 0;
  bool                  Result = FALSE;

  TRACEENTER;

  //Calculate inf header size
  if (HDD_GetFileSize(AbsInfName, &InfFileSize))
  {
    printf("File size of inf: %llu", InfFileSize);

    if (((InfFileSize % 122312) % 1024 == 84) || ((InfFileSize % 122312) % 1024 == 248))
      SystemType = ST_TMSS;
    else if (((InfFileSize % 122312) % 1024 == 80) || ((InfFileSize % 122312) % 1024 == 244))
      SystemType = ST_TMSC;
    else
printf(" -> DEBUG! Assertion error: SystemType not detected!\n");

    printf(" -> SystemType: %s\n", ((SystemType==ST_TMSC) ? "ST_TMSC" : "ST_TMSS"));
  }
  else
  {
    printf("LoadInfFile() E0901: Source inf not found.\n");
    TRACEEXIT;
    return FALSE;
  }

  //Read the source .inf
  fInfIn = fopen(AbsInfName, "rb");
  if(!fInfIn)
  {
    free(InfBuffer); InfBuffer = NULL;
    printf("LoadInfFile() E0902: Source inf not found.\n");
    TRACEEXIT;
    return FALSE;
  }

  //Allocate and clear the buffer
  InfSize = sizeof(TYPE_RecHeader_TMSS);
  InfBuffer = (byte*) malloc(max(InfSize, 32768));
  if(InfBuffer)
    memset(InfBuffer, 0, max(InfSize, 32768));
  else
  {
    fclose(fInfIn);
    printf("LoadInfFile() E0903: Not enough memory.\n");
    TRACEEXIT;
    return FALSE;
  }

  Result = (fread(InfBuffer, 1, InfSize, fInfIn) + 1 >= InfSize);
  fclose(fInfIn);

  //Decode the source .inf
  if (Result)
  {
    SystemType = DetermineInfType(InfBuffer);
    switch (SystemType)
    {
      case ST_TMSS:
        InfSize = sizeof(TYPE_RecHeader_TMSS);
        RecHeaderInfo = &(((TYPE_RecHeader_TMSS*)InfBuffer)->RecHeaderInfo);
        BookmarkInfo  = &(((TYPE_RecHeader_TMSS*)InfBuffer)->BookmarkInfo);
        ServiceInfo   = &(((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo);
        break;
      case ST_TMSC:
        InfSize = sizeof(TYPE_RecHeader_TMSC);
        RecHeaderInfo = &(((TYPE_RecHeader_TMSC*)InfBuffer)->RecHeaderInfo);
        BookmarkInfo  = &(((TYPE_RecHeader_TMSC*)InfBuffer)->BookmarkInfo);
        ServiceInfo   = &(((TYPE_RecHeader_TMSC*)InfBuffer)->ServiceInfo);
        break;
      case ST_TMST:
        InfSize = sizeof(TYPE_RecHeader_TMST);
        RecHeaderInfo = &(((TYPE_RecHeader_TMST*)InfBuffer)->RecHeaderInfo);
        BookmarkInfo  = &(((TYPE_RecHeader_TMST*)InfBuffer)->BookmarkInfo);
        ServiceInfo   = &(((TYPE_RecHeader_TMST*)InfBuffer)->ServiceInfo);
        break;
      default:
        printf("LoadInfFile() E0904: Incompatible system type.\n");
        free(InfBuffer); InfBuffer = NULL;
        TRACEEXIT;
        return FALSE;
    }

    // Prüfe auf HD-Video
    VideoPID = ServiceInfo->VideoPID;
    if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_PART2) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H264) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H263))
    {
      isHDVideo = TRUE;
      VideoPID = ServiceInfo->VideoPID;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d\n", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG1) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG2))
    {
      isHDVideo = FALSE;
      VideoPID = ServiceInfo->VideoPID;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else
    {
      VideoPID = 0;
      printf("LoadInfFile() E0905: Unknown video stream type.\n");
    }
  }
  TRACEEXIT;
  return Result;
}

bool SaveInfFile(const char *AbsDestInf, const char *AbsSourceInf)
{
  FILE                 *fInfIn = NULL, *fInfOut = NULL;
  size_t                BytesRead;
  bool                  Result = FALSE;

  TRACEENTER;

  //Allocate and clear the buffer
  if(!InfBuffer)
  {
    printf("SaveInfFile() E0901: Buffer not allocated.\n");
    return FALSE;
  }

  //Encode the new inf and write it to the disk
  fInfOut = fopen(AbsDestInf, "wb");
  if(fInfOut)
  {
    Result = (fwrite(InfBuffer, 1, InfSize, fInfOut) == InfSize);

    // Kopiere den Rest der Source-inf (falls vorhanden) in die neue inf hinein
    fInfIn = fopen(AbsSourceInf, "rb");
    if(fInfIn)
    {
      fseek(fInfIn, InfSize, SEEK_SET);
      do {
        BytesRead = fread(InfBuffer, 1, 32768, fInfIn);
        if (BytesRead > 0)
          Result = (fwrite(InfBuffer, 1, BytesRead, fInfOut) == BytesRead) && Result;
      } while (BytesRead > 0);
      fclose(fInfIn); fInfIn = NULL;
    }
    Result = (fflush(fInfOut) == 0) && Result;
    Result = (fclose(fInfOut) == 0) && Result;
  }
  else
  {
    printf("PatchInfFiles() E0902: New inf not created.\n");
    Result = FALSE;
  }

  free(InfBuffer); InfBuffer = NULL;
  TRACEEXIT;
  return Result;
}

void ProcessInfFile(const dword CurrentPosition, const dword PositionOffset)
{
  static bool           FirstRun = TRUE, ResumeSet = FALSE;
  static int            NrSegments = 0;
  static int            End = 0, Start = 0, j = 0;
  static dword          i = 0;

  TRACEENTER;

  if (!InfBuffer || !BookmarkInfo) {
    TRACEEXIT;
    return;
  }

  if (FirstRun)
  {
if (BookmarkInfo->NrBookmarks > NRBOOKMARKS)
  printf("DEBUG: Assertion Error: NrBookmarks=%lu\n", BookmarkInfo->NrBookmarks);
    BookmarkInfo->NrBookmarks = min(BookmarkInfo->NrBookmarks, NRBOOKMARKS);

    // CutDecodeFromBM
    if (BookmarkInfo->Bookmarks[NRBOOKMARKS-2] == 0x8E0A4247)       // ID im vorletzen Bookmark-Dword (-> neues SRP-Format und CRP-Format auf SRP)
      End = NRBOOKMARKS - 2;
    else if (BookmarkInfo->Bookmarks[NRBOOKMARKS-1] == 0x8E0A4247)  // ID im letzten Bookmark-Dword (-> CRP- und altes SRP-Format)
      End = NRBOOKMARKS - 1;

    if(End)
    {
      NrSegments = BookmarkInfo->Bookmarks[End - 1];
      if (NrSegments > NRSEGMENTMARKER) NrSegments = NRSEGMENTMARKER;
      Start = End - NrSegments - 5;
    }
    FirstRun = FALSE;
  }

  while ((i < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[i] < CurrentPosition))
  {
    BookmarkInfo->Bookmarks[i] -= PositionOffset;
    i++;
  }

  while (Start && (j < NrSegments) && (BookmarkInfo->Bookmarks[Start+j] < CurrentPosition))
  {
    BookmarkInfo->Bookmarks[Start+j] -= PositionOffset;
    j++;
  }

  if (!ResumeSet && BookmarkInfo->Resume <= CurrentPosition)
  {
    BookmarkInfo->Resume -= PositionOffset;
    ResumeSet = TRUE;
  }
  TRACEEXIT;
}
