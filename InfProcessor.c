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
#include "InfProcessor.h"
#include "RebuildInf.h"
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
  int                   AntiS = 0, AntiT = 0, AntiC = 0;
  SYSTEM_TYPE           SizeType = ST_UNKNOWN, Result = ST_UNKNOWN;

  TYPE_RecHeader_TMSS  *Inf_TMSS = (TYPE_RecHeader_TMSS*)InfBuffer;
  TYPE_RecHeader_TMSC  *Inf_TMSC = (TYPE_RecHeader_TMSC*)InfBuffer;
  TYPE_RecHeader_TMST  *Inf_TMST = (TYPE_RecHeader_TMST*)InfBuffer;

  TRACEENTER;

  // Dateigrˆﬂe beurteilen
  if (((InfFileSize % 122312) % 1024 == 84) || ((InfFileSize % 122312) % 1024 == 248))
  {
//    PointsS++;
//    PointsT++;
    SizeType = ST_TMSS;
  }
  else if (((InfFileSize % 122312) % 1024 == 80) || ((InfFileSize % 122312) % 1024 == 244))
  {
//    PointsC++;
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

  if (Inf_TMSS->TransponderInfo.Frequency    &&  !((Inf_TMSS->TransponderInfo.Frequency      >=     10700)  &&  (Inf_TMSS->TransponderInfo.Frequency  <=     12750)))  AntiS++;
  if (Inf_TMSS->TransponderInfo.SymbolRate   &&  !((Inf_TMSS->TransponderInfo.SymbolRate     >=     10000)  &&  (Inf_TMSS->TransponderInfo.SymbolRate <=     30000)))  AntiS++;
  if (Inf_TMSS->TransponderInfo.UnusedFlags1  ||  Inf_TMSS->TransponderInfo.Unknown2)  AntiS++;
  if (Inf_TMSS->TransponderInfo.TPMode > 1)  AntiS++;
  if ((dword)Inf_TMSS->BookmarkInfo.NrBookmarks > 64)  AntiS++;

  if (Inf_TMST->TransponderInfo.Frequency    &&  !((Inf_TMST->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMST->TransponderInfo.Frequency  <=    862000)))  AntiT++;
  if (Inf_TMST->TransponderInfo.Bandwidth    &&  !((Inf_TMST->TransponderInfo.Bandwidth      >=         6)  &&  (Inf_TMST->TransponderInfo.Bandwidth  <=         8)))  AntiT++;
  if (Inf_TMST->TransponderInfo.LPHP > 1  ||  Inf_TMST->TransponderInfo.unused2)  AntiT++;
  if (Inf_TMST->TransponderInfo.SatIdx != 0)  AntiT++;
  if ((dword)Inf_TMST->BookmarkInfo.NrBookmarks > 64)  AntiT++;

  if (Inf_TMSC->TransponderInfo.Frequency    &&  !((Inf_TMSC->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMSC->TransponderInfo.Frequency  <=    862000)))  AntiC++;
  if (Inf_TMSC->TransponderInfo.SymbolRate   &&  !((Inf_TMSC->TransponderInfo.SymbolRate     >=      6111)  &&  (Inf_TMSC->TransponderInfo.SymbolRate <=      6900)))  AntiC++;
  if (Inf_TMSC->TransponderInfo.ModulationType > 4)  AntiC++;
  if (Inf_TMSC->TransponderInfo.SatIdx != 0)  AntiC++;
  if ((dword)Inf_TMSC->BookmarkInfo.NrBookmarks > 64)  AntiC++;

  printf("Determine SystemType: DVBs = %d, DVBt = %d, DVBc = %d Points\n", -AntiS, -AntiT, -AntiC);

  if ((AntiC == 0 && AntiS > 0 && AntiT > 0) || (AntiC == 1 && AntiS > 1 && AntiT > 1))
    Result = ST_TMSC;
  else
    if ((AntiS == 0 && AntiC > 0 && AntiT > 0) || (AntiS == 1 && AntiC > 1 && AntiT > 1))
      Result = ST_TMSS;
    else if ((AntiT == 0 && AntiS > 0 && AntiC > 0) || (AntiT == 1 && AntiS > 1 && AntiC > 1))
      Result = ST_TMST;
    else if ((Inf_TMSS->BookmarkInfo.NrBookmarks == 0) && (Inf_TMSC->BookmarkInfo.NrBookmarks == 0))
      Result = ST_TMSS;

  printf(" -> SystemType=ST_TMS%c\n", (Result==ST_TMSS ? 's' : ((Result==ST_TMSC) ? 'c' : ((Result==ST_TMST) ? 't' : '?'))));
  if (Result != SizeType && SizeType && !(Result == ST_TMST && SizeType == ST_TMSS))
    printf(" -> DEBUG! Assertion error: SystemType in inf (%u) not consistent to filesize (%u)!\n", Result, SizeType);

  TRACEEXIT;
  return Result;
}

bool LoadInfFile(char *AbsInfName)
{
  FILE                 *fInfIn = NULL;
  TYPE_Service_Info    *ServiceInfo = NULL;
  unsigned long long    InfFileSize = 0;
  bool                  HDFound = FALSE, Result = FALSE;

  TRACEENTER;

  //Allocate and clear the buffer
  InfSize = sizeof(TYPE_RecHeader_TMSS);
  InfBuffer = (byte*) malloc(max(InfSize, 32768));
  if(InfBuffer)
  {
    memset(InfBuffer, 0, max(InfSize, 32768));
    RecHeaderInfo = (TYPE_RecHeader_Info*) InfBuffer;
    ServiceInfo   = &(((TYPE_RecHeader_TMSS*)InfBuffer)->ServiceInfo);
  }
  else
  {
    printf("LoadInfFile() E0901: Not enough memory.\n");
    TRACEEXIT;
    return FALSE;
  }

  //Calculate inf header size
/*  if (HDD_GetFileSize(AbsInfName, &InfFileSize))
  {
    printf("File size of inf: %llu", InfFileSize);

    if (((InfFileSize % 122312) % 1024 == 84) || ((InfFileSize % 122312) % 1024 == 248))
      SystemType = ST_TMSS;
    else if (((InfFileSize % 122312) % 1024 == 80) || ((InfFileSize % 122312) % 1024 == 244))
      SystemType = ST_TMSC;
    else
printf(" -> DEBUG! Assertion error: SystemType not detected!\n");

    printf(" -> SystemType: %s\n", ((SystemType==ST_TMSC) ? "ST_TMSC" : "ST_TMSS"));
  } */

  // Get inf infos from rec
  GenerateInfFile(fIn, (TYPE_RecHeader_TMSS*)InfBuffer);

  //Read the source .inf
  if (AbsInfName && !RebuildInf)
    fInfIn = fopen(AbsInfName, "rb");
  if(fInfIn)
  {
    Result = (fread(InfBuffer, 1, InfSize, fInfIn) + 4 >= InfSize);
    fclose(fInfIn);
  }
  else
  {
    if (AbsInfName) AbsInfName[0] = '\0';
    SystemType = ST_TMSS;
  }

  //Decode the source .inf
  if (Result)
  {
    SystemType = DetermineInfType(InfBuffer, InfFileSize);
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

    // Pr¸fe auf verschl¸sselte Aufnahme
    if (((RecHeaderInfo->CryptFlag & 1) != 0) && !*RecFileOut)
    {
      printf("LoadInfFile() E0905: Recording is encrypted.\n");
      free(InfBuffer); InfBuffer = NULL;
      TRACEEXIT;
      return FALSE;
    }

    // Pr¸fe auf HD-Video
    VideoPID = ServiceInfo->VideoPID;
    if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_PART2) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H264) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG4_H263))
    {
      HDFound = TRUE;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d\n", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG1) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG2))
    {
      HDFound = FALSE;
      printf("VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d\n", ServiceInfo->VideoStreamType, VideoPID, isHDVideo);
    }
    else
    {
      VideoPID = 0;
      printf("LoadInfFile() W0901: Unknown video stream type.\n");
    }

if (RecHeaderInfo->Reserved != 0)
  printf("DEBUG! Assertion Error: Reserved-Flags is not 0.\n");

    if (HDFound != isHDVideo)
    {
      printf("LoadInfFile() E0907: Inconsistant video type: inf=%s, rec=%s.\n", (HDFound ? "HD" : "SD"), (isHDVideo ? "HD" : "SD"));
      free(InfBuffer); InfBuffer = NULL;
      TRACEEXIT;
      return FALSE;
    }

    InfDuration = 60*RecHeaderInfo->DurationMin + RecHeaderInfo->DurationSec;

    if (RecHeaderInfo->rs_HasBeenStripped)
      AlreadyStripped = TRUE;
  }

  TRACEEXIT;
  return TRUE;
}

bool SetInfCryptFlag(const char *AbsInfFile)
{
  FILE                 *fInfIn;
  TYPE_RecHeader_Info   RecHeaderInfo;
  bool                  ret = FALSE;

  if ((fInfIn = fopen(AbsInfFile, "r+b")))
  {
    fread(&RecHeaderInfo, 1, 18, fInfIn);
    rewind(fInfIn);
    RecHeaderInfo.CryptFlag = RecHeaderInfo.CryptFlag | 1;
    ret = (fwrite(&RecHeaderInfo, 1, 18, fInfIn) == 18);
    ret = ret && fclose(fInfIn);
  }
  return ret;
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
      if (DoStrip)
      {
        RecHeaderInfo->rs_ToBeStripped = FALSE;
        RecHeaderInfo->rs_HasBeenStripped = TRUE;
      }
      RecHeaderInfo->rbn_HasBeenScanned = TRUE;
      if (NewDurationMS)
      {
        RecHeaderInfo->DurationMin = (word)((NewDurationMS + 500) / 60000);
        RecHeaderInfo->DurationSec = ((NewDurationMS + 500) / 1000) % 60;
      }
      if (NewStartTimeOffset)
        RecHeaderInfo->StartTime = AddTime(RecHeaderInfo->StartTime, NewStartTimeOffset / 60000);
      Result = (fwrite(InfBuffer, 1, InfSize, fInfOut) == InfSize);
    }

    // Passe die Source-inf (falls vorhanden) an
    fInfIn = fopen(AbsSourceInf, "r+b");
    if(fInfIn)
    {
      fread(RecHeaderInfo, 1, 8, fInfIn);
      rewind(fInfIn);
      if (DoStrip)
        RecHeaderInfo->rs_ToBeStripped = FALSE;
      RecHeaderInfo->rbn_HasBeenScanned = TRUE;
      fwrite(RecHeaderInfo, 1, 8, fInfIn);
    }

    // Kopiere den Rest der Source-inf (falls vorhanden) in die neue inf hinein
    if (fInfOut && fInfIn && !RebuildInf)
    {
      fseek(fInfIn, InfSize, SEEK_SET);
      do {
        BytesRead = fread(InfBuffer, 1, 32768, fInfIn);
        if (BytesRead > 0)
          Result = (fwrite(InfBuffer, 1, BytesRead, fInfOut) == BytesRead) && Result;
      } while (BytesRead > 0);
    }

    // Schlieﬂe die bearbeiteten Dateien
    if (fInfIn) fclose(fInfIn);
    if (fInfOut)
    {
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
