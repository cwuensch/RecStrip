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
static SYSTEM_TYPE      SystemType = ST_TMSS;
static size_t           InfSize = 0;


// ----------------------------------------------
// *****  READ AND WRITE INF FILE  *****
// ----------------------------------------------

bool LoadInfFile(const char *AbsInfName)
{
  FILE                 *fInfIn = NULL;
  TYPE_Service_Info    *ServiceInfo = NULL;
  unsigned long long    InfFileSize = 0;
  bool                  Result = FALSE;

  //Calculate inf header size
  if (HDD_GetFileSize(AbsInfName, &InfFileSize))
    SystemType = (InfFileSize % 122312 <= 10320) ? ST_TMSC : ST_TMSS;

  //Allocate and clear the buffer
  InfSize = (SystemType == ST_TMSC) ? sizeof(TYPE_RecHeader_TMSC) : sizeof(TYPE_RecHeader_TMSS);
  InfBuffer = (byte*) malloc(max(InfSize, 32768));
  if(InfBuffer) 
    memset(InfBuffer, 0, InfSize);
  else
  {
    printf("LoadInfFile() E0901: Not enough memory.\n");
    return FALSE;
  }

  //Read the source .inf
  fInfIn = fopen(AbsInfName, "rb");
  if(!fInfIn)
  {
    free(InfBuffer);
    printf("LoadInfFile() E0902: Source inf not found.\n");
    return FALSE;
  }

  Result = (fread(InfBuffer, 1, InfSize, fInfIn) == InfSize);
  fclose(fInfIn);

  //Decode the source .inf
  switch (SystemType)
  {
    case ST_TMSS:
      RecHeaderInfo = &(((TYPE_RecHeader_TMSS*)InfBuffer)->RecHeaderInfo);
      BookmarkInfo  = &(((TYPE_RecHeader_TMSS*)InfBuffer)->BookmarkInfo);
      ServiceInfo   = &(((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo);
      break;
    case ST_TMSC:
      RecHeaderInfo = &(((TYPE_RecHeader_TMSC*)InfBuffer)->RecHeaderInfo);
      BookmarkInfo  = &(((TYPE_RecHeader_TMSC*)InfBuffer)->BookmarkInfo);
      ServiceInfo   = &(((TYPE_RecHeader_TMSC*)InfBuffer)->ServiceInfo);
      break;
/*    case ST_TMST:
      RecHeaderInfo = &(((TYPE_RecHeader_TMST*)InfBuffer)->RecHeaderInfo);
      BookmarkInfo  = &(((TYPE_RecHeader_TMST*)InfBuffer)->BookmarkInfo);
      ServiceInfo   = &(((TYPE_RecHeader_TMST*)InfBuffer)->ServiceInfo);
      break;  */
    default:
      printf("LoadInfFile() E0903: Incompatible system type.\n");
      free(InfBuffer);
      return FALSE;
  }

  // Prüfe auf HD-Video
  VideoPID = ServiceInfo->VideoPID;
  if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_PART2) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H264) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H263))
  {
    isHDVideo = TRUE;
    VideoPID = ServiceInfo->VideoPID;
    printf("VideoStream=%x, VideoPID=%4.4x, HD=%d", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
  }
  else if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG1) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG2))
  {
    isHDVideo = FALSE;
    VideoPID = ServiceInfo->VideoPID;
    printf("VideoStream=%x, VideoPID=%4.4x, HD=%d", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
  }
  else
  {
    VideoPID = 0;
    printf("LoadInfFile() E0904: Unknown video stream type.\n");
  }
  return Result;
}

bool SaveInfFile(const char *AbsDestInf, const char *AbsSourceInf)
{
  FILE                 *fInfIn = NULL, *fInfOut = NULL;
  size_t                BytesRead;
  bool                  Result = FALSE;

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
      fclose(fInfIn);
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
  return Result;
}

void ProcessInfFile(const dword CurrentPosition, const dword PositionOffset)
{
  static bool           FirstRun = TRUE, ResumeSet = FALSE;
  static int            NrSegments = 0;
  static int            End = 0, Start = 0, j = 0;
  static dword          i = 0;

  if (!BookmarkInfo) return;

  if (FirstRun)
  {
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
}
