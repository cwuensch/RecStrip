/*
  RecStrip for Topfield PVR
  (C) 2015 Christian Wünsch

  Based on Naludump 0.1.1 by Udo Richter
  Concepts from NaluStripper (Marten Richter)
  Concepts from Mpeg2cleaner (Stefan Pöschel)
  Contains portions of VDR (Klaus Schmidinger)
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
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../../../../API/TMS/include/type.h"

#define BUFSIZE        65536
#define FBLIB_DIR_SIZE   512
#define NRBOOKMARKS      177   // eigentlich werden nur 48 Bookmarks unterstützt!! (SRP2401)
#define NRSEGMENTMARKER  101

#ifdef _WIN32
  #define inline
  #define __attribute__(a)
  #define stat64 _stat64
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
  #define snprintf c99_snprintf
  #define vsnprintf c99_vsnprintf

  inline int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
  {
    int count = -1;
    if (size != 0)
      count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
    if (count == -1)
      count = _vscprintf(format, ap);
    return count;
  }

  inline int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
  {
    int count;
    va_list ap;
    va_start(ap, format);
    count = c99_vsnprintf(outBuf, size, format, ap);
    va_end(ap);
    return count;
  }
  #pragma pack(1)
#endif


#define STREAM_VIDEO_MPEG1              0x01
#define STREAM_VIDEO_MPEG2              0x02
#define STREAM_VIDEO_MPEG4_PART2        0x10
#define STREAM_VIDEO_MPEG4_H263         0x1A
#define STREAM_VIDEO_MPEG4_H264         0x1B
#define STREAM_VIDEO_VC1                0xEA
#define STREAM_VIDEO_VC1SM              0xEB
#define STREAM_UNKNOWN                  0xFF

typedef enum
{
  ST_UNKNOWN,
  ST_S,
  ST_T,
  ST_C,
  ST_T5700,
  ST_TMSS,
  ST_TMST,
  ST_TMSC,
  ST_T5800,
  ST_ST,
  ST_CT,
  ST_TF7k7HDPVR,
  ST_NRTYPES
} SYSTEM_TYPE;

typedef struct
{
  char                  HeaderMagic[4];
  word                  HeaderVersion;
  byte                  HeaderUnknown2[2];
  dword                 HeaderStartTime;
  word                  HeaderDuration;
  word                  HeaderDurationSec;

  word                  CryptFlag:2;  // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  Flags:6;
  word                  Flags2:6;
  word                  TSFlag:1;
  word                  CopyFlag:1;

  byte                  HeaderUnknown4[10];
} TYPE_RecHeader_Info;

typedef struct
{
  byte                  SatIdx;
  byte                  ServiceType;

  word                  TPIdx:10;   // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  TunerNum:2;
  word                  DelFlag:1;
  word                  CASFlag:1;
  word                  LockFlag:1;
  word                  SkipFlag:1;

  word                  SVCID;
  word                  PMTPID;
  word                  PCRPID;
  word                  VideoPID;
  word                  AudioPID;

  char                  ServiceName[24];

  byte                  VideoStreamType;
  byte                  AudioStreamType;
} TYPE_Service_Info;

typedef struct
{
  byte                  EventUnknown1[2];
  byte                  EventDurationMin;
  byte                  EventDurationHr;
  dword                 EventID;
  dword                 EventStartTime;
  dword                 EventEndTime;
  byte                  EventRunningStatus;
  byte                  EventTextLength;
  byte                  EventParentalRate;
  char                  EventNameDescription[273];
} TYPE_Event_Info;

typedef struct
{
  word                  ExtEventServiceID;
  word                  ExtEventTextLength;
  dword                 ExtEventEventID;
  char                  ExtEventText[1024];
} TYPE_ExtEvent_Info;

typedef struct
{
  dword                 NrBookmarks;
  dword                 Bookmarks[177];
  dword                 Resume;
} TYPE_Bookmark_Info;

typedef struct
{
  byte                  SatIdx;
  word                  Polar:1;              // 0=V, 1=H
  word                  unused1:3;
  word                  ModulationSystem:1;   // 0=DVBS, 1=DVBS2
  word                  ModulationType:2;     // 0=Auto, 1=QPSK, 2=8PSK, 3=16QAM
  word                  FECMode:4;            // 0x0 = AUTO, 0x1 = 1_2, 0x2 = 2_3, 0x3 = 3_4,
                                            // 0x4 = 5_6 , 0x5 = 7_8, 0x6 = 8_9, 0x7 = 3_5,
                                            // 0x8 = 4_5, 0x9 = 9_10, 0xa = reserved, 0xf = NO_CONV
  word                  Pilot:1;
  word                  unused2:4;
  byte                  unused3;
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  AllowTimeSync:1;
  word                  unused4:15;
  word                  OriginalNetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMSS;

typedef struct
{
  byte                  SatIdx;
  byte                  ChannelNr;
  byte                  Bandwidth;
  byte                  unused1;
  dword                 Frequency;
  word                  TSID;
  byte                  LPHP;
  byte                  unused2;
  word                  OriginalNetworkID;
  word                  NetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMST;

typedef struct
{
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  OriginalNetworkID;
  byte                  ModulationType;
  byte                  unused1;
}__attribute__((packed)) TYPE_TpInfo_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSS      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSS;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSC      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMST      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMST;

typedef struct
{
  dword                 SHOffset; // = (FrameType shl 24) or SHOffset
  byte                  MPEGType;
  byte                  FrameIndex;
  byte                  Field5;
  byte                  Zero1;
  dword                 PHOffsetHigh;
  dword                 PHOffset;
  dword                 PTS2;
  dword                 NextPH;
  dword                 Timems;
  dword                 Zero5;
} tnavSD;

typedef struct
{
  byte                  Version;
  unsigned long long    RecFileSize;
  dword                 NrSegmentMarker;
  dword                 ActiveSegment;
}__attribute__((packed)) tCutHeader1;

typedef struct
{
  word                  Version;
  unsigned long long    RecFileSize;
  word                  NrSegmentMarker;
  word                  ActiveSegment;
  word                  Padding;
}__attribute__((packed)) tCutHeader2;

typedef struct
{
  dword                 Block;  //Block nr
  dword                 Timems; //Time in ms
  float                 Percent;
  int                   Selected;
} tSegmentMarker;



// Globale Variablen
char                    RecFileIn[FBLIB_DIR_SIZE], RecFileOut[FBLIB_DIR_SIZE];
word                    VideoPID = 0;
bool                    isHDVideo = TRUE;
byte                    PACKETSIZE, PACKETOFFSET;

FILE                   *fIn = NULL, *fOut = NULL;
FILE                   *fNavIn = NULL, *fNavOut = NULL;

byte                   *InfBuffer = NULL;
TYPE_RecHeader_Info    *RecHeaderInfo = NULL;
TYPE_Bookmark_Info     *BookmarkInfo = NULL;
SYSTEM_TYPE             SystemType = ST_TMSS;
size_t                  InfSize = 0;

tSegmentMarker         *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
int                     NrSegmentMarker = 0;
int                     ActiveSegment = 0;

unsigned long long int  CurrentPacket = 0, DroppedPackets = 0;
unsigned long long int  CurrentPosition = 0, PositionOffset = 0;



// ----------------------------------------------
// *****  READ AND WRITE INF FILE  *****
// ----------------------------------------------

bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize)
{
  struct stat64         statbuf;
  bool                  ret = FALSE;

  if(AbsFileName)
  {
    ret = (stat64(AbsFileName, &statbuf) == 0);
    if (ret && OutFileSize)
      *OutFileSize = statbuf.st_size;
  }
  return ret;
}

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
    isHDVideo = TRUE;
  else if ((ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG1) || (ServiceInfo->VideoStreamType==STREAM_VIDEO_MPEG2))
    isHDVideo = FALSE;
  else
  {
    printf("LoadInfFile() E0904: Unknown video stream type.\n");
    return FALSE;
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


// ----------------------------------------------
// *****  READ AND WRITE CUT FILE  *****
// ----------------------------------------------

void SecToTimeString(dword Time, char *const OutTimeString)  // needs max. 4 + 1 + 2 + 1 + 2 + 1 = 11 chars
{
  dword                 Hour, Min, Sec;

  if(OutTimeString)
  {
    Hour = (Time / 3600);
    Min  = (Time / 60) % 60;
    Sec  = Time % 60;
    if (Hour >= 10000) Hour = 9999;
    snprintf(OutTimeString, 11, "%lu:%02lu:%02lu", Hour, Min, Sec);
  }
}

void MSecToTimeString(dword Timems, char *const OutTimeString)  // needs max. 4 + 1 + 2 + 1 + 2 + 1 + 3 + 1 = 15 chars
{
  dword                 Hour, Min, Sec, Millisec;

  if(OutTimeString)
  {
    Hour = (Timems / 3600000);
    Min  = (Timems / 60000) % 60;
    Sec  = (Timems / 1000) % 60;
    Millisec = Timems % 1000;
    snprintf(OutTimeString, 15, "%lu:%02lu:%02lu,%03lu", Hour, Min, Sec, Millisec);
  }
}

dword TimeStringToMSec(char *const TimeString)
{
  dword                 Hour=0, Min=0, Sec=0, Millisec=0, ret=0;

  if(TimeString)
  {
    if (sscanf(TimeString, "%lu:%lu:%lu%*1[.,]%lu", &Hour, &Min, &Sec, &Millisec) == 4)
      ret = 1000*(60*(60*Hour + Min) + Sec) + Millisec;
  }
  return ret;
}

void GetCutNameFromRec(const char *RecFileName, char *const OutCutFileName)
{
  char *p = NULL;

  if (RecFileName && OutCutFileName)
  {
    snprintf(OutCutFileName, FBLIB_DIR_SIZE, "%s", RecFileName);
    if ((p = strrchr(OutCutFileName, '.')) == NULL)
      p = &OutCutFileName[strlen(OutCutFileName)];
    snprintf(p, 5, ".cut");
  }
}

bool CutFileDecodeBin(FILE *fCut, unsigned long long *OutSavedSize)
{
  byte                  Version;
  int                   SavedNrSegments = 0;
  bool                  ret = FALSE;

  memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));

  NrSegmentMarker = 0;
  ActiveSegment = 0;
  if (OutSavedSize) *OutSavedSize = 0;

  if (fCut)
  {
    // Check correct version of cut-file
    Version = fgetc(fCut);
    switch (Version)
    {
      case 1:
      {
        rewind(fCut);
        tCutHeader1 CutHeader;
        ret = (fread(&CutHeader, sizeof(CutHeader), 1, fCut) == 1);
        if (ret)
        {
          *OutSavedSize = CutHeader.RecFileSize;
          SavedNrSegments = CutHeader.NrSegmentMarker;
          ActiveSegment = CutHeader.ActiveSegment;
        }
        break;
      }
      case 2:
      {
        rewind(fCut);
        tCutHeader2 CutHeader;
        ret = (fread(&CutHeader, sizeof(CutHeader), 1, fCut) == 1);
        if (ret)
        {
          *OutSavedSize = CutHeader.RecFileSize;
          SavedNrSegments = CutHeader.NrSegmentMarker;
          ActiveSegment = CutHeader.ActiveSegment;
        }
        break;
      }
      default:
        printf("CutFileDecodeBin: .cut version mismatch!\n");
    }

    if (ret)
    {
      SavedNrSegments = min(SavedNrSegments, NRSEGMENTMARKER);
      NrSegmentMarker = fread(SegmentMarker, sizeof(tSegmentMarker), SavedNrSegments, fCut);
      if (NrSegmentMarker < SavedNrSegments)
        printf("CutFileDecodeBin: Unexpected end of file!\n");
    }
  }
  return ret;
}

bool CutFileDecodeTxt(FILE *fCut, unsigned long long *OutSavedSize)
{
  char                  Buffer[512];
  unsigned long long    SavedSize = 0;
  int                   SavedNrSegments = 0;
  bool                  HeaderMode=FALSE, SegmentsMode=FALSE;
  char                  TimeStamp[16];
  char                 *c, Selected;
  int                   p;
  bool                  ret = FALSE;

  memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));

  NrSegmentMarker = 0;
  ActiveSegment = 0;
  if (OutSavedSize) *OutSavedSize = 0;

  if (fCut)
  {
    // Check the first line
    if (fgets(Buffer, sizeof(Buffer), fCut) >= 0)
    {
      if (strncmp(Buffer, "[MCCut3]", 8) == 0)
      {
        HeaderMode = TRUE;
        ret = TRUE;
      }
    }

    while (ret && (fgets(Buffer, sizeof(Buffer), fCut) >= 0))
    {
      //Interpret the following characters as remarks: //
      c = strstr(Buffer, "//");
      if(c) *c = '\0';

      // Remove line breaks in the end
      p = strlen(Buffer);
      while (p && (Buffer[p-1] == '\r' || Buffer[p-1] == '\n' || Buffer[p-1] == ';'))
        Buffer[--p] = '\0';

      // Kommentare und Sektionen
      switch (Buffer[0])
      {
        case '\0':
          continue;

        case '%':
        case ';':
        case '#':
        case '/':
          continue;

        // Neue Sektion gefunden
        case '[':
        {
          HeaderMode = FALSE;
          // Header überprüfen
          if ((SavedSize <= 0) || (SavedNrSegments < 0))
          {
            ret = FALSE;
            break;
          }
          if (strcmp(Buffer, "[Segments]") == 0)
            SegmentsMode = TRUE;
          continue;
        }
      }

      // Header einlesen
      if (HeaderMode)
      {
        char            Name[50];
        unsigned long long Value;

        if (sscanf(Buffer, "%49[^= ] = %lld", Name, &Value) == 2)
        {
          if (strcmp(Name, "RecFileSize") == 0)
          {
            SavedSize = Value;
            if (OutSavedSize) *OutSavedSize = SavedSize;
          }
          else if (strcmp(Name, "NrSegmentMarker") == 0)
            SavedNrSegments = (int)Value;
//          else if (strcmp(Name, "ActiveSegment") == 0)
//            ActiveSegment = Value;
        }
      }

      // Segmente einlesen
      else if (SegmentsMode)
      {
        //[Segments]
        //#Nr. ; Sel ; StartBlock ; StartTime ; Percent
        if (sscanf(Buffer, "%*i ; %c ; %lu ; %16[^;\r\n] ; %f%%", &Selected, &SegmentMarker[NrSegmentMarker].Block, TimeStamp, &SegmentMarker[NrSegmentMarker].Percent) >= 3)
        {
          SegmentMarker[NrSegmentMarker].Selected = (Selected == '*');
          SegmentMarker[NrSegmentMarker].Timems = (TimeStringToMSec(TimeStamp));
          NrSegmentMarker++;
        }
      }
    }
    free(Buffer);

    if (ret)
    {
      if (NrSegmentMarker != SavedNrSegments)
        printf("CutFileDecodeTxt: Invalid number of segments read (%d of %d)!\n", NrSegmentMarker, SavedNrSegments);
    }
    else
      printf("CutFileDecodeTxt: Invalid cut file format!\n");
  }
  return ret;
}

bool CutFileLoad(const char *AbsCutName)
{
  FILE                 *fCut = NULL;
  byte                  Version;
  unsigned long long    RecFileSize, SavedSize;
  bool                  ret = FALSE;

  // Schaue zuerst im Cut-File nach
  fCut = fopen(AbsCutName, "rb");
  if(fCut)
  {
    Version = fgetc(fCut);
    if (Version == '[') Version = 3;
    rewind(fCut);

    printf("CutFileLoad: Importing cut-file version %hhu\n", Version);
    switch (Version)
    {
      case 1:
      case 2:
      {
        ret = CutFileDecodeBin(fCut, &SavedSize);
        break;
      }
      case 3:
      default:
      {
        ret = CutFileDecodeTxt(fCut, &SavedSize);
        break;
      }
    }
    fclose(fCut);

    // Check, if size of rec-File has been changed
    if (ret)
    {
      HDD_GetFileSize(RecFileIn, &RecFileSize);
      if (RecFileSize != SavedSize)
      {
        printf("CutFileLoad: .cut file size mismatch!\n");
        return FALSE;
      }
    }
    else
      printf("CutFileLoad: Failed to read cut-info from .cut!\n");
  }
  else
    printf("CutFileLoad: Cannot open cut file %s!\n", AbsCutName);

  return ret;
}

bool CutFileSave(const char* AbsCutName)
{
  FILE                 *fCut = NULL;
  char                  TimeStamp[16];
  unsigned long long    RecFileSize;
  int                   i;
  bool                  ret = TRUE;

  // neues CutFile speichern
  if (SegmentMarker)
  {
    if (!HDD_GetFileSize(RecFileOut, &RecFileSize))
      printf("CutFileSave: Could not detect size of recording!\n"); 

    fCut = fopen(AbsCutName, "wb");
    if(fCut)
    {
      ret = (fprintf(fCut, "[MCCut3]\r\n") > 0) && ret;
      ret = (fprintf(fCut, "RecFileSize=%llu\r\n", RecFileSize) > 0) && ret;
      ret = (fprintf(fCut, "NrSegmentMarker=%d\r\n", NrSegmentMarker) > 0) && ret;
      ret = (fprintf(fCut, "ActiveSegment=%d\r\n\r\n", ActiveSegment) > 0) && ret;  // sicher!?
      ret = (fprintf(fCut, "[Segments]\r\n") > 0) && ret;
      ret = (fprintf(fCut, "#Nr ; Sel ; StartBlock ;     StartTime ; Percent\r\n") > 0) && ret;
      for (i = 0; i < NrSegmentMarker; i++)
      {
        MSecToTimeString(SegmentMarker[i].Timems, TimeStamp);
        ret = (fprintf(fCut, "%3d ;  %c  ; %10lu ;%14s ;  %5.1f%%\r\n", i, (SegmentMarker[i].Selected ? '*' : '-'), SegmentMarker[i].Block, TimeStamp, SegmentMarker[i].Percent) > 0) && ret;
      }
      ret = (fflush(fCut) == 0) && ret;
      ret = (fclose(fCut) == 0) && ret;
    }
  }
  return ret;
}


// ----------------------------------------------
// *****  PROCESS NAV FILE  *****
// ----------------------------------------------

void ProcessNavFile(const unsigned long long CurrentPosition, const unsigned long long PositionOffset)
{
  static tnavSD         NavBuffer[2], *curNavRec = &NavBuffer[0];
  static bool           FirstRun = TRUE;
  static unsigned long long PictureHeaderOffset = 0;

  if (FirstRun && fNavIn)
  {
    // Versuche, nav-Dateien aus Timeshift-Aufnahmen zu unterstützen ***experimentell***
    dword FirstDword = 0;
    fread(&FirstDword, 4, 1, fNavIn);
    if(FirstDword == 0x72767062)  // 'bpvr'
      fseek(fNavIn, 1056, SEEK_SET);
    else
      rewind(fNavIn);

    if (fread(curNavRec, sizeof(tnavSD) * (isHDVideo ? 2 : 1), 1, fNavIn))
      PictureHeaderOffset = ((unsigned long long)(curNavRec->PHOffsetHigh) << 32) | curNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
      fclose(fNavOut); fNavOut = NULL;
    }
    FirstRun = FALSE;
  }

  while (fNavIn && (PictureHeaderOffset < CurrentPosition))
  {
    PictureHeaderOffset -= PositionOffset;
    curNavRec->PHOffsetHigh = PictureHeaderOffset >> 32;
    curNavRec->PHOffset = PictureHeaderOffset & 0xffffffff;

    if (fNavOut && !fwrite(curNavRec, sizeof(tnavSD) * (isHDVideo ? 2 : 1), 1, fNavOut))
    {
      printf("ProcessNavFile(): Error writing to nav file!\n");
      fclose(fNavOut); fNavOut = NULL;
    }

    if (fread(curNavRec, sizeof(tnavSD) * (isHDVideo ? 2 : 1), 1, fNavIn))
      PictureHeaderOffset = ((unsigned long long)(curNavRec->PHOffsetHigh) << 32) | curNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
      fclose(fNavOut); fNavOut = NULL;
    }
  }
}

// ----------------------------------------------
// *****  PROCESS INF / CUT FILE  *****
// ----------------------------------------------
static inline dword CalcBlockSize(unsigned long long Size)
{
  // Workaround für die Division durch BLOCKSIZE (9024)
  // Primfaktorenzerlegung: 9024 = 2^6 * 3 * 47
  // max. Dateigröße: 256 GB (dürfte reichen...)
  return (dword)(Size >> 6) / 141;
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
      int NrSegments = BookmarkInfo->Bookmarks[End - 1];
      if (NrSegments > NRSEGMENTMARKER) NrSegments = NRSEGMENTMARKER;
      Start = End - NrSegments - 5;
    }
    FirstRun = TRUE;
  }

  while ((i < BookmarkInfo->NrBookmarks) && (BookmarkInfo->Bookmarks[i] < CurrentPosition))
  {
    BookmarkInfo->Bookmarks[i] -= PositionOffset;
    i++;
  }

  while (Start && (j < NrSegments) && (BookmarkInfo->Bookmarks[j] < CurrentPosition))
  {
    BookmarkInfo->Bookmarks[j] -= PositionOffset;
    j++;
  }

  if (!ResumeSet && CurrentPosition >= BookmarkInfo->Resume)
  {
    BookmarkInfo->Resume -= PositionOffset;
    ResumeSet = TRUE;
  }
}

void ProcessCutFile(const dword CurrentPosition, const dword PositionOffset)
{
  static int i = 0;

  while ((i < NrSegmentMarker) && (SegmentMarker[i].Block < CurrentPosition))
  {
    SegmentMarker[i].Block -= PositionOffset;
    i++;
  }
}


// ----------------------------------------------
// *****  Analyse von REC-Files  *****
// ----------------------------------------------

bool isPacketStart(const byte PacketArray[], int ArrayLen)
{
  int                   i;
  bool                  ret = TRUE;

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
  return ret;
}

int GetPacketSize(char *RecFileName)
{
  char                 *p;
  bool                  ret = FALSE;

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
    int                 f = -1;

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
  return (ret ? PACKETSIZE : 0);
}


// ----------------------------------------------
// *****  NALU Dump  *****
// ----------------------------------------------
#define TS_SIZE               188
#define TS_PID_MASK_HI        0x1F
#define TS_PAYLOAD_START      0x40
#define TS_ADAPT_FIELD_EXISTS 0x20
#define TS_PAYLOAD_EXISTS     0x10
#define TS_CONT_CNT_MASK      0x0F

inline int TsPid(const byte *p)
{
  return (p[1] & TS_PID_MASK_HI) * 256 + p[2];
}

inline bool TsHasPayload(const byte *p)
{
  return ((p[3] & TS_PAYLOAD_EXISTS) != 0);
}

inline bool TsHasAdaptationField(const byte *p)
{
  return ((p[3] & TS_ADAPT_FIELD_EXISTS) != 0);
}

inline bool TsPayloadStart(const byte *p)
{
  return ((p[1] & TS_PAYLOAD_START) != 0);
}

inline int TsPayloadOffset(const byte *p)
{
  int o = (p[3] & TS_ADAPT_FIELD_EXISTS) ? p[4] + 5 : 4;
  return o <= TS_SIZE ? o : TS_SIZE;
}

inline int TsContinuityCounter(const byte *p)
{
  return p[3] & TS_CONT_CNT_MASK;
}

inline void TsSetContinuityCounter(byte *p, int Counter)
{
  p[3] = (p[3] & ~TS_CONT_CNT_MASK) | (Counter & TS_CONT_CNT_MASK);
}

void TsExtendAdaptionField(byte *Packet, int ToLength)
{
  // Hint: ExtenAdaptionField(p, TsPayloadOffset(p) - 4) is a null operation

  int Offset = TsPayloadOffset(Packet); // First byte after existing adaption field

  if (ToLength <= 0)
  {
    // Remove adaption field
    Packet[3] = Packet[3] & ~TS_ADAPT_FIELD_EXISTS;
    return;
  }

  // Set adaption field present
  Packet[3] = Packet[3] | TS_ADAPT_FIELD_EXISTS;

  // Set new length of adaption field:
  Packet[4] = ToLength <= TS_SIZE-4 ? ToLength-1 : TS_SIZE-4-1;

  if (Packet[4] == TS_SIZE-4-1)
  {
    // No more payload, remove payload flag
    Packet[3] = Packet[3] & ~TS_PAYLOAD_EXISTS;
  }

  int NewPayload = TsPayloadOffset(Packet); // First byte after new adaption field

  // Fill new adaption field
  if (Offset == 4 && Offset < NewPayload)
    Offset++; // skip adaptation_field_length
  if (Offset == 5 && Offset < NewPayload)
    Packet[Offset++] = 0; // various flags set to 0
  while (Offset < NewPayload)
    Packet[Offset++] = 0xff; // stuffing byte
}


enum eNaluFillState {
  NALU_NONE=0,    // currently not NALU fill stream
  NALU_FILL,      // Within NALU fill stream, 0xff bytes and NALU start code in byte 0
  NALU_TERM,      // Within NALU fill stream, read 0x80 terminating byte
  NALU_END        // Beyond end of NALU fill stream, expecting 0x00 0x00 0x01 now
};

eNaluFillState NaluFillState = NALU_NONE;

unsigned int History = 0xffffffff;

int LastContinuityInput = -1;
int LastContinuityOutput = -1;
//int ContinuityOffset = 0;

bool DropAllPayload = FALSE;

int PesId = -1;
int PesOffset = 0;

int NaluOffset = 0;


struct sPayloadInfo {
  int DropPayloadStartBytes;
  int DropPayloadEndBytes;
  bool DropAllPayloadBytes;
};


/*void NaluDumpReset(void)
{
  LastContinuityInput = -1;
//  ContinuityOffset = 0;
  PesId = -1;
  PesOffset = 0;
  NaluFillState = NALU_NONE;
  NaluOffset = 0;
  History = 0xffffffff;
  DropAllPayload = false;
}*/

void ProcessPayload(unsigned char *Payload, int size, bool PayloadStart, sPayloadInfo &Info)
{
  Info.DropPayloadStartBytes = 0;
  Info.DropPayloadEndBytes = 0;
  int LastKeepByte = -1;

  if (PayloadStart)
  {
    History = 0xffffffff;
    PesId = -1;
    NaluFillState = NALU_NONE;
  }

  for (int i=0; i<size; i++)
  {
    History = (History << 8) | Payload[i];

    PesOffset++;
    NaluOffset++;

    bool DropByte = false;

    if (History >= 0x00000180 && History <= 0x000001FF)
    {
      // Start of PES packet
      PesId = History & 0xff;
      PesOffset = 0;
      NaluFillState = NALU_NONE;
    }
    else if (PesId >= 0xe0 && PesId <= 0xef // video stream
     && History >= 0x00000100 && History <= 0x0000017F) // NALU start code
    {
      int NaluId = History & 0xff;
      NaluOffset = 0;
      NaluFillState = ((NaluId & 0x1f) == 0x0c) ? NALU_FILL : NALU_NONE;
    }

    if (PesId >= 0xe0 && PesId <= 0xef // video stream
     && PesOffset >= 1 && PesOffset <= 2)
    {
      Payload[i] = 0; // Zero out PES length field
    }

    if (NaluFillState == NALU_FILL && NaluOffset > 0) // Within NALU fill data
    {
      // We expect a series of 0xff bytes terminated by a single 0x80 byte.

      if (Payload[i] == 0xFF)
      {
        DropByte = true;
      }
      else if (Payload[i] == 0x80)
      {
        NaluFillState = NALU_TERM; // Last byte of NALU fill, next byte sets NaluFillEnd=true
        DropByte = true;
      }
      else // Invalid NALU fill
      {
        printf("cNaluDumper: Unexpected NALU fill data: %02x\n", Payload[i]);
        NaluFillState = NALU_END;
        if (LastKeepByte == -1)
        {
          // Nalu fill from beginning of packet until last byte
          // packet start needs to be dropped
          Info.DropPayloadStartBytes = i;
        }
      }
    }
    else if (NaluFillState == NALU_TERM) // Within NALU fill data
    {
      // We are after the terminating 0x80 byte
      NaluFillState = NALU_END;
      if (LastKeepByte == -1)
      {
        // Nalu fill from beginning of packet until last byte
        // packet start needs to be dropped
        Info.DropPayloadStartBytes = i;
      }
    }

    if (!DropByte)
      LastKeepByte = i; // Last useful byte
  }

  Info.DropAllPayloadBytes = (LastKeepByte == -1);
  Info.DropPayloadEndBytes = size-1-LastKeepByte;
}

bool ProcessTSPacket(unsigned char *Packet)
{
  int ContinuityOffset = 0;
  bool HasAdaption = TsHasAdaptationField(Packet);
  bool HasPayload = TsHasPayload(Packet);

  // Check continuity:
  int ContinuityInput = TsContinuityCounter(Packet);
  if (LastContinuityInput >= 0)
  {
    int NewContinuityInput = HasPayload ? (LastContinuityInput + 1) & TS_CONT_CNT_MASK : LastContinuityInput;
    ContinuityOffset = (NewContinuityInput - ContinuityInput) & TS_CONT_CNT_MASK;
    if (ContinuityOffset > 0)
      printf("cNaluDumper: TS continuity offset %i\n", ContinuityOffset);
//    if (Offset > ContinuityOffset)
//      ContinuityOffset = Offset; // max if packets get dropped, otherwise always the current one.
  }
  LastContinuityInput = ContinuityInput;

  if (HasPayload) {
    sPayloadInfo Info;
    int Offset = TsPayloadOffset(Packet);
    ProcessPayload(Packet + Offset, TS_SIZE - Offset, TsPayloadStart(Packet), Info);

    if (DropAllPayload && !Info.DropAllPayloadBytes)
    {
      // Return from drop packet mode to normal mode
      DropAllPayload = false;

      // Does the packet start with some remaining NALU fill data?
      if (Info.DropPayloadStartBytes > 0)
      {
        // Add these bytes as stuffing to the adaption field.

        // Sample payload layout:
        // FF FF FF FF FF 80 00 00 01 xx xx xx xx
        //     ^DropPayloadStartBytes

        TsExtendAdaptionField(Packet, Offset - 4 + Info.DropPayloadStartBytes);
      }
    }

    bool DropThisPayload = DropAllPayload;

    if (!DropAllPayload && Info.DropPayloadEndBytes > 0) // Payload ends with 0xff NALU Fill
    {
      // Last packet of useful data
      // Do early termination of NALU fill data
      Packet[TS_SIZE-1] = 0x80;
      DropAllPayload = true;
      // Drop all packets AFTER this one

      // Since we already wrote the 0x80, we have to make sure that
      // as soon as we stop dropping packets, any beginning NALU fill of next
      // packet gets dumped. (see DropPayloadStartBytes above)
    }

    if (DropThisPayload && HasAdaption)
    {
      // Drop payload data, but keep adaption field data
      TsExtendAdaptionField(Packet, TS_SIZE-4);
      DropThisPayload = false;
    }

    if (DropThisPayload)
    {
      return true; // Drop packet
    }
  }

  // Fix Continuity Counter and reproduce incoming offsets:
  int NewContinuityOutput = ContinuityInput;
  if (LastContinuityOutput >= 0)
    NewContinuityOutput = TsHasPayload(Packet) ? (LastContinuityOutput + 1) & TS_CONT_CNT_MASK : LastContinuityOutput;
  NewContinuityOutput = (NewContinuityOutput + ContinuityOffset) & TS_CONT_CNT_MASK;
  TsSetContinuityCounter(Packet, NewContinuityOutput);
  LastContinuityOutput = NewContinuityOutput;
  ContinuityOffset = 0;

  return false; // Keep packet
}


// ----------------------------------------------
// *****  MAIN FUNCTION  *****
// ----------------------------------------------

int main(int argc, const char* argv[])
{
  char                  NavFileIn[FBLIB_DIR_SIZE], NavFileOut[FBLIB_DIR_SIZE], InfFileIn[FBLIB_DIR_SIZE], InfFileOut[FBLIB_DIR_SIZE], CutFileIn[FBLIB_DIR_SIZE], CutFileOut[FBLIB_DIR_SIZE];
  byte                  Buffer[192];
  int                   ReadBytes;
  time_t                startTime, endTime;

  printf("\nRecStrip for Topfield PVR v0.1\n");
  printf("(C) 2015 Christian Wuensch\n");
  printf("- based on Naludump 0.1.1 by Udo Richter -\n");
  printf("- portions of VDR (K. Schmidinger), Mpeg2cleaner (S. Poeschel) and MovieCutter -\n");


/*tSegmentMarker testHead;
testHead.Block=2;
testHead.Selected=1;
testHead.Percent=3;
testHead.Timems=0;
FILE* test = fopen("D:/Topfield/TAP/SamplesTMS/MovieCutter/Schnitt/test.txt", "wb");
int wr = fwrite(&testHead, sizeof(testHead), 1, test);
fclose(test);
return 0;*/





  // Eingabe-Parameter prüfen
  if (argc > 2)
  {
    strncpy(RecFileIn, argv[1], sizeof(RecFileIn));
    if (argc > 2)
      strncpy(RecFileOut, argv[2], sizeof(RecFileOut));
  }
  else
  {
    printf("\nUsage: %s <source-rec> <dest-rec>\n\n", argv[0]);
    exit(1);
  }

  // Input-File öffnen
  printf("\nInput file: %s\n", RecFileIn);
  fIn = fopen(RecFileIn, "rb");
  if (fIn)
  {
    setvbuf(fIn, NULL, _IOFBF, BUFSIZE);
    GetPacketSize(RecFileIn);
  }
  else
  {
    printf("ERROR: Cannot open %s.\n", RecFileIn);
    exit(2);
  }

  // ggf. Output-File öffnen
  if (argc > 2)
  {
    printf("Output file: %s\n", RecFileOut);
    fOut = fopen(RecFileOut, "wb");
    if (fOut)
      setvbuf(fOut, NULL, _IOFBF, BUFSIZE);
    else
    {
      fclose(fIn);
      printf("ERROR: Cannot create %s.\n", RecFileOut);
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
  }
  else
  {
    printf("WARNING: Cannot open inf file %s.\n", InfFileIn);
    InfFileIn[0] = '\0';
  }

  // ggf. nav-Files öffnen
  snprintf(NavFileIn, sizeof(NavFileIn), "%s.nav", RecFileIn);
  printf("\nNav file: %s\n", NavFileIn);
  fNavIn = fopen(NavFileIn, "rb");
  if (fNavIn)
  {
    if (argc > 2)
    {
      snprintf(NavFileOut, sizeof(NavFileOut), "%s.nav", RecFileOut);
      printf("Nav output: %s\n", NavFileOut);
      fNavOut = fopen(NavFileOut, "wb");
      if (!fNavIn)
        printf("WARNING: Cannot create nav file %s.\n", NavFileOut);
    }
  }
  else
    printf("WARNING: Cannot open nav file %s.\n", NavFileIn);

  // ggf. cut-File einlesen
  GetCutNameFromRec(RecFileIn, CutFileIn);
  printf("\nCut file: %s\n", CutFileIn);

  // Puffer allozieren
  SegmentMarker = (tSegmentMarker*) malloc(NRSEGMENTMARKER * sizeof(tSegmentMarker));

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

  // Datei paketweise einlesen und verarbeiten
  time(&startTime);
  while (true)
  {
    ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);

    // alle nav-Einträge, deren Position < CurrentPosition sind, um PositionOffset reduzieren und ausgeben
    ProcessNavFile(CurrentPosition, PositionOffset);

    // alle Bookmarks / cut-Einträge, deren Position <= CurrentPosition/9024 sind, um PositionOffset/9024 reduzieren
    dword CurPosBlocks = CalcBlockSize(CurrentPosition);
    dword PosOffsetBlocks = CalcBlockSize(PositionOffset);
    ProcessInfFile(CurPosBlocks, PosOffsetBlocks);
    ProcessCutFile(CurPosBlocks, PosOffsetBlocks);


    if (ReadBytes > 0)
    {
      if (/*(TsPid(&Buffer[PACKETOFFSET]) == 0x12) ||*/ ((TsPid(&Buffer[PACKETOFFSET]) == VideoPID) && (ProcessTSPacket(&Buffer[PACKETOFFSET]) == TRUE)))
      {
        // Paket wird entfernt
        DroppedPackets++;
        PositionOffset += ReadBytes;
      }
      else
      {
        // (ggf. verändertes) Paket wird in Ausgabe geschrieben
        if (fOut && !fwrite(Buffer, ReadBytes, 1, fOut))
        {
          printf("ERROR: Failed writing to output file.\n");
          fclose(fIn);
          fclose(fOut);
          if(fNavIn) fclose(fNavIn);
          if (fNavOut) fclose(fNavOut);
          exit(4);
        }
      }
      CurrentPacket++;
      CurrentPosition += ReadBytes;
    }
    else
      break;
  }
  fclose(fIn);
  if(fOut)
  {
    if (fflush(fOut) != 0 || fclose(fOut) != 0)
    {
      printf("ERROR: Failed closing the output file.\n");
      exit(5);
    }
  }
  if(fNavIn) fclose(fNavIn);
  if (fNavOut)
  {
    if (fflush(fNavOut) != 0 || fclose(fNavOut) != 0)
      printf("WARNING: Failed closing the nav file.\n");
  }

  if (*InfFileIn && (argc > 2) && !SaveInfFile(InfFileOut, InfFileIn))
    printf("WARNING: Cannot create inf %s.\n", InfFileOut);

  if (*CutFileIn && (argc > 2) && !CutFileSave(CutFileOut))
    printf("WARNING: Cannot create cut %s.\n", CutFileOut);

  printf("\nPackets: %lli, Dropped: %lli (%lli%%)\n", CurrentPacket, DroppedPackets, CurrentPacket ? DroppedPackets*100/CurrentPacket : 0);

  time(&endTime);
  printf("\nElapsed time: %f sec.\n", difftime(endTime, startTime));

  free(SegmentMarker);

  #ifdef _WIN32
    getchar();
  #endif
  exit(0);
}
