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
static size_t           InfSize = 0;


dword AddTime(dword pvrDate, int addMinutes)  //add minutes to the day
{
  word                  day;
  short                 hour, min;

  TRACEENTER;
  day = (pvrDate >> 16) & 0xffff;
  hour= (pvrDate >> 8) & 0xff;
  min = (pvrDate) & 0xff;

  min += addMinutes % 60;
  if(min < 0)
  {
    hour-=1;
    min+=60;
  }
  else if(min > 59)
  {
    hour+=1;
    min-=60;
  }

  hour += addMinutes / 60;

  if(hour < 0)
  {
    day-=1;
    hour+=24;
  }
  else
  {
    while(hour > 23)
    {
      day+=1;
      hour-=24;
    }
  }

  TRACEEXIT;
  return ((day<<16)|(hour<<8)|min);
}


// ----------------------------------------------
// *****  READ AND WRITE INF FILE  *****
// ----------------------------------------------

static SYSTEM_TYPE DetermineInfType(const byte *const InfBuffer, const unsigned long long InfFileSize)
{
  int                   PointsS = 0, PointsT = 0, PointsC = 0;
  SYSTEM_TYPE           SizeType = ST_UNKNOWN, Result = ST_UNKNOWN;

  TYPE_RecHeader_TMSS  *Inf_TMSS = (TYPE_RecHeader_TMSS*)InfBuffer;
  TYPE_RecHeader_TMSC  *Inf_TMSC = (TYPE_RecHeader_TMSC*)InfBuffer;
  TYPE_RecHeader_TMST  *Inf_TMST = (TYPE_RecHeader_TMST*)InfBuffer;

  TRACEENTER;

  // Dateigröße beurteilen
  if (((InfFileSize % 122312) % 1024 == 84) || ((InfFileSize % 122312) % 1024 == 248))
  {
    PointsS++;
    PointsT++;
    SizeType = ST_TMSS;
  }
  else if (((InfFileSize % 122312) % 1024 == 80) || ((InfFileSize % 122312) % 1024 == 244))
  {
    PointsC++;
    SizeType = ST_TMSC;
  }

  // Frequenzdaten

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

  if ((Inf_TMSS->TransponderInfo.Frequency      >=     10700)  &&  (Inf_TMSS->TransponderInfo.Frequency  <=     12750))  PointsS++;
  if ((Inf_TMSS->TransponderInfo.SymbolRate     >=     10000)  &&  (Inf_TMSS->TransponderInfo.SymbolRate <=     30000))  PointsS++;
  if ((Inf_TMSS->TransponderInfo.unused2        ==         0)  &&  (Inf_TMSS->TransponderInfo.unused3    ==         0))  PointsS++;
//  if  (Inf_TMSS->TransponderInfo.TPMode         ==         0)   PointsS++;

  if ((Inf_TMST->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMST->TransponderInfo.Frequency  <=    862000))  PointsT++;
  if ((Inf_TMST->TransponderInfo.Bandwidth      >=         6)  &&  (Inf_TMST->TransponderInfo.Bandwidth  <=         8))  PointsT++;
  if ((Inf_TMST->TransponderInfo.LPHP           <=         1)  &&  (Inf_TMST->TransponderInfo.unused2    ==         0))  PointsT++;
  if  (Inf_TMST->TransponderInfo.SatIdx         !=         0)   PointsT++;  else PointsT--;

  if ((Inf_TMSC->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMSC->TransponderInfo.Frequency  <=    862000))  PointsC++;
  if ((Inf_TMSC->TransponderInfo.SymbolRate     >=      6111)  &&  (Inf_TMSC->TransponderInfo.SymbolRate <=      6900))  PointsC++;
  if  (Inf_TMSC->TransponderInfo.ModulationType <=         4)   PointsC++;
  if  (Inf_TMSC->TransponderInfo.SatIdx         ==         0)   PointsC++;  else PointsC--;
  if (*(dword*)(&InfBuffer[sizeof(TYPE_RecHeader_TMSC)]) == 0)  PointsC++;

  printf("Determine SystemType: DVBs = %d, DVBt = %d, DVBc = %d points\n", PointsS, PointsT, PointsC);

  // Wenn SatIdx gesetzt, dann kann TMS-C ausgeschlossen werden (?)
//  if (Inf_TMSC->TransponderInfo.SatIdx != 0) PointsC = 0;

  if (PointsC>=6 || (PointsC>=5 && PointsS<=3 && PointsT<=4) || (PointsC>=4 && PointsS<=2 && PointsT<=3))
    Result = ST_TMSC;
  else
    if (PointsS >= 3 && PointsT <= 4)
      Result = ST_TMSS;
    else if (PointsT >= 4 && PointsS <= 3)
      Result = ST_TMST;

  printf(" -> SystemType=ST_TMS%c\n", (Result==ST_TMSS ? 's' : ((Result==ST_TMSC) ? 'c' : ((Result==ST_TMST) ? 't' : '?'))));
  if (Result != SizeType && !(Result == ST_TMST && SizeType == ST_TMSS))
    printf(" -> DEBUG! Assertion error: SystemType in inf (%u) not consistent to filesize (%u)!\n", Result, SystemType);

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
    SystemType = ST_TMSS;
    printf("LoadInfFile() E0901: Source inf not found.\n");
    TRACEEXIT;
    return FALSE;
  }

  //Read the source .inf
  fInfIn = fopen(AbsInfName, "rb");
  if(!fInfIn)
  {
    SystemType = ST_TMSS;
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

  Result = (fread(InfBuffer, 1, InfSize, fInfIn) + 4 >= InfSize);
  fclose(fInfIn);

  //Decode the source .inf
  if (Result)
  {
    SystemType = DetermineInfType(InfBuffer, InfFileSize);
    RecHeaderInfo = (TYPE_RecHeader_Info*) InfBuffer;
    ServiceInfo   = &(((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo);
    switch (SystemType)
    {
      case ST_TMSS:
        InfSize = sizeof(TYPE_RecHeader_TMSS);
        BookmarkInfo = &(((TYPE_RecHeader_TMSS*)InfBuffer)->BookmarkInfo);
        break;
      case ST_TMSC:
        InfSize = sizeof(TYPE_RecHeader_TMSC);
        BookmarkInfo = &(((TYPE_RecHeader_TMSC*)InfBuffer)->BookmarkInfo);
        break;
      case ST_TMST:
        InfSize = sizeof(TYPE_RecHeader_TMST);
        BookmarkInfo = &(((TYPE_RecHeader_TMST*)InfBuffer)->BookmarkInfo);
        break;
      default:
        printf("LoadInfFile() E0904: Incompatible system type.\n");
        free(InfBuffer); InfBuffer = NULL;
        TRACEEXIT;
        return FALSE;
    }

    // Prüfe auf verschlüsselte Aufnahme
    if ((RecHeaderInfo->CryptFlag & 1) != 0)
    {
      printf("LoadInfFile() E0905: Recording is encrypted.\n");
      free(InfBuffer); InfBuffer = NULL;
      TRACEEXIT;
      return FALSE;
    }

    // Prüfe auf HD-Video
    VideoPID = ServiceInfo->VideoPID;
    if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_PART2) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H264) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H263))
    {
      isHDVideo = TRUE;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d\n", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG1) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG2))
    {
      isHDVideo = FALSE;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d\n", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else
    {
      VideoPID = 0;
      printf("LoadInfFile() E0906: Unknown video stream type.\n");
    }

if (RecHeaderInfo->Reserved != 0)
  printf("DEBUG! Assertion Error: Reserved-Flags is not 0.\n");
    if (RecHeaderInfo->rs_HasBeenStripped)
      AlreadyStripped = TRUE;
  }
  TRACEEXIT;
  return Result;
}

bool CloseInfFile(const char *AbsDestInf, const char *AbsSourceInf, bool Save)
{
  FILE                 *fInfIn = NULL, *fInfOut = NULL;
  size_t                BytesRead;
  bool                  Result = FALSE;

  TRACEENTER;

  if (Save)
  {
    if(!InfBuffer)
    {
      printf("SaveInfFile() E0901: Buffer not allocated.\n");
      return FALSE;
    }

    //Encode the new inf and write it to the disk
    fInfOut = fopen(AbsDestInf, "wb");
    if(fInfOut)
    {
      RecHeaderInfo->rs_ToBeStripped = FALSE;
      RecHeaderInfo->rs_HasBeenStripped = TRUE;
      RecHeaderInfo->rbn_HasBeenScanned = TRUE;
      if (NewDurationMS)
      {
        RecHeaderInfo->HeaderDuration = (word)((NewDurationMS + 500) / 60000);
        RecHeaderInfo->HeaderDurationSec = ((NewDurationMS + 500) / 1000) % 60;
      }
      if (NewStartTimeOffset)
        RecHeaderInfo->HeaderStartTime = AddTime(RecHeaderInfo->HeaderStartTime, NewStartTimeOffset / 60000);
      Result = (fwrite(InfBuffer, 1, InfSize, fInfOut) == InfSize);

      // Kopiere den Rest der Source-inf (falls vorhanden) in die neue inf hinein
      fInfIn = fopen(AbsSourceInf, "r+b");
      if(fInfIn)
      {
        fread(RecHeaderInfo, 1, 8, fInfIn);
        RecHeaderInfo->rs_ToBeStripped = FALSE;
        RecHeaderInfo->rbn_HasBeenScanned = TRUE;
        fwrite(RecHeaderInfo, 1, 8, fInfIn);

        fseek(fInfIn, InfSize, SEEK_SET);
        do {
          BytesRead = fread(InfBuffer, 1, 32768, fInfIn);
          if (BytesRead > 0)
            Result = (fwrite(InfBuffer, 1, BytesRead, fInfOut) == BytesRead) && Result;
        } while (BytesRead > 0);
        fclose(fInfIn);
      }
//      Result = (fflush(fInfOut) == 0) && Result;
      Result = (fclose(fInfOut) == 0) && Result;
    }
    else
    {
      printf("PatchInfFiles() E0902: New inf not created.\n");
      Result = FALSE;
    }
  }
  free(InfBuffer); InfBuffer = NULL;
  TRACEEXIT;
  return Result;
}
