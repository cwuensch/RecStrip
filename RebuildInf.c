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
#include <sys/stat.h>
#include <time.h>
#ifndef LINUX
 #ifdef _WIN32
  #include <windows.h>
 #else
//  #include <fileapi.h>
  #include <dirent.h>
 #endif
#endif
#include "type.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "PESProcessor.h"
#include "PESFileLoader.h"
#include "RebuildInf.h"
#include "NavProcessor.h"
#include "TtxProcessor.h"
#include "HumaxHeader.h"
#include "EycosHeader.h"
#include "CutProcessor.h"
#include "H264.h"

/*#ifdef _WIN32
  #define timezone _timezone
#else
  extern long timezone;
#endif*/

byte                   *EPGBuffer = NULL;
int                     EPGLen = 0;
long long               FirstFilePCR = 0, LastFilePCR = 0;
dword                   FirstFilePTS = 0, LastFilePTS = 0;
int                     VideoHeight = 0, VideoWidth = 0;
double                  VideoFPS = 0, VideoDAR = 0;
static int              TtxTimeZone = 0;


static inline byte BCD2BIN(byte BCD)
{
  return (BCD >> 4) * 10 + (BCD & 0x0f);
}
static inline byte BIN2BCD(byte BIN)
{
  return (((BIN / 10) << 4) & 0xf0) | ((BIN % 10) & 0x0f);
}

time_t MakeUnixTime(const word year, const byte month, const byte day, const byte hour, const byte minute, const byte second, int *const out_timeoffset)
{
  struct tm timeinfo = {0};
  time_t UnixTimeStamp;
  timeinfo.tm_year = year - 1900;
  timeinfo.tm_mon  = month - 1;    //months since January - [0,11]
  timeinfo.tm_mday = day;          //day of the month - [1,31] 
  timeinfo.tm_hour = hour;         //hours since midnight - [0,23]
  timeinfo.tm_min  = minute;       //minutes after the hour - [0,59]
  timeinfo.tm_sec  = second;       //seconds after the minute - [0,59]
  timeinfo.tm_isdst = -1;          //detect Daylight Saving Time according to the timestamp's date
  UnixTimeStamp = mktime(&timeinfo);  // converts local to UTC time

#ifndef LINUX
  if (out_timeoffset)
  {
    #ifdef _WIN32
      *out_timeoffset = -1*_timezone + 3600*timeinfo.tm_isdst;
    #else
      *out_timeoffset = -1*timezone + 3600*timeinfo.tm_isdst;
    #endif
  }
#endif
  return UnixTimeStamp;
}

static struct tm Unix2TimeStruct(time_t UnixTimeStamp, bool convertToLocal)
{
  struct tm timeinfo = {0};
  if(!UnixTimeStamp) return timeinfo;

#ifndef LINUX
  if (convertToLocal)
  {
    #ifdef _WIN32
      UnixTimeStamp -= _timezone;
      localtime_s(&timeinfo, &UnixTimeStamp);
    #else
      UnixTimeStamp -= timezone;
      localtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    if (timeinfo.tm_isdst)
      UnixTimeStamp += 3600;
  }
#endif

  #ifdef _WIN32
    UnixTimeStamp -= _timezone;
    gmtime_s(&timeinfo, &UnixTimeStamp);
  #else
    UnixTimeStamp -= timezone;
    gmtime_r(&UnixTimeStamp, &timeinfo);
  #endif
  return timeinfo;
}

tPVRTime Unix2TFTime(time_t UnixTimeStamp, byte *const outSec, bool convertToLocal)
{
  if(!UnixTimeStamp) return 0;
#ifndef LINUX
  if (convertToLocal)
  {
    struct tm timeinfo = {0};

    #ifdef _WIN32
      UnixTimeStamp -= _timezone;
      localtime_s(&timeinfo, &UnixTimeStamp);
    #else
      UnixTimeStamp -= timezone;
      localtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    if (timeinfo.tm_isdst)
      UnixTimeStamp += 3600;
  }
#endif

  if (outSec)
    *outSec = UnixTimeStamp % 60;
  return (DATE ( (UnixTimeStamp / 86400) + 0x9e8b, (UnixTimeStamp / 3600) % 24, (UnixTimeStamp / 60) % 60 ));
}

time_t TF2UnixTime(tPVRTime TFTimeStamp, byte TFTimeSec, bool convertToUTC)
{
  time_t Result = (MJD(TFTimeStamp) - 0x9e8b) * 86400 + HOUR(TFTimeStamp) * 3600 + MINUTE(TFTimeStamp) * 60 + TFTimeSec;
  if(!TFTimeStamp) return 0;
#ifndef LINUX
  if (convertToUTC)
  {
    struct tm timeinfo = {0};

    #ifdef _WIN32
      Result += _timezone;
      localtime_s(&timeinfo, &Result);
    #else
      Result += timezone;
      localtime_r(&Result, &timeinfo);
    #endif
    if (timeinfo.tm_isdst)
      Result -= 3600;
  }
#endif

  return Result;
}

tPVRTime EPG2TFTime(tPVRTime TFTimeStamp, int *const out_timeoffset)
{
  struct tm timeinfo = {0};
  int time_offset = 3600;
  time_t UnixTime = TF2UnixTime(TFTimeStamp, 0, FALSE);  // kein Convert, da EPG-Daten mit TtxTimeZone konvertiert werden sollen

#ifndef LINUX
  #ifdef _WIN32
    localtime_s(&timeinfo, &UnixTime);
    time_offset = -1*_timezone + 3600*timeinfo.tm_isdst;
  #else
    localtime_r(&UnixTime, &timeinfo);
    time_offset = -1*timezone + 3600*timeinfo.tm_isdst;
  #endif
#endif

  if(out_timeoffset) *out_timeoffset = time_offset;
  return Unix2TFTime(UnixTime + (TtxTimeZone ? -TtxTimeZone : time_offset), NULL, FALSE);
}

tPVRTime AddTimeSec(tPVRTime pvrTime, byte pvrTimeSec, byte *const outSec, int addSeconds)
{
  word  Day  = MJD(pvrTime);
  short Hour = HOUR(pvrTime);
  short Min  = MINUTE(pvrTime);  
  short Sec = pvrTimeSec;
  TRACEENTER;

/*
  long long             seconds;
  seconds = pvrTimeSec + 60 * (pvrTime.Minute + 60 * (pvrTime.Hour + 24 * (long long)pvrTime.Mjd));
  seconds += addSeconds;

  time.Mjd = (word)(seconds / 86400);
  time.Hour = (byte)((seconds / 3600) % 24);
  time.Minute = (byte)((seconds / 60) % 60);
  if (outSec)
    *outSec = (byte)(seconds % 60); */

  Sec += addSeconds % 60;
  if(Sec < 0)       { Min--; Sec += 60; }
  else if(Sec > 59) { Min++; Sec -= 60; }

  Min += (addSeconds / 60) % 60;
  if(Min < 0)       { Hour--; Min += 60; }
  else if(Min > 59) { Hour++; Min -= 60; }

  Hour += (addSeconds / 3600) % 24;
  if(Hour < 0)      { Day--; Hour += 24; }
  else if(Hour >23) { Day++; Hour -= 24; }

  Day += (addSeconds / 86400);

  if(outSec) *outSec = (byte)Sec;

  TRACEEXIT;
  return (DATE(Day, Hour, Min));
}


static char* rtrim(char *s)
{
  char *ptr;
  if (!s)  return NULL;   // handle NULL string
  if (!*s) return s;      // handle empty string

  ptr = s + strlen(s);
  while((*--ptr) == ' ');
  ptr[1] = '\0';
  return s;
}


// from ProjectX
// Fully Reverses the bit order in a byte: 12345678 -> 87654321
static byte byte_reverse(byte b)
{
  b = (((b >> 1) & 0x55) | ((b << 1) & 0xaa));
  b = (((b >> 2) & 0x33) | ((b << 2) & 0xcc));
  b = (((b >> 4) & 0x0f) | ((b << 4) & 0xf0));
  return b;
}

// Reverts the last nibble in a byte: 00001234 -> 00004321
static byte nibble_reverse(byte b)
{
  return byte_reverse(b << 4);
}

// Decodes Hamming from reversed byte order (like in Teletext), returns reverse-corrected nibble: 00001234
static byte hamming_decode_rev(byte b)
{
  switch (b)
  {
    case 0xa8: return 0;
    case 0x0b: return 8;
    case 0x26: return 4;
    case 0x85: return 12;
    case 0x92: return 2;
    case 0x31: return 10;
    case 0x1c: return 6;
    case 0xbf: return 14;
    case 0x40: return 1;
    case 0xe3: return 9;
    case 0xce: return 5;
    case 0x6d: return 13;
    case 0x7a: return 3;
    case 0xd9: return 11;
    case 0xf4: return 7;
    case 0x57: return 15;
    default:
      return 0xFF;     // decode error , not yet corrected
  }
}

word GetMinimalAudioPID(tAudioTrack AudioPIDs[])
{
  int k;
  word minPID = 0xffff;
  for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0); k++)
  {
    if(AudioPIDs[k].pid < minPID) minPID = AudioPIDs[k].pid;
  }
  if(minPID == 0xffff) minPID = 0;
  return minPID;
}


bool LoadTechnisat(char *AbsTsFileName, TYPE_RecHeader_TMSS *RecInf)
{
  FILE *fAdd;
  char TempStr[FBLIB_DIR_SIZE], *p;
  dword year = 0, month = 0, day = 0;
  bool ret = FALSE;

  p = strrchr(AbsTsFileName, '[');
  if (p && sscanf(p, "[%u . %u . %u]", &day, &month, &year) == 3)
  {
    RecInf->RecHeaderInfo.StartTime = Unix2TFTime(MakeUnixTime((word)year, (byte)month, (byte)day, 0, 0, 0, NULL), NULL, TRUE);
    ret = TRUE;
  }

  // Marken einlesen (es muss zuvor eine .nav erzeugt werden!)
  strncpy(TempStr, AbsTsFileName, sizeof(TempStr));
  TempStr[sizeof(TempStr)-1] = '\0';
  p = strrchr(TempStr, '/');
  if(!p) p = strrchr(TempStr, '\\');
  if(p) p++; else p = TempStr;
  strcpy(p, "Marken.txt");

  if ((fAdd = fopen(TempStr, "r")))
  {
    dword hour = 0, minute = 0, second = 0, millisec = 0, i = 1;
    fgets(TempStr, sizeof(TempStr), fAdd);
    if (sscanf(TempStr, "%u . %u . %u %u _ %u", &day, &month, &year, &hour, &minute) == 5)
      RecInf->RecHeaderInfo.StartTime = Unix2TFTime(MakeUnixTime((word)year, (byte)month, (byte)day, (byte)hour, (byte)minute, 0, NULL), NULL, TRUE);

    ResetSegmentMarkers();
    while (fgets(TempStr, sizeof(TempStr), fAdd))
    {
      if (sscanf(TempStr, "%*[^:] : %u : %u : %u , %u", &hour, &minute, &second, &millisec) == 4)
        SegmentMarker[i++].Timems = 1000 * (60 * (60*hour + minute) + second) + millisec;
    }
    fclose(fAdd);
    SegmentMarker[i].Position = RecFileSize;
    NrSegmentMarker = i + 1;
    CutImportFromTimeStamps(4, PACKETSIZE);
  }
  return ret;
}

bool LoadDVBViewer(char *AbsTsFileName, TYPE_RecHeader_TMSS *RecInf)
{
  FILE                 *fLog;
  char                  LogFile[FBLIB_DIR_SIZE], LogDate[12], *Buffer = NULL, *p;
  time_t                UnixTime1 = 0, UnixTime2 = 0, Duration;
  dword                 day=0, month=0, year=0, hour=0, minute=0, second=0, startH, startM, endH, endM;
  int                   NameLen = 0, c = 0, i;
  bool                  found = FALSE, ret = FALSE;

  TRACEENTER;
//  InitInfStruct(RecInf);
  memset(LogDate, 0, sizeof(LogDate));

  // Zus�tzliche Log-Datei vom DVBViewer laden
  strcpy(LogFile, AbsTsFileName);

  if (strlen(LogFile) > 24)
  {
    LogFile[strlen(LogFile)-23] = '\0';
    if ((p = strrchr(LogFile, '-')) && (p[1] > '1') && (p[1] <= '9') && (p[2]==' '))
    {
      // Falls der Dateiname einen Szenen-Index ("-3") enth�lt, w�hle das erste Logfile der Aufnahme
      *p = '\0';
      strncpy(LogDate, &AbsTsFileName[strlen(AbsTsFileName)-23], 11);

     #ifndef LINUX
      #ifdef _WIN32
      {
        HANDLE hFind;
        WIN32_FIND_DATAA FindFileData;
        strcat(LogFile, "*.log");
        p = strrchr(LogFile, '\\');
        if(!p) p = strrchr(LogFile, '/');
        if(p) p++; else p = LogFile;
        if ((hFind = FindFirstFileA(LogFile, &FindFileData)) != INVALID_HANDLE_VALUE)
        {
          do
            if ((strstr(FindFileData.cFileName, LogDate)) && strstr(FindFileData.cFileName, "-1 "))
              { strncpy(p, FindFileData.cFileName, sizeof(LogFile) - (p-LogFile)); found = TRUE; break; }
          while (FindNextFileA(hFind, &FindFileData));
          FindClose(hFind);
        }
      }
      #else
      {
        DIR *dh = NULL;
        struct dirent *file;
        int len;
        if ((p = strrchr(LogFile, '/')))  *p = '\0';
//printf("DEBUG: Open folder = %s\n", (p ? LogFile : "."));
        if ((dh = opendir(p ? LogFile : ".")))
        {
          if(p) *(p++) = '/'; else p = LogFile;
          len = strlen(p);
//printf("DEBUG: Reference name = %s\n", p);
          while ((file = readdir(dh)))
          {
//printf("DEBUG: File found = %s\n", file->d_name);
            if ((strncmp(file->d_name, p, len) == 0) && (strstr(file->d_name, LogDate)) && (strstr(file->d_name, "-1 ")) && (strcmp(&file->d_name[strlen(file->d_name)-4], ".log") == 0))
              { strncpy(p, file->d_name, sizeof(LogFile) - (p-LogFile)); found = TRUE; break; }
          }
//printf("DEBUG: Final result = %s\n", LogFile);
          closedir(dh);
        }
      }
      #endif
     #endif
    }
    if (!found)  // else
    {
      // Falls nicht, nimm das Log zum .ts
      strcpy(LogFile, AbsTsFileName);
      if((p = strrchr(LogFile, '.'))) *p = '\0';  // ".ts" entfernen
      strcat(LogFile, ".log");
//printf("DEBUG: Final result = %s\n", LogFile);
    }
  }

  if ((fLog = fopen(LogFile, "rb")))
  {
    printf("  Importing DVBViewer logfile (%s)\n", strrchr(LogFile, '\\') ? strrchr(LogFile, '\\')+1 : (strrchr(LogFile, '/') ? strrchr(LogFile, '/')+1 : LogFile));
    if ((Buffer = (char*)malloc(4096)))
    {
      ret = TRUE;
      memset(RecInf->EventInfo.EventNameDescription, 0, sizeof(RecInf->EventInfo.EventNameDescription));
      memset(RecInf->ExtEventInfo.Text, 0, sizeof(RecInf->ExtEventInfo.Text));
      RecInf->ExtEventInfo.TextLength = 0;

      for (i = 0; ret && fgets(Buffer, 4096, fLog); i++)
      {
        // Remove line breaks in the end
        c = (int)strlen(Buffer);
        while (c && (Buffer[c-1] == '\r' || Buffer[c-1] == '\n' || Buffer[c-1] == ';'))
          Buffer[--c] = '\0';
        
        if (c > 0)
        {
          switch (i)
          {
            case 0:
              c = (int)strlen(Buffer);
              if(c > 10)
              {
                ret = ret && (sscanf(&Buffer[c-10], "%2u.%2u.%4u", &day, &month, &year) == 3);
                Buffer[c-11] = '\0';
                StrToUTF8(RecInf->ServiceInfo.ServiceName, Buffer, sizeof(RecInf->ServiceInfo.ServiceName), 0);
                printf("    ServiceName=%s\n", RecInf->ServiceInfo.ServiceName);
              }
              break;
            case 2:
              NameLen = (int) min(strlen(Buffer), sizeof(RecInf->EventInfo.EventNameDescription) - 1);
              StrToUTF8(RecInf->EventInfo.EventNameDescription, Buffer, 4096, 0);
//              RecInf->EventInfo.EventNameDescription[NameLen] = '\0';
              NameLen = (int)strlen(RecInf->EventInfo.EventNameDescription);
              RecInf->EventInfo.EventNameLength = NameLen;
              printf("    EventName = %s\n", RecInf->EventInfo.EventNameDescription);
              break;
            case 3:
              if (sscanf(Buffer, "%2u:%2u..%2u:%2u", &startH, &startM, &endH, &endM) == 4)
              {
                UnixTime1 = MakeUnixTime(year, month, day, startH, startM, 0, NULL);
                UnixTime2 = MakeUnixTime(year, month, ((endH>startH || (endH==startH && endM>=startM)) ? day : day+1), endH, endM, 0, NULL);
                Duration = UnixTime2 - UnixTime1;
                RecInf->EventInfo.StartTime = Unix2TFTime(UnixTime1, NULL, FALSE);
                RecInf->EventInfo.EndTime = Unix2TFTime(UnixTime2, NULL, FALSE);
                RecInf->EventInfo.DurationHour = (byte) (Duration / 3600);
                RecInf->EventInfo.DurationMin = (byte) ((Duration / 60) % 60);
                UnixTime1 = 0;
              }
              else i = 8;
              break;
            case 5:
              if (NameLen + 1 < (int)sizeof(RecInf->EventInfo.EventNameDescription))
              {
                StrToUTF8(&RecInf->EventInfo.EventNameDescription[NameLen], Buffer, sizeof(RecInf->EventInfo.EventNameDescription) - NameLen, 0);
                printf("    EventDesc = %s\n", &RecInf->EventInfo.EventNameDescription[NameLen]);
              }
              break;
            case 7:
            {
              if(ExtEPGText) free(ExtEPGText);
              if (!(ExtEPGText = (char*) malloc(strlen(Buffer) + 1)))
              {
                printf("  Could not allocate memory for ExtEPGText.\n");
                TRACEEXIT;
                return FALSE;
              }
              memset(RecInf->ExtEventInfo.Text, 0, sizeof(RecInf->ExtEventInfo.Text));
              strcpy(ExtEPGText, Buffer);
              StrToUTF8(RecInf->ExtEventInfo.Text, ExtEPGText, (int)sizeof(RecInf->ExtEventInfo.Text), 0);
              if (RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] != '\0')
                snprintf(&RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 4], 4, "...");
//              RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] = '\0';
              RecInf->ExtEventInfo.TextLength = (word)strlen(RecInf->ExtEventInfo.Text);
              printf("    EPGExtEvt = %s\n", ExtEPGText);
              break;
            }
            default:
            {
              if (sscanf(Buffer, "%2u:%2u:%2u", &hour, &minute, &second) == 3)
              {
                UnixTime2 = MakeUnixTime(year, month, ((UnixTime1==0 || hour>startH || (hour==startH && minute>=startM)) ? day : day+1), hour, minute, second, NULL);
                if (!UnixTime1)
                {
                  startH = hour;
                  startM = minute;
                  UnixTime1 = UnixTime2;
                }
              }
              break;
            }
          }
        }
      }
      if (UnixTime1)
      {
        RecInf->RecHeaderInfo.StartTime = Unix2TFTime(UnixTime1, &RecInf->RecHeaderInfo.StartTimeSec, TRUE);
        Duration = (UnixTime2 - UnixTime1);
        RecInf->RecHeaderInfo.DurationMin = (word) (Duration / 60);
        RecInf->RecHeaderInfo.DurationSec = (byte) (Duration % 60);
        printf("    EvtStart  = %s (UTC)\n", TimeStrTF(RecInf->EventInfo.StartTime, 0));
        printf("    EvtDuratn = %02d:%02d\n", RecInf->EventInfo.DurationHour, RecInf->EventInfo.DurationMin);
      }
      free(Buffer);
    }
    else
      printf("  LoadDVBViewer: Could not allocate buffer!\n");
    fclose(fLog);
  }
  TRACEEXIT;
  return ret;
}


//------ RebuildINF
void InitInfStruct(TYPE_RecHeader_TMSS *RecInf)
{
  TRACEENTER;
  memset(RecInf, 0, sizeof(TYPE_RecHeader_TMSS));
  RecInf->RecHeaderInfo.Magic[0]      = 'T';
  RecInf->RecHeaderInfo.Magic[1]      = 'F';
  RecInf->RecHeaderInfo.Magic[2]      = 'r';
  RecInf->RecHeaderInfo.Magic[3]      = 'c';
  RecInf->RecHeaderInfo.Version       = 0x8000;
  RecInf->RecHeaderInfo.CryptFlag     = 0;
  RecInf->RecHeaderInfo.FlagUnknown   = 1;
  RecInf->ServiceInfo.ServiceType     = 1;  // SVC_TYPE_Radio
  RecInf->ServiceInfo.SatIndex        = 1;
  RecInf->ServiceInfo.FlagTuner       = 3;
  RecInf->ServiceInfo.VideoStreamType = 0xff;
  RecInf->ServiceInfo.VideoPID        = 0xffff;
  RecInf->ServiceInfo.AudioStreamType = 0xff;
  RecInf->ServiceInfo.AudioPID        = 0xfff;
  RecInf->ServiceInfo.AudioTypeFlag   = 3;  // unknown
//  strcpy(RecInf->ServiceInfo.ServiceName, "RecStrip");
  TRACEEXIT;
}

// see https://www.etsi.org/deliver/etsi_en/300400_300499/300468/01.11.01_60/en_300468v011101p.pdf
static bool AnalysePMT(byte *PSBuffer, int BufSize, TYPE_RecHeader_TMSS *RecInf)
{
  tTSPMT               *PMT = (tTSPMT*)PSBuffer;
  char                  LangCode[4];
  int                   ElemPt;
  int                   SectionLength, ProgramInfoLength, ElemLength;
  word                  PID;
  byte                  StreamTag = (byte) -1;
  int                   k;
  bool                  VideoFound = FALSE;

  if(PMT->TableID != TABLE_PMT) return FALSE;

  TRACEENTER;

  //The following variables have a constant distance from the packet header
  SectionLength = PMT->SectionLen1 * 256 | PMT->SectionLen2;
  SectionLength = min(SectionLength + 3, BufSize);    // SectionLength z�hlt erst ab Byte 3

  RecInf->ServiceInfo.ServiceID = PMT->ProgramNr1 * 256 | PMT->ProgramNr2;
  RecInf->ServiceInfo.PCRPID = PMT->PCRPID1 * 256 | PMT->PCRPID2;
  printf(", SID=%hu, PCRPID=%hd", RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID);

  ProgramInfoLength = PMT->ProgInfoLen1 * 256 | PMT->ProgInfoLen2;

  //Program info (-> �berspringen)
  ElemPt = sizeof(tTSPMT);

/*  while(ProgramInfoLength > 0)
  {
    tTSDesc* Desc = (tTSDesc*) &PSBuffer[ElemPt];
    ElemPt            += (Desc->DescrLength + 2);
    ProgramInfoLength -= (Desc->DescrLength + 2);
    SectionLength     -= (Desc->DescrLength + 2);
  } */

  // Einfacher:
  ElemPt += ProgramInfoLength;

  //Loop through all elementary stream descriptors and search for the Audio and Video descriptor
  while (ElemPt + (int)sizeof(tElemStream) + 4 <= SectionLength)     // 4 Bytes CRC am Ende
  {
    tElemStream* Elem = (tElemStream*) &PSBuffer[ElemPt];
    PID = Elem->ESPID1 * 256 | Elem->ESPID2;
    ElemLength = Elem->ESInfoLen1 * 256 | Elem->ESInfoLen2;
    memset(LangCode, 0, sizeof(LangCode));

    for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != PID); k++);
    if (k < MAXCONTINUITYPIDS)
//    if (ElemLength > 0)  // Ist das n�tig??
    {

#if defined(_WIN32) && defined(_DEBUG)
printf("\n\n  ELEMENT: Type=%hhu, PID=%hd, Length=%d", Elem->stream_type, PID, ElemLength);
#endif

      switch (Elem->stream_type)
      {
        case STREAM_VIDEO_MPEG4_PART2:
        case STREAM_VIDEO_MPEG4_H263:
        case STREAM_VIDEO_MPEG4_H264:
        case STREAM_VIDEO_VC1:
        case STREAM_VIDEO_VC1SM:
          if(RecInf->ServiceInfo.VideoStreamType == 0xff)  // fall-through
            isHDVideo = TRUE;  // fortsetzen...
          // (fall-through!)

        case STREAM_VIDEO_MPEG1:
        case STREAM_VIDEO_MPEG2:
        {
          int DescrLength = ElemLength;
          VideoFound = TRUE;
          RecInf->ServiceInfo.ServiceType = 0;  // SVC_TYPE_Tv
          if(RecInf->ServiceInfo.VideoStreamType == 0xff)
          {
            VideoPID = PID;
            RecInf->ServiceInfo.VideoPID = PID;
            RecInf->ServiceInfo.VideoStreamType = Elem->stream_type;
            ContinuityPIDs[0] = PID;
            printf(", Stream=0x%hhx, VPID=%hd, HD=%d", RecInf->ServiceInfo.VideoStreamType, VideoPID, isHDVideo);

            while (DescrLength > 0)
            {
              dword DescrPt = ElemPt + sizeof(tElemStream);
              while (DescrLength > 0)
              {
                tTSDesc2* Desc = (tTSDesc2*) &PSBuffer[ElemPt + sizeof(tElemStream)];
#if defined(_WIN32) && defined(_DEBUG)
printf("\n    DESC: Type=TSDesc, Size=%d, Len=%hhu, Tag=0x%02hhx (%c), Data=0x%02hhx", sizeof(tTSDesc), Desc->DescrLength, Desc->DescrTag, (Desc->DescrTag > 0x20 ? Desc->DescrTag : ' '), Desc->Data[0]);
#endif
//                if (Desc->DescrTag == DESC_StreamIdentifier)
//                  VideoStreamTag = Desc->Data[0];
                DescrPt     += (Desc->DescrLength + sizeof(tTSDesc));
                DescrLength -= (Desc->DescrLength + sizeof(tTSDesc));
              }
            }
          }
          break;
        }

        case 0x06:  // Teletext!
        {
          dword DescrPt = ElemPt + sizeof(tElemStream);
          int DescrLength = ElemLength;
          int streamType = STREAMTYPE_UNKNOWN;
          byte DescType = 0;

          while (DescrLength > 0)
          {
            tTSSubtDesc* Desc = (tTSSubtDesc*) &PSBuffer[DescrPt];
            tTSDesc2* Desc2 = (tTSDesc2*) &PSBuffer[DescrPt];

            if (Desc2->DescrTag == DESC_StreamIdentifier)
              AudioPIDs[k].streamId = StreamTag;

            if ((Desc->DescrTag == DESC_Teletext) || (Desc->DescrTag == DESC_Subtitle))
            {
              if (Desc->DescrTag == DESC_Teletext)
              {
                // Teletext
                tTSTtxDesc* Desc3 = (tTSTtxDesc*) Desc;
                int i;
#if defined(_WIN32) && defined(_DEBUG)
printf("\n    DESC: Type=Teletext, Size=%d, Len=%hhu, Data=0x%02hhx, Lang=%.3s", sizeof(tTSDesc), Desc->DescrLength, (byte)Desc->LanguageCode[0], (Desc->DescrTag=='V' || Desc->DescrTag=='Y' || Desc->DescrTag==0x0a ? Desc->LanguageCode : ""));
#endif
                if ((TeletextPID == (word)-1) /* || (strncmp(Desc->LanguageCode, "deu", 3) == 0 || strncmp(Desc->LanguageCode, "ger", 3) == 0) */)
                {
                  TeletextPID = PID;
                  printf("\n  TS: TeletextPID=%hd [%.3s]", TeletextPID, Desc->LanguageCode);
                }
                streamType = STREAMTYPE_TELETEXT;
                for (i = 0; i < Desc->DescrLength / 5; i++)
                {
                  if (Desc3->ttx[i].teletext_type == 1)
                    printf(", InitPage=%hx", Desc3->ttx[i].magazine_nr << 8 | Desc3->ttx[i].page_nr);
                  else if (Desc3->ttx[i].teletext_type == 2)
                  {
                    printf(", SubtPage=%hu", Desc3->ttx[i].magazine_nr << 8 | Desc3->ttx[i].page_nr);
                    DescType = Desc3->ttx[i].page_nr + 1;  // plus 1, because 0 = unset
//                    if(!TeletextPage) TeletextPage = Desc3->ttx[i].page_nr;
                  }
                  else
                    printf(", OtherPage=%hu", Desc3->ttx[i].page_nr);
                }
                printf("\n");
              }
              else if (Desc->DescrTag == DESC_Subtitle)
              {
                // DVB-Subtitles
#if defined(_WIN32) && defined(_DEBUG)
printf("\n    DESC: Type=Subtitle, Size=%d, Len=%hhu, Data=0x%02hhx, Lang=%.3s", sizeof(tTSDesc), Desc->DescrLength, (byte)Desc->LanguageCode[0], (Desc->DescrTag=='V' || Desc->DescrTag=='Y' || Desc->DescrTag==0x0a ? Desc->LanguageCode : ""));
#endif
                if ((SubtitlesPID == (word)-1) /* || (strncmp(Desc->LanguageCode, "deu", 3) == 0 || strncmp(Desc->LanguageCode, "ger", 3) == 0) */)
                {
                  SubtitlesPID = PID;
                  printf("\n  TS: SubtitlesPID=%hd [%.3s]", SubtitlesPID, Desc->LanguageCode);
                }
                streamType = STREAMTYPE_SUBTITLE;
                DescType = Desc->subtitling_type + 1;  // plus 1, because 0 = unset
              }

              if ((k < MAXCONTINUITYPIDS) && !AudioPIDs[k].scanned)
              {
                AudioPIDs[k].pid = PID;
                AudioPIDs[k].sorted = TRUE;
                AudioPIDs[k].streamType = streamType;
                if (Desc->DescrLength >= 3)
                {
                  strncpy(AudioPIDs[k].desc, (char*)Desc->LanguageCode, 3);
                  AudioPIDs[k].desc_flag = DescType;  // plus 1, because 0 = unset
                }
              }
              PID = 0;
              break;
            }

            DescrPt     += (Desc->DescrLength + sizeof(tTSDesc));
            DescrLength -= (Desc->DescrLength + sizeof(tTSDesc));
          }
          if (PID == 0) break;
          // sonst fortsetzen mit Audio (fall-through)
#if defined(_WIN32) && defined(_DEBUG)
printf("\n    -> No Teletext/Subtitle track detected! Analysing audio instead...");  // fall-through
#endif
        }

        //case STREAM_AUDIO_MP3:  //Ignored because it crashes with STREAM_VIDEO_MPEG1
        case STREAM_AUDIO_MPEG1:
        case STREAM_AUDIO_MPEG2:
        case STREAM_AUDIO_MPEG4_AAC:
        case STREAM_AUDIO_MPEG4_AAC_PLUS:
        //case STREAM_AUDIO_MPEG4_AC3_PLUS:  // Ignored because 0x06 = Private Stream (falls through)
        case STREAM_AUDIO_MPEG4_AC3:
        case STREAM_AUDIO_MPEG4_DTS:
        {
          dword DescrPt = ElemPt + sizeof(tElemStream);
          int DescrLength = ElemLength;
          byte DescType = 0;
          bool isAC3 = FALSE;

          while (DescrLength > 0)
          {
            tTSDesc2* Desc = (tTSDesc2*) &PSBuffer[DescrPt];

#if defined(_WIN32) && defined(_DEBUG)
printf("\n    DESC: Type=TSDesc, Size=%d, Len=%hhu, Tag=0x%02hhx (%c), Data=0x%02hhx, Lang=%.3s", sizeof(tTSDesc), Desc->DescrLength, Desc->DescrTag, (Desc->DescrTag > 0x20 ? Desc->DescrTag : ' '), Desc->Data[0], (Desc->DescrTag==0xa ? (char*)Desc->Data : ""));
#endif
            if (Desc->DescrTag == DESC_StreamIdentifier)
              StreamTag = Desc->Data[0];
            else if (Desc->DescrTag == DESC_AC3)
              isAC3 = TRUE;
            else if ((Desc->DescrTag == DESC_AudioLang) && (Desc->DescrLength >= 3))
            {
              strncpy(LangCode, (char*)Desc->Data, 3);
              DescType = ((tTSAudioDesc*)Desc)->AudioFlag;  // plus 1, because 0 = unset
//              break;
            }
            DescrPt     += (Desc->DescrLength + sizeof(tTSDesc));
            DescrLength -= (Desc->DescrLength + sizeof(tTSDesc));
          }

          if ((k < MAXCONTINUITYPIDS) && !AudioPIDs[k].scanned && !AudioPIDs[k].streamType)
          {
            if (Elem->stream_type == 6 && !isAC3) break;
            AudioPIDs[k].pid = PID;
            AudioPIDs[k].type = Elem->stream_type;  // (Elem->stream_type == STREAM_AUDIO_MPEG2) ? 0 : (Elem->stream_type == STREAM_AUDIO_MPEG1 ? 1 : Elem->stream_type);
            AudioPIDs[k].sorted = TRUE;
            AudioPIDs[k].streamId = StreamTag;
            AudioPIDs[k].streamType = ((Elem->stream_type != 0x06) || isAC3) ? STREAMTYPE_AUDIO : STREAMTYPE_UNKNOWN;
            strncpy(AudioPIDs[k].desc, LangCode, 4);
            AudioPIDs[k].desc_flag = DescType + 1;  // plus 1, because 0 = unset
          }
          AddContinuityPids(PID, FALSE);
          break;
        }
      }
    }
    StreamTag = (byte) -1;
    ElemPt += (ElemLength + sizeof(tElemStream));
  }

  if(TeletextPID != 0xffff) AddContinuityPids(TeletextPID, FALSE);
  AddContinuityPids(0x12, FALSE);
  printf("\n");

//  isHDVideo = HDFound;
  TRACEEXIT;
  return VideoFound;
}

static bool AnalyseSDT(byte *PSBuffer, int BufSize, word ServiceID, TYPE_RecHeader_TMSS *RecInf)
{
  tTSSDT               *SDT = (tTSSDT*)PSBuffer;
  tTSService           *pService = NULL;
  tTSServiceDesc       *pServiceDesc = NULL;
  int                   SectionLength, p, SvcLen;
  char                 *Svc, tmp;

  TRACEENTER;

  if (SDT->TableID == TABLE_SDT)
  {
    SectionLength = SDT->SectionLen1 * 256 | SDT->SectionLen2;
    SectionLength = min(SectionLength + 3, BufSize);    // SectionLength z�hlt erst ab Byte 3

    p = sizeof(tTSSDT);
    while (p + (int)sizeof(tTSService) <= SectionLength)
    {
      pService = (tTSService*) &PSBuffer[p];
      p += sizeof(tTSService);
      pServiceDesc = (tTSServiceDesc*) &PSBuffer[p];
      
      if (pServiceDesc->DescrTag==DESC_Service)
      {
        if ((pService->ServiceID1 * 256 | pService->ServiceID2) == ServiceID)
        {
//          memset(RecInf->ServiceInfo.ServiceName, 0, sizeof(RecInf->ServiceInfo.ServiceName));
          Svc = &pServiceDesc->ProviderName + pServiceDesc->ProviderNameLen + 1;
          SvcLen = min((&pServiceDesc->ProviderName)[pServiceDesc->ProviderNameLen], BufSize - p - 1);
          tmp = (&pServiceDesc->ProviderName)[SvcLen];
          Svc[SvcLen] = '\0';
          StrToUTF8(RecInf->ServiceInfo.ServiceName, Svc, sizeof(RecInf->ServiceInfo.ServiceName), 0);
          Svc[SvcLen] = tmp;
printf("  TS: SvcName   = %s\n", RecInf->ServiceInfo.ServiceName);
          TRACEEXIT;
          return TRUE;
        }
      }
      p += (pService->DescriptorsLen1 * 256 | pService->DescriptorsLen2);
    }
  }
  TRACEEXIT;
  return FALSE;
}

bool AnalyseEIT(byte *Buffer, int BufSize, word ServiceID, word *OutTransportID, TYPE_Event_Info *OutEventInfo, TYPE_ExtEvent_Info *OutExtEventInfo, bool OverwriteInf)
{
  tTSEIT               *EIT = (tTSEIT*)Buffer;
  tEITEvent            *Event = NULL;
  tTSDesc              *Desc = NULL;
  tShortEvtDesc        *ShortDesc = NULL;
  tExtEvtDesc          *ExtDesc = NULL;
//  char                 *ExtEPGText = NULL;
  int                   ExtEPGTextLen = 0, SectionLength, DescriptorLoopLen, p;
  word                  EventID;

  TRACEENTER;

  if ((EIT->TableID == TABLE_EIT) && ((EIT->ServiceID1 * 256 | EIT->ServiceID2) == ServiceID || !ServiceID))
  {
    if (OverwriteInf)
    {
      if(ExtEPGText) free(ExtEPGText);
      if (!(ExtEPGText = (char*) malloc(EPGBUFFERSIZE)))
      {
        printf("Could not allocate memory for ExtEPGText.\n");
        TRACEEXIT;
        return FALSE;
      }

      memset(OutEventInfo->EventNameDescription, 0, sizeof(OutEventInfo->EventNameDescription));
      memset(OutExtEventInfo->Text, 0, sizeof(OutExtEventInfo->Text));
//      memset(ExtEPGText, 0, EPGBUFFERSIZE);
      OutExtEventInfo->TextLength = 0;
      ExtEPGText[0] = '\0';

      if(OutTransportID) *OutTransportID = (EIT->TS_ID1 * 256 | EIT->TS_ID2);
    }

    SectionLength = EIT->SectionLen1 * 256 | EIT->SectionLen2;
    SectionLength = min(SectionLength + 3, BufSize);    // SectionLength z�hlt erst ab Byte 3
    p = sizeof(tTSEIT);
    while (p + (int)sizeof(tTSEIT) + 4 <= SectionLength)
    {
      Event = (tEITEvent*) &Buffer[p];
      EventID = Event->EventID1 * 256 | Event->EventID2;
      DescriptorLoopLen = Event->DescriptorLoopLen1 * 256 | Event->DescriptorLoopLen2;

      p += sizeof(tEITEvent);
      if(Event->RunningStatus == 4) // *CW-debug* richtig: 4
      {
        //Found the event we were looking for
        while ((DescriptorLoopLen > 0) && (p + (int)sizeof(tTSDesc) + 4 <= SectionLength))
        {
          Desc = (tTSDesc*) &Buffer[p];
          
          if(Desc->DescrTag == DESC_EITShortEvent)
          {
            // Short Event Descriptor
            int         NameLen, TextLen;
            char       *Name, *Text, tmp;
            ShortDesc = (tShortEvtDesc*) Desc;

            OutEventInfo->ServiceID = ServiceID;
            OutEventInfo->EventID = EventID;
            OutEventInfo->RunningStatus = Event->RunningStatus;
            OutEventInfo->StartTime = (Event->StartTime[0] << 24) | (Event->StartTime[1] << 16) | (BCD2BIN(Event->StartTime[2]) << 8) | BCD2BIN(Event->StartTime[3]);
            OutEventInfo->DurationHour = BCD2BIN(Event->DurationSec[0]);
            OutEventInfo->DurationMin = BCD2BIN(Event->DurationSec[1]);
            OutEventInfo->EndTime = AddTimeSec(OutEventInfo->StartTime, 0, NULL, OutEventInfo->DurationHour * 3600 + OutEventInfo->DurationMin * 60);
//            StartTimeUnix = 86400*((OutEventInfo->StartTime>>16) - 40587) + 3600*BCD2BIN(Event->StartTime[2]) + 60*BCD2BIN(Event->StartTime[3]);
printf("  TS: EvtStart  = %s (UTC)\n", TimeStrTF(OutEventInfo->StartTime, 0));
printf("  TS: EvtDuratn = %02d:%02d\n", OutEventInfo->DurationHour, OutEventInfo->DurationMin);

            Name = (char*)&Buffer[p + sizeof(tShortEvtDesc)];
            NameLen = min(ShortDesc->EvtNameLen, BufSize - p - 1);
            Text = (char*) &Buffer[p + sizeof(tShortEvtDesc) + NameLen + 1];
            TextLen = min((byte) Buffer[p + sizeof(tShortEvtDesc) + NameLen], BufSize - p - 1);

            tmp = Name[NameLen];
            Name[NameLen] = '\0';
            StrToUTF8(OutEventInfo->EventNameDescription, Name, sizeof(OutEventInfo->EventNameDescription), 0);
            Name[NameLen] = tmp;

            NameLen = (int)strlen(OutEventInfo->EventNameDescription);
            OutEventInfo->EventNameLength = NameLen;
printf("  TS: EventName = %s\n", OutEventInfo->EventNameDescription);

            tmp = Text[TextLen];
            Text[TextLen] = '\0';
            StrToUTF8(&OutEventInfo->EventNameDescription[NameLen], Text, sizeof(OutEventInfo->EventNameDescription) - NameLen, 0);
            Text[TextLen] = tmp;
printf("  TS: EventDesc = %s\n", &OutEventInfo->EventNameDescription[NameLen]);
          }

          else if(Desc->DescrTag == DESC_EITExtEvent && OverwriteInf)
          {
            // Extended Event Descriptor
            byte        NameLen, TextLen, tmp, *s;
            char       *Name, *Text;
            int         i;

            ExtDesc = (tExtEvtDesc*) Desc;
            OutExtEventInfo->ServiceID = ServiceID;
//            RecInf->ExtEventInfo.EventID = EventID;

            OutExtEventInfo->NrItemizedPairs = ExtDesc->ItemsLen;
if (ExtDesc->ItemsLen > 0)
  printf("  TS: Number of detected EPG Itemized Pairs: %d\n", OutExtEventInfo->NrItemizedPairs);

            // EPG Text kopieren
            s = (byte*) &ExtDesc->ItemDescLen;
            for (i = 0; i < ExtDesc->ItemsLen; i++)
            {
              NameLen = *(s++);  s += NameLen;
              TextLen = *(s++);  s += TextLen;
            }
            TextLen = *(s++);
            tmp = s[TextLen];
            s[TextLen] = '\0';
            StrToUTF8(&ExtEPGText[ExtEPGTextLen], (char*) s, EPGBUFFERSIZE - ExtEPGTextLen, 0);
            s[TextLen] = tmp;
            ExtEPGTextLen += (int)strlen(&ExtEPGText[ExtEPGTextLen]);

            // Itemized Strings kopieren
            s = (byte*) &ExtDesc->ItemDescLen;
            for (i = 0; i < ExtDesc->ItemsLen; i++)
            {
              NameLen = *(s++);
              Name = (char*) s;
              s += NameLen;
              TextLen = *(s++);
              Text = (char*) s;
              s += TextLen;

              snprintf (&ExtEPGText[ExtEPGTextLen], EPGBUFFERSIZE - ExtEPGTextLen, "\xC2\x8A");  ExtEPGTextLen += 2;
              tmp = Name[NameLen];
              Name[NameLen] = '\0';
              StrToUTF8(&ExtEPGText[ExtEPGTextLen], Name, EPGBUFFERSIZE - ExtEPGTextLen, 0);  ExtEPGTextLen += (int)strlen(&ExtEPGText[ExtEPGTextLen]);
              Name[NameLen] = tmp;

              snprintf (&ExtEPGText[ExtEPGTextLen], EPGBUFFERSIZE - ExtEPGTextLen, ": ");        ExtEPGTextLen += 2;
              tmp = Text[TextLen];
              Text[TextLen] = '\0';
              StrToUTF8(&ExtEPGText[ExtEPGTextLen], Text, EPGBUFFERSIZE - ExtEPGTextLen, 0);  ExtEPGTextLen += (int)strlen(&ExtEPGText[ExtEPGTextLen]);
              Text[TextLen] = tmp;
            }
          }

          DescriptorLoopLen -= (Desc->DescrLength + sizeof(tTSDesc));
          p += (Desc->DescrLength + sizeof(tTSDesc));
        }
        if (OverwriteInf)
        {
          strncpy(OutExtEventInfo->Text, ExtEPGText, min(ExtEPGTextLen, (int)sizeof(OutExtEventInfo->Text)));
          OutExtEventInfo->Text[sizeof(OutExtEventInfo->Text) - 1] = '\0';
          OutExtEventInfo->TextLength = (word) min(ExtEPGTextLen, (int)sizeof(OutExtEventInfo->Text) - 1);
printf("  TS: EPGExtEvt = %s\n", ExtEPGText);
        }

        if (DoInfoOnly)
        {
          char *c, *d;

          // Ersetze eventuelles '\n', '\t' im Output
          for (c = ExtEPGText; *c != '\0'; c++)
          {
            if (*c == '\n')
            {
              *c = 0x8A;
              if (ExtEPGTextLen < EPGBUFFERSIZE)
              {
                for(d = &ExtEPGText[ExtEPGTextLen]; d >= c; d--)  *(d+1) = *d;
                *c = 0xC2;
                ExtEPGTextLen++;
              }
            }
            if (*c == '\t') *c = ' ';
          }
        }
        else
        {
          free(ExtEPGText); ExtEPGText = NULL;
        }

//printf("CRC = %x\n", crc32m_tab(Buffer, SectionLength-4));

        TRACEEXIT;
        return TRUE;
      }
      else
      {
        //Not this one
        p += DescriptorLoopLen;
      }
    }
  }
  TRACEEXIT;
  return FALSE;
}

static bool AnalyseTtx(byte *PSBuffer, int BufSize, bool AcceptTimeString, tPVRTime ReferenceDate, tPVRTime *const TtxTime, byte *const TtxTimeSec, int *const TtxTimeZone, char *const ServiceName, int SvcNameLen)
{
  char                  programme[50];
  dword                 History = 0xffffffff;
  int                   PESLength = BufSize, p = 0;
  TRACEENTER;

  while (p + 10 < BufSize)
  {
    if ((History & 0xFFFFFF00) == 0x00000100)
    {
      tPESHeader *PESHeader = (tPESHeader*) &PSBuffer[p-4];
      PESLength = PESHeader->PacketLength1 * 256 + PESHeader->PacketLength2;
      PESLength = min(PESLength + 7, BufSize - (p-4));
      p = p-4 + 7 + (PESHeader->OptionalHeaderMarker ? PESHeader->PESHeaderLen + 2 : 0);  // Additional header
    }

    if (PSBuffer[p] == 0x10)
    {
      p++;
      while (p + 46 <= PESLength)
      {
        if ((PSBuffer[p] & 0xfe) == 0x02 && PSBuffer[p+1] == 44)
        {
          byte *data_block = &PSBuffer[p+6];

          byte row = ((hamming_decode_rev(PSBuffer[p+5]) & 0x0f) << 4) | (hamming_decode_rev(PSBuffer[p+4]) & 0x0f);
          byte magazin = row & 7;
          if (magazin == 0) magazin = 8;
          row = row >> 3;

          if ((magazin == 8 && row == 30 && data_block[1] == 0xA8) || (AcceptTimeString && magazin < 8 && row == 0))
          {
            int i, len = 0;
            byte packet_format = hamming_decode_rev(data_block[0]) & 0x0e;

            if (packet_format <= 2)
            {
              // get initial page
/*              byte initialPageUnits = hamming_decode_rev(data_block[1]);
              byte initialPageTens  = hamming_decode_rev(data_block[2]);
              //byte initialPageSub1  = hammingDecode_rev(data_block[3]);
              byte initialPageSub2  = hamming_decode_rev(data_block[4]);
              //byte initialPageSub3  = hamming_decode_rev(data_block[5]);
              byte initialPageSub4  = hamming_decode_rev(data_block[6]);
              dword InitialPage = initialPageUnits + 10 * initialPageTens + 100 * (((initialPageSub2 >> 3) & 1) + ((initialPageSub4 >> 1) & 6));
*/
              // Programme Identification
              programme[0] = '\0';
              for (i = 20; i < 40 && len+3 < (int)sizeof(programme); i++)
              {
                char u[4] = { 0, 0, 0, 0 };
                word c = telx_to_ucs2(byte_reverse(data_block[i]));
                // strip any control codes from PID, eg. TVP station
                if (c < 0x20) continue;
                len += ucs2_to_utf8(u, c);
                strcat(programme, u);
              }
              rtrim(programme);

              if (magazin == 8 && packet_format <= 2)
              {
                if(ServiceName && !KeepHumaxSvcName)
                {
                  StrToUTF8(ServiceName, programme, SvcNameLen, 0);
//                  ServiceName[SvcNameLen-1] = '\0';
                }
printf("  TS: Teletext Programme Identification Data: '%s'\n", programme);
              }
              else if (AcceptTimeString && TtxTime && !*TtxTime && ((programme[12]==':' && programme[15]==':') || (programme[13]==':' && programme[16]==':') || (programme[14]==':' && programme[17]==':')))
              {
                dword year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
                struct tm timeinfo = {0};
                time_t UnixTime = TF2UnixTime(ReferenceDate, 0, TRUE);
                #ifdef _WIN32
                  localtime_s(&timeinfo, &UnixTime);
                #else
                  localtime_r(&UnixTime, &timeinfo);
                #endif
printf("  TS: Teletext Possible Time String: '%s'\n", programme);

                if (sscanf(programme, "xt%2u.%2u.%2u%2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "xt%*2[A-Za-z].%2u.%2u%2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "t %2u.%2u.%2u%2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "t%*2[A-Za-z] %2u.%2u. %2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "ns%2u.%2u.%2u%2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "Brbg%2u.%2u.%2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "ITAL%2u.%2u. %2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, " ,%2u.%2u.%2u%2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "%*2[A-Za-z].%2u.%2u.%2u %2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "%*2[A-Za-z] %2u.%2u.%2u %2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, " %2u.%2u.%2u%2u:%2u:%2u", &day, &month, &year, &hour, &minute, &second) == 6)
                  *TtxTime = Unix2TFTime(MakeUnixTime((word)(year + 2000), (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "  %*2[A-Za-z] %2u.%2u.%2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "%*2[A-Za-z] %2u.%2u. %2u:%2u:%2u", &day, &month, &hour, &minute, &second) == 5)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, (byte)month, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, " %*2[A-Za-z] %2u %*3[A-Za-z]%2u:%2u:%2u", &day, &hour, &minute, &second) == 4)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);
                else if (sscanf(programme, "%*3[A-Za-z].%2u.%*3[A-Za-z]%2u:%2u:%2u", &day, &hour, &minute, &second) == 4)
                  *TtxTime = Unix2TFTime(MakeUnixTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, (byte)day, (byte)hour, (byte)minute, (byte)second, TtxTimeZone), TtxTimeSec, TRUE);

                if (TtxTimeZone && *TtxTimeZone)
                  *TtxTimeZone = -*TtxTimeZone;
                if (TtxTime && *TtxTime)
                {
printf("  TS: Teletext date*: %s (GMT%+d)\n", TimeStrTF(*TtxTime, *TtxTimeSec), -*TtxTimeZone/3600);
                  TRACEEXIT;
                  return TRUE;
                }
              }
//              else if (AcceptTimeString && TtxTime)
//printf("%s\n", programme);
            }

            if (magazin == 8 && packet_format < 2)
            {
              // offset in half hours
              int time_offset;
              byte timeOffsetCode   = byte_reverse(data_block[9]);
              byte timeOffsetH2     = ((timeOffsetCode >> 1) & 0x1F);

              // get current time
              byte mjd1             = byte_reverse(data_block[10]);
              byte mjd2             = byte_reverse(data_block[11]);
              byte mjd3             = byte_reverse(data_block[12]);
              dword mjd = ((mjd1 & 0x0F)-1)*10000 + ((mjd2 >> 4)-1)*1000 + ((mjd2 & 0x0F)-1)*100 + ((mjd3 >> 4)-1)*10 + ((mjd3 & 0x0F)-1);

              byte utc1             = byte_reverse(data_block[13]);
              byte utc2             = byte_reverse(data_block[14]);
              byte utc3             = byte_reverse(data_block[15]);

              int localH = 10 * ((utc1 >> 4) - 1) + ((utc1 & 0x0f) - 1);
              int localM = 10 * ((utc2 >> 4) - 1) + ((utc2 & 0x0f) - 1);
              int localS = 10 * ((utc3 >> 4) - 1) + ((utc3 & 0x0f) - 1);

              if ((timeOffsetCode & 0x40) == 0)
              {
                // positive offset polarity
                localM += (timeOffsetH2 * 30);
                localH += (localM / 60);
                mjd    += (localH / 24);
                localM  = localM % 60;
                localH  = localH % 24;
                if(TtxTimeZone) *TtxTimeZone = -1 * (int)timeOffsetH2 * 1800;
              }
              else
              {
                // negative offset polarity
                localM -= (timeOffsetH2 * 30);
                while (localM < 0)  { localM += 60; localH--; }
                while (localH < 0)  { localH += 24; mjd--; }
                if(TtxTimeZone) *TtxTimeZone = (int)timeOffsetH2 * 1800;
              }

              if (TtxTime)
              {
                *TtxTime = DATE(mjd, localH, localM);
                if(TtxTimeSec) *TtxTimeSec = localS;
                EPG2TFTime(*TtxTime, &time_offset);
printf("  TS: Teletext date: %s (GMT%+d)\n", TimeStrTF(*TtxTime, *TtxTimeSec), -*TtxTimeZone/3600);
              }

if (TtxTimeZone && time_offset != -*TtxTimeZone && (MedionMode || HumaxSource || EycosSource || TechniSource || DVBViewerSrc))
{
//  *TtxTime = AddTimeSec(*TtxTime, *TtxTimeSec, TtxTimeSec, -*TtxTimeZone - time_offset);
//  printf("  TS: Teletext date: %s (GMT%+d, corrected!)\n", TimeStrTF(*TtxTime, *TtxTimeSec), time_offset/3600);
  printf("  TS: Teletext time zone corrected: GMT%+d -> GMT%+d\n", -*TtxTimeZone/3600, time_offset/3600);
  *TtxTimeZone = -time_offset;
}

//printf("  TS: Teletext date: mjd=%u, %02hhu:%02hhu:%02hhu\n", mjd, localH, localM, localS);
              TRACEEXIT;
              return 2;
            }
          }
        }
        p += max(PSBuffer[p+1] + 2, 1);
      }
    }
    History = (History << 8) | PSBuffer[p++];
  }
  TRACEEXIT;
  return FALSE;
}

// Audio Substream Headers: https://web.archive.org/web/20220721025819/http://stnsoft.com/DVD/ass-hdr.html
// AC3-Header: https://web.archive.org/web/20220120112704/http://stnsoft.com/DVD/ac3hdr.html
static tStreamType AnalyseAudio(byte *PSBuffer, int BufSize, word pid, tAudioTrack *const AudioTrack)
{
  int                   p = 0;
  dword                 History = 0xffffffff;
  byte                  HeaderFound = 0;
  TRACEENTER;

  while (p + 10 < BufSize)
  {
    // PES-Header (Audio)
    if (((History & 0xFFFFFFE0) == 0x000001C0) || (History == 0x000001BD))
    {
      tPESHeader *PESHeader = (tPESHeader*) &PSBuffer[p-4];
      HeaderFound = History & 0xFF;
      p = p-4 + 7 + (PESHeader->OptionalHeaderMarker ? PESHeader->PESHeaderLen + 2 : 0);
    }

    // Audio Sequence Header
    if (HeaderFound == 0xBD)
    {
      if ((History & 0xFFFF0000) == 0x0B770000)
      {
        const float AC3Sampling[] = {48, 44.1f, 32, 0};
        const short AC3Bitrates[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640};

        if (AudioTrack)
        {
          AudioTrack->streamType = STREAMTYPE_AUDIO;
          if(!AudioTrack->type) AudioTrack->type = STREAM_AUDIO_MPEG4_AC3_PLUS;
        }
        printf("  TS: PID=%d, AudioType: AC3 @ %.1f kHz, %hd kbit/s", pid, AC3Sampling[(PSBuffer[p] & 0xc0)], ((PSBuffer[p] & 0x3f) < 38) ? AC3Bitrates[(PSBuffer[p] & 0x3f) / 2] : -1);
        if(*AudioTrack->desc) printf(" [%s]\n", AudioTrack->desc); else printf("\n");
        TRACEEXIT;
        return STREAMTYPE_AUDIO;
      }
      else if (History == 0x7FFE8001)
      {
        const float DTSSampling[] = {-1, 8, 16, 32, -1, -1, 11.025f, 22.050f, 44.1f, -1, -1, 12, 24, 48, -1, -1};
        // 0F - targeted rate is 768 Kbps, actual rate is 754.5 Kbps
        // 18 - targeted rate is 1536 Kbps, actual rate is 1509.75 Kbps

        tDTSHeader *DTSHeader = (tDTSHeader*) &PSBuffer[p-4];
        byte rate = DTSHeader->rate1 << 3 | DTSHeader->rate2;
        if (AudioTrack)
        {
          AudioTrack->streamType = STREAMTYPE_AUDIO;
          if(!AudioTrack->type) AudioTrack->type = STREAM_AUDIO_MPEG4_DTS;
        }
        printf("  TS: PID=%d, AudioType: DTS @ %.1f kHz, %hd kbit/s", pid, DTSSampling[DTSHeader->sfreq], ((rate == 0x0F) ? 768 : ((rate == 0x18) ? 1536 : 0)));
        if(*AudioTrack->desc) printf(" [%s]\n", AudioTrack->desc); else printf("\n");
        TRACEEXIT;
        return STREAMTYPE_AUDIO;
      }
      else if (History == 0x000001BD) // && (PSBuffer[p] == 0x10) && ((PSBuffer[p+1] & 0xfe) == 0x02))
      {
        tTtxHeader *ttxHeader = (tTtxHeader*) &PSBuffer[p];
        if ((ttxHeader->data_identifier == 0x10)  // = EBU data EN 300 472 (teletext)
         /* && (ttxHeader->data_unit_id == 0x02 || ttxHeader->data_unit_id == 0x03) */)  // = EBU Teletext (non-subtitle) data / DATA_UNIT_EBU_TELETEXT_SUBTITLE / DATA_UNIT_EBU_TELETEXT_NONSUBTITLE
        {
          printf("  TS: PID=%d, Teletext stream", pid);
          if (AudioTrack)
          {
            AudioTrack->streamType = STREAMTYPE_TELETEXT;
            if(*AudioTrack->desc) printf(" [%s]\n", AudioTrack->desc); else printf("\n");
          }
          if ((TeletextPID == (word)-1) || (AudioTrack && *AudioTrack->desc && (strncmp(AudioTrack->desc, "deu", 3) == 0 || strncmp(AudioTrack->desc, "ger", 3) == 0)))
            TeletextPID = pid;
          TRACEEXIT;
          return STREAMTYPE_TELETEXT;
        }
        else if (ttxHeader->data_identifier == 0x20)  // = EBU Subtitles (?)
        {
          printf("  TS: PID=%d, DVB subtitles stream", pid);
          if (AudioTrack)
          {
            AudioTrack->streamType = STREAMTYPE_SUBTITLE;
            if(*AudioTrack->desc) printf(" [%s]\n", AudioTrack->desc); else printf("\n");
          }
          if ((SubtitlesPID == (word)-1) || (AudioTrack && *AudioTrack->desc && (strncmp(AudioTrack->desc, "deu", 3) == 0 || strncmp(AudioTrack->desc, "ger", 3) == 0)))
            SubtitlesPID = pid;
          TRACEEXIT;
          return STREAMTYPE_SUBTITLE;
        }
      }
    }
    else if (HeaderFound && ((History & 0xFFF00000) == 0xFFF00000))
    {
      const char* AudioModes[] = {"Stereo", "Joint Stereo", "Dual Channel", "Single Channel"};
      const float AudioSampling[] = {44.1f, 48, 32, 0};
      const short Bitrates[] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1};
      tAudioHeader *SeqHeader = (tAudioHeader*) &PSBuffer[p-4];

      printf("  TS: PID=%d, AudioType: MPEG%d, Layer %d @ %.1f kHz, %s, %hd kbit/s", pid, 2-SeqHeader->MpegVersion, 4-SeqHeader->Layer, AudioSampling[SeqHeader->SamplingFreq], AudioModes[SeqHeader->Mode], ((SeqHeader->Layer==2) ? Bitrates[SeqHeader->BitrateIndex]: SeqHeader->BitrateIndex*32));
      if (AudioTrack)
      {
        AudioTrack->streamType = STREAMTYPE_AUDIO;
//        if (!AudioTrack->type)
          AudioTrack->type  = (SeqHeader->MpegVersion == 1) ? STREAM_AUDIO_MPEG1 : STREAM_AUDIO_MPEG2;
        AudioTrack->layer   = SeqHeader->Layer;
        AudioTrack->mode    = SeqHeader->Mode;
        AudioTrack->bitrate = SeqHeader->BitrateIndex;
        if(*AudioTrack->desc) printf(" [%s]\n", AudioTrack->desc); else printf("\n");
      }
      TRACEEXIT;
      return STREAMTYPE_AUDIO;
    }
    History = (History << 8) | PSBuffer[p++];
  }

  TRACEEXIT;
  return STREAMTYPE_UNKNOWN;
}

// PES Headers: https://en.wikipedia.org/wiki/Packetized_elementary_stream
static bool AnalyseVideo(byte *PSBuffer, int BufSize, word pid, int *const VidHeight, int *const VidWidth, double *const VidFPS, double *const VidDAR)
{
  const double          FrameRateValues[10]  = {0, 24000/1001.0, 24.0, 25.0, 30000/1001.0, 30.0, 50.0, 60000/1001.0, 60.0, 0};
  const double          AspectRatioValues[6] = {0, 1.0, 4/3.0, 16/9.0, 2.21, 0}; 
  const char*           AspectRatioTexts[6]   = {"forbidden", "1:1", "4:3", "16:9", "2.21:1", "unknown"};
  int                   p = 0;
  dword                 History = 0xffffffff;
  bool                  PESHeaderFound = FALSE;
  TRACEENTER;

  while (p + 10 < BufSize)
  {
    // PES-Header (MPEG2 Video, bei Medion ggf. abweichend?)
    // http://www.fr-an.de/projects/mpeg/k030304.htm
    if ((History & 0xFFFFFFF0) == 0x000001E0)
    {
      tPESHeader *PESHeader = (tPESHeader*) &PSBuffer[p-4];
      PESHeaderFound = TRUE;
      p = p-4 + 7 + (PESHeader->OptionalHeaderMarker ? PESHeader->PESHeaderLen + 2 : 0);
    }

    // Video Sequence Header (MPEG2)
    // http://www.fr-an.de/projects/mpeg/k010202.htm
    if (PESHeaderFound && (History & 0xFFFFFFFF) == 0x000001B3)
    {
      tSequenceHeader *SeqHeader = (tSequenceHeader*) &PSBuffer[p-4];

      if (VidWidth)  *VidWidth  = (SeqHeader->Width1 << 4) + SeqHeader->Width2;
      if (VidHeight) *VidHeight = (SeqHeader->Height1 << 8) + SeqHeader->Height2;
      if (VidDAR && (SeqHeader->AspectRatio > 0) && (SeqHeader->AspectRatio < sizeof(AspectRatioValues)))
        *VidDAR = AspectRatioValues[SeqHeader->AspectRatio];
      if (VidFPS && (SeqHeader->FrameRate > 0) && (SeqHeader->FrameRate < sizeof(FrameRateValues)))
        *VidFPS = FrameRateValues[SeqHeader->FrameRate];
//      Bitrate = (((SeqHeader->Bitrate1 << 8) + SeqHeader->Bitrate2) << 3) + SeqHeader->Bitrate3;

      if ((SeqHeader->Height1 << 8) + SeqHeader->Height2 == 576)
        isHDVideo = FALSE;
      printf("  TS: PID=%hd, VideoType: MPEG2, %dx%d @ %.3f fps, AspectRatio=%s (%.3f) -> SD\n", pid, VideoWidth, VideoHeight, VideoFPS, AspectRatioTexts[SeqHeader->AspectRatio], VideoDAR);
      TRACEEXIT;
      return (SeqHeader->AspectRatio > 0) && (SeqHeader->FrameRate > 0);
    }

    // SPS NALU frame (H.264)
    // https://stackoverflow.com/questions/25398584/how-to-find-resolution-and-framerate-values-in-h-264-mpeg-2-ts
//    if (PESHeaderFound && (History & 0xFFFFFF1F) == 0x00000107)
    if (PESHeaderFound && ((History & 0xFFFFFFFF) == 0x00000167 || (History & 0xFFFFFFFF) == 0x00000127))
    {
      const char *AspectString = "special";
      int RealLength;

      RealLength = EBSPtoRBSP(&PSBuffer[p], BufSize-p);
      if ((RealLength < 0) || (p + RealLength >= BufSize))
      {
        TRACEEXIT;
        return FALSE;
      }
      if (ParseSPS(&PSBuffer[p], RealLength, VidHeight, VidWidth, VidFPS, VidDAR))
        isHDVideo = TRUE;

      if ((VideoDAR - 1.777 <= 0.001) && (VideoDAR - 1.777 >= 0))
        AspectString = AspectRatioTexts[AR_16to9];
      else if ((VideoDAR - 1.363 <= 0.001) && (VideoDAR - 1.333 >= 0))
        AspectString = AspectRatioTexts[AR_4to3];
      else if ((VideoDAR - 1.0 <= 0.001) && (1.0 - VideoDAR <= 0.001))
        AspectString = AspectRatioTexts[AR_1to1];

      printf("  TS: PID=%hd, VideoType: H.264, %dx%d @ %.3f fps, AspectRatio=%s (%.3f) -> HD\n", pid, VideoWidth, VideoHeight, VideoFPS, AspectString, VideoDAR);
      TRACEEXIT;
      return (*VidFPS > 0) && (*VidDAR > 0);
    }

    History = (History << 8) | PSBuffer[p++];
  }

  TRACEEXIT;
  return FALSE;
}

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf)
{
  FILE                 *fIn2 = fIn;
  tPSBuffer             PMTBuffer, EITBuffer, TtxBuffer;
  byte                 *Buffer = NULL, *pEPGBuffer = EPGBuffer;
  int                   LastPMTBuffer = 0, LastEITBuffer = 0, LastTtxBuffer = 0;
  byte                  FrameType = 0;
  tPESHeader           *curPESPacket = NULL;
  word                  PMTPID = 0;
  tPVRTime              TtxTime = 0;
  byte                  TtxTimeSec = 0;
  dword                 TtxPCR = 0, TtxPTS = 0, curPTS = 0;
  int                   dPCR = 0, dPTS = 0;
  int                   Offset, ReadBytes, d, d_max=128, i;
  bool                  PMTOK = FALSE, EITOK = FALSE, SDTOK = FALSE, TtxFound = FALSE, TtxOK = FALSE, VidOK = FALSE;
  bool                  FirstFilePTSOK = FALSE, LastFilePTSOK = FALSE, AllPidsScanned = FALSE;
  int                   AudOK = 0;
  byte                 *p;
  long long             FilePos = 0;
  bool                  ret = FALSE;

  const byte            ANDMask[7] = {0xFF, 0xC0, 0x00, 0xD0, 0xFF, 0xFF, 0xFC};
  const byte            PMTMask[7] = {0x47, 0x40, 0x00, 0x10, 0x00, 0x02, 0xB0};

  TRACEENTER;
  Buffer = (byte*) malloc((MedionMode == 1 ? 655360 : 98304 + 192));
  if (!Buffer)
  {
    printf("  Failed to allocate the buffer.\n");
    TRACEEXIT;
    return FALSE;
  }

  FirstFilePCR = 0; LastFilePCR = 0; FirstFilePTS = 0; LastFilePTS = 0; FirstFilePTSOK = FALSE; LastFilePTSOK = FALSE;
  memset (PATPMTBuf, 0, 4*192 + 5);
  memset (&EITBuffer, 0, sizeof(tPSBuffer));

//  rewind(fIn);
//  if (!HumaxSource && !EycosSource)
//    InitInfStruct(RecInf);

  //Spezial-Anpassung, um Medion-PES (EPG und Teletext) auszulesen
  if (MedionMode)
  {
    FILE               *fMDIn = NULL;
    char                AddFileIn[FBLIB_DIR_SIZE], *p;
    size_t              len;
    bool                IsFirst, IsLast, Contains2;

    if ((p = strrchr(RecFileIn, '/'))) p++;
    else if ((p = strrchr(RecFileIn, '\\'))) p++;
    else p = RecFileIn;

    if (p[0] == '[')
    {
      dword year = 0, month = 0, day = 0;
      if (sscanf(p, "[%u-%u-%u]", &year, &month, &day) == 3)
        RecInf->RecHeaderInfo.StartTime = Unix2TFTime(MakeUnixTime((word)year, (byte)month, (byte)day, 0, 0, 0, NULL), NULL, TRUE);
    }

    strcpy(AddFileIn, RecFileIn);
    if((p = strrchr(AddFileIn, '.'))) *p = '\0';
    if(((len = strlen(AddFileIn)) > 6) && (strncmp(&AddFileIn[len-6], "_video", 6) == 0)) AddFileIn[len-6] = '\0';
    len = strlen(AddFileIn);

    if (MedionMode == 1)
    {
      RecInf->ServiceInfo.ServiceType = 0;
      RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG2;
      RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG1;
      RecInf->ServiceInfo.AudioTypeFlag = 0;

      // Read first 640 kB of Video PES
      if ((ReadBytes = (int)fread(Buffer, 1, 655360, fIn)))
      {
        int p = 0;
        while (p < ReadBytes - 1000)
        {
          while ((p < ReadBytes - 1000) && (Buffer[p] != 0 || Buffer[p+1] != 0 || Buffer[p+2] != 1))
            p++;
          
          if (FirstFilePTSOK < 3 && GetPTS(&Buffer[p], &curPTS, NULL))
          {
            if (FindPictureHeader(&Buffer[p], min(200, (int)(&Buffer[ReadBytes]-&Buffer[p])), &FrameType, NULL))
            {
              if (FrameType == 1)  // zuerst vom I-Frame nehmen
              {
                if (!FirstFilePTSOK)
                {
                  FirstFilePTS = curPTS;
                  FirstFilePTSOK = 1;
                }
                else FirstFilePTSOK = 3;
              }
              else if (FirstFilePTSOK)
              {
                if(FrameType == 2) FirstFilePTSOK = 2;
                if ((FirstFilePTSOK == 2) && ((int)(FirstFilePTS - curPTS) > 0))
                  FirstFilePTS = curPTS;
              }
              if (FirstFilePTS && (!FirstFilePCR || (FirstFilePCR - ((long long)FirstFilePTS * 600) + PCRTOPTSOFFSET_SD > 0)))
                FirstFilePCR = (long long)FirstFilePTS * 600 - PCRTOPTSOFFSET_SD;
            }
          }

          if (!VidOK)
            VidOK = AnalyseVideo(&Buffer[p], ReadBytes - p, 0, &VideoHeight, &VideoWidth, &VideoFPS, &VideoDAR);
          if (VidOK && isHDVideo) RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG4_H264;

          if(FirstFilePTSOK >= 3 && (!DoInfoOnly || VidOK)) break;
          p++;
        }
      }
      else
      {
        printf("  Failed to read the first %d PES bytes.\n", 524288);
        free(Buffer);
        TRACEEXIT;
        return FALSE;
      }

      // Read last 500 kB of Video PES
      fseeko64(fIn, -524288, SEEK_END);
      if ((ReadBytes = (int)fread(Buffer, 1, 524288, fIn)) == 524288)
      {
        int p = ReadBytes - 14;
        while (p >= 0)
        {
          while ((p > 0) && (Buffer[p] != 0 || Buffer[p+1] != 0 || Buffer[p+2] != 1 || Buffer[p+3] < 0xe0 || Buffer[p+3] > 0xef))
            p--;
          if (GetPTS(&Buffer[p], &curPTS, NULL) && !LastFilePTSOK)
          {
            if (FindPictureHeader(&Buffer[p], min(300, (int)(&Buffer[ReadBytes]-&Buffer[p])), &FrameType, NULL))
            {
              if (!LastFilePTS || (int)(curPTS - LastFilePTS) > 0)
                LastFilePTS = curPTS;
              else if (LastFilePTS && FrameType == 1)
              {
                LastFilePTSOK = TRUE;
                break;
              }
            } 
          }
          p--;
        }
        if (!LastFilePCR && LastFilePTS)
          LastFilePCR = (long long)LastFilePTS * 600 - PCRTOPTSOFFSET_SD;
      }
      else
      {
        printf("  Failed to read the last %d PES bytes.\n", 524288);
        free(Buffer);
        TRACEEXIT;
        return FALSE;
      }
    }

    // Read first 64 kB of Audio PES
    strcat (AddFileIn, "_audio1.pes");
    if ((fMDIn = fopen(AddFileIn, "rb")))
    {
      if ((ReadBytes = (int)fread(Buffer, 1, 65536, fMDIn)) > 0)
      {
        int p = 0;
        while (p < ReadBytes - 1000)
        {
          while ((p < ReadBytes - 1000) && (Buffer[p] != 0 || Buffer[p+1] != 0 || Buffer[p+2] != 1))
            p++;
          if (AnalyseAudio(&Buffer[p], 65536-p, 0, &AudioPIDs[0]))
          {
/*            if (AudioPIDs[0].type > 4)
            {
              RecInf->ServiceInfo.AudioStreamType = AudioPIDs[0].type;
/*              switch (RecInf->ServiceInfo.AudioStreamType)
              {
                case STREAM_AUDIO_MPEG4_AC3:
                  RecInf->ServiceInfo.AudioTypeFlag = 1; break;
                case STREAM_AUDIO_MPEG4_AAC:
                case STREAM_AUDIO_MPEG4_AAC_PLUS:
                  RecInf->ServiceInfo.AudioTypeFlag = 2; break;
                default:
                  RecInf->ServiceInfo.AudioTypeFlag = 3;
              } *//*
            } */
            AudioPIDs[0].scanned = 1;
            strncpy(AudioPIDs[0].desc, "deu", 4);
            if(VidOK) ret = TRUE;
            break;
          }
          p++;
        }
      }
      fclose(fMDIn);
    }

    // Read first 96 kB of Teletext PES
    strcpy (&AddFileIn[len], "_ttx.pes");
    if ((fMDIn = fopen(AddFileIn, "rb")))
    {
      dword FirstTtxPTS = 0;
      int p = 0, res = 0;

      if ((ReadBytes = (int)fread(Buffer, 1, 98304, fMDIn)) > 0)
      {
        while (p < ReadBytes - 1288)
        {
          while ((p < ReadBytes - 1288) && (Buffer[p] != 0 || Buffer[p+1] != 0 || Buffer[p+2] != 1 || Buffer[p+3] != 0xBD))
            p++;

          // For Teletext-Stream with unmatched PTS -> enable special mode with offset
          if (!FirstTtxPTS)
            if (GetPTS(&Buffer[p], &FirstTtxPTS, NULL) && FirstTtxPTS && (abs((int)(FirstTtxPTS - FirstFilePTS)) > 450000))
            {
              TtxPTSOffset = FirstFilePTS - FirstTtxPTS;
//              printf("  TS: Enable special PTS fixing mode. (FirstFilePTS=%u, FirstTtxPTS=%u)\n", FirstFilePTS, FirstTtxPTS);
            }

          if (TtxOK < 2)
          {
            res = AnalyseTtx(&Buffer[p], ReadBytes-p, (p > ReadBytes/2), RecInf->RecHeaderInfo.StartTime, &TtxTime, &TtxTimeSec, &TtxTimeZone, RecInf->ServiceInfo.ServiceName, sizeof(RecInf->ServiceInfo.ServiceName));
            if(res > TtxFound) TtxFound = res;
            if((TtxFound && !TtxOK) || (TtxFound >= 2 && TtxOK <= 1))
              TtxOK = (GetPTS(&Buffer[p], &TtxPTS, NULL) && (TtxPTS != 0)) ? TtxFound : TtxOK;
          }
          else
          {
//            printf("  TS: TeletextPID=%hd\n", TeletextPID);
            break;
          }
          p++;
        }
        if(TtxOK)
          TtxPCR = (TtxPTS + TtxPTSOffset - 66666) / 45;
      }

      if (ExtractAllTeletext >= 2)
      {
        int i;
        fseek(fMDIn, p, SEEK_SET);
        TtxProcessor_SetOverwrite(TRUE);

        for (i = 0; i < 300; i++)
        {
          if ((ReadBytes = (int)fread(Buffer, 1, 236992, fMDIn)) > 0)
          {
            p = 0;
            while (p < ReadBytes - 1288)
            {
              while ((p < ReadBytes - 1288) && (Buffer[p] != 0 || Buffer[p+1] != 0 || Buffer[p+2] != 1 || Buffer[p+3] != 0xBD))
                p++;
              p += process_pes_packet(&Buffer[p], min(ReadBytes-p, 4096));  // 1288 / 1472
            }
          }
          else break;
        }
      }
      fclose(fMDIn);
    }

    // Read EPG Event file
    RecInf->ServiceInfo.ServiceID = 1;

    IsLast    = (len>2 && strncmp(&AddFileIn[len-2], "-2", 2) == 0);
    Contains2 = (len>4 && strncmp(&AddFileIn[len-4], "-2", 2) == 0);
    IsFirst   = (len>2 && strncmp(&AddFileIn[len-2], "-1", 2) == 0 && !Contains2);
    if (IsLast)
    {
      AddFileIn[len-1] = '1';
      strcpy(&AddFileIn[len], "_epg.txt");
      if(HDD_FileExist(AddFileIn))
      {
        if(!Contains2) IsFirst = TRUE;
      }
      else
        AddFileIn[len-1] = '2';
    }
    else if (IsFirst)
    {
      AddFileIn[len-1] = '2';
      strcpy(&AddFileIn[len], "-1_epg.txt");
      if (HDD_FileExist(AddFileIn))
        len += 2;
      else
        AddFileIn[len-1] = '1';
    }

    strcpy (&AddFileIn[len], "_epg.txt");
    if ((MJD(RecInf->RecHeaderInfo.StartTime) >= 54009) && (fMDIn = fopen(AddFileIn, "rb")))  // 54009 = 2006-10-01 (davor kaputte EPGs)
    {
      memset(Buffer, 0, 16384);
      if ((ReadBytes = (int)fread(Buffer, 1, 16384, fMDIn)) > 0)
      {
        byte *p = Buffer;
        int PTSDuration = DeltaPCR(FirstFilePTS, LastFilePTS) / 45000;
        tPVRTime MidTimeUTC = AddTimeSec(TtxTime, TtxTimeSec, NULL, TtxTimeZone + PTSDuration/2);
        bool OverwriteEPG = (!RecInf->EventInfo.StartTime);

        while ((p - Buffer < 16380) && (*p == 0x00))
          p++;

        if (*(int*)p == 0x12345678)
        {
          while ((p - Buffer < ReadBytes) && (*(int*)p == 0x12345678))
          {
            int EITLen = *(int*)(&p[4]);
            tTSEIT *EIT = (tTSEIT*)(&p[8]);
            TYPE_Event_Info tmpEvent;
            RecInf->ServiceInfo.ServiceID = (EIT->ServiceID1 * 256 | EIT->ServiceID2);
            if ((EITOK = AnalyseEIT(&p[8], ReadBytes - (int)(p-Buffer), RecInf->ServiceInfo.ServiceID, &TransportStreamID, (OverwriteEPG ? &RecInf->EventInfo : &tmpEvent), &RecInf->ExtEventInfo, OverwriteEPG)))
            {
              EPGLen = 0;
              if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
              if (EITLen && ((EPGBuffer = (byte*)malloc(EITLen + 1))))
              {
                EPGBuffer[0] = 0;  // Pointer field (=0) vor der TableID (nur im ersten TS-Paket der Tabelle, gibt den Offset an, an der die Tabelle startet, z.B. wenn noch Reste der vorherigen am Paketanfang stehen)
                memcpy(&EPGBuffer[1], &p[8], EITLen);
                pEPGBuffer = EPGBuffer;
                EPGLen = EITLen + 1;
              }

              if (!IsFirst && (IsLast || !TtxOK || (/*(RecInf->EventInfo.StartTime <= MidTimeUTC) &&*/ ((OverwriteEPG ? RecInf->EventInfo : tmpEvent).EndTime >= MidTimeUTC))))
                break;
            }
            p = p + 8 + EITLen + 53;
          }
        }
        else
          printf("  Medion-EIT unreadable! ServiceID possibly %hu?\n", *(word*) &Buffer[0xe58]);
      }
      fclose(fMDIn);
    }

    RecInf->ServiceInfo.PMTPID = 0;

    GetPidsFromMap(&RecInf->ServiceInfo.ServiceID, &RecInf->ServiceInfo.PMTPID, &VideoPID, &AudioPIDs[0].pid, &TeletextPID, NULL, RecInf->ServiceInfo.ServiceName);
    if(!RecInf->ServiceInfo.ServiceID) RecInf->ServiceInfo.ServiceID = 1;
    if(!RecInf->ServiceInfo.PMTPID) RecInf->ServiceInfo.PMTPID = 256;
    printf("  TS: SID=%hu, PCRPID=%hd, PMTPID=%hd\n", RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID, RecInf->ServiceInfo.PMTPID);

    AudioPIDs[0].streamType = STREAMTYPE_AUDIO;
    AudioPIDs[1].streamType = STREAMTYPE_TELETEXT;
    AudioPIDs[1].pid = TeletextPID;
    RecInf->ServiceInfo.VideoPID = VideoPID;
    RecInf->ServiceInfo.AudioPID = AudioPIDs[0].pid;
    RecInf->ServiceInfo.PCRPID = VideoPID;
    SimpleMuxer_SetPIDs(VideoPID, AudioPIDs[0].pid, TeletextPID);
    printf("  Using new PMTPID=%hd, VideoPID=%hd, AudioPID=%hd, TtxPID=%hd\n", RecInf->ServiceInfo.PMTPID, VideoPID, AudioPIDs[0].pid, TeletextPID);

    if(!TtxFound)
      printf ("  Failed to get Teletext information.\n");
    if(!EITOK)
      printf ("  Failed to get the EIT information.\n");
  }


  if (MedionMode != 1)
  {
    tTSPacket          *curPacket = NULL, *lastPacket = NULL;
    word                curPID = 0;
    bool                PTSLastPayloadStart = FALSE;
    dword               PTSLastEndNulls = 0;
    PMTPID = 0;
    PSBuffer_Init(&PMTBuffer, PMTPID,  4096, TRUE, TRUE);
    PSBuffer_Init(&EITBuffer, 0x0012, 16384, TRUE, TRUE);

/*    ReadBytes = (int)fread(Buffer, 1, 5760, fIn);
    if(ReadBytes < 5760)
    {
      printf("  Failed to read the first 5760 TS bytes.\n");
      free(Buffer);
      TRACEEXIT;
      return FALSE;
    }
    Offset = FindNextPacketStart(Buffer, ReadBytes); */


    for (d = (DoFixPMT ? 0 : -1); d < 10; d++)
    {
      if (d < 0)
      {
        // Versuche erst PMT am Anfang der Aufnahme zu finden (gestrippte Aufnahmen mit PMT/EPG nur in den ersten Paketen)
        FilePos = 0;
        fseeko64(fIn, FilePos, SEEK_SET);  // nicht n�tig, wenn fseek(CurrentPosition) in LoadInfFromRec - generell eig. nicht n�tig
      }
      if (d == 0)
      {
        // Springe in die Mitte der Aufnahme (f�r Medion-Analyse mit PAT/PMT nur am Start, die folgenden 13 Zeilen auskommentieren [und die Zeile "fseeko64(fIn, FilePos, SEEK_SET)" auf 0 setzen -> nicht mehr n�tig(?)])
        if (HumaxSource)
          FilePos = ((RecFileSize/2)/HumaxHeaderIntervall * HumaxHeaderIntervall);
        else
          FilePos = ((RecFileSize/2)/PACKETSIZE * PACKETSIZE);
        fseeko64(fIn, FilePos, SEEK_SET);
      }

      // Read 512/504 TS packets from the middle
      ReadBytes = (int)fread(Buffer, 1, (HumaxSource ? 3*32768 : 512*PACKETSIZE), fIn);
      if(ReadBytes < (HumaxSource ? 3*32768 : 512*PACKETSIZE))
      {
        printf("  Failed to read %d x %d TS bytes from the rec file.\n", d, (HumaxSource ? 3*32768 : 512*PACKETSIZE));
        free(Buffer);
        TRACEEXIT;
        return FALSE;
      }

      // Wenn PMT direkt am Anfang, merken
      if (d < 0)
      {
        tTSPacket *packet1, *packet2, *curPacket;
        int k = 0;

        Offset = FindNextPacketStart(Buffer, ReadBytes);
        packet1 = (tTSPacket*) &Buffer[Offset + PACKETOFFSET];
        packet2 = (tTSPacket*) &Buffer[Offset + PACKETSIZE + PACKETOFFSET];

        if (((packet1->PID1 * 256 + packet1->PID2 == 0) && (packet1->Data[0] == 0) && (packet1->Data[1] == TABLE_PAT)) && ((packet2->Data[0] == 0) && (packet2->Data[1] == TABLE_PMT) && ((packet2->Data[2] & 0xFC) == 0xB0)))
        {
          PMTPID = packet2->PID1 * 256 + packet2->PID2;
          printf("  TS: PMTPID=%hd", PMTPID);
          PSBuffer_Init(&PMTBuffer, PMTPID, 4096, TRUE, TRUE);
          PMTatStart = TRUE;

          // Kopiere PAT/PMT/EIT-Pakete vom Dateianfang in Buffer (nur beim ersten File-Open?)
          memset(PATPMTBuf, 0, 4*192 + 5);

          curPacket = packet1;
          for (k = 0; (k*PACKETSIZE < ReadBytes) && ((curPacket->PID1 * 256 + curPacket->PID2 == 0) || (curPacket->PID1 * 256 + curPacket->PID2 == PMTPID)); k++)
          {
            if(curPacket->PID1 * 256 + curPacket->PID2 == PMTPID) PSBuffer_ProcessTSPacket(&PMTBuffer, curPacket);
            memcpy(&PATPMTBuf[((PACKETSIZE==192) ? 0 : 4) + k*192], &Buffer[Offset + k*PACKETSIZE], PACKETSIZE);
            curPacket = (tTSPacket*)&Buffer[Offset + (k+1)*PACKETSIZE + PACKETOFFSET];
          }
          if (PMTBuffer.ValidBuffer == 1)
          {
            PMTOK = AnalysePMT(PMTBuffer.Buffer1, PMTBuffer.ValidBufLen, RecInf);
            if(PMTOK) RecInf->ServiceInfo.PMTPID = PMTPID;
          }
          WriteDescPackets = TRUE;

          NrEPGPacks = 0;
          p = (byte*)curPacket;
          while ((((tTSPacket*)p)->PID1 == 0) && (((tTSPacket*)p)->PID2 == 18))
          {
            NrEPGPacks++;
            p += PACKETSIZE;
          }
            
          p = &Buffer[Offset + k*PACKETSIZE];
          if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
          if (NrEPGPacks && ((EPGPacks = (byte*)malloc(NrEPGPacks * 192))))
          {
            memset(EPGPacks, 0, NrEPGPacks * 192);
            for (k = 0; k < NrEPGPacks; k++)
            {
              PSBuffer_ProcessTSPacket(&EITBuffer, (tTSPacket*) (&p[k*PACKETSIZE + PACKETOFFSET]));
              memcpy(&EPGPacks[((PACKETSIZE==192) ? 0 : 4) + k*192], &p[k*PACKETSIZE], PACKETSIZE);
            }
            EITOK = AnalyseEIT(EITBuffer.Buffer1, EITBuffer.ValidBufLen, RecInf->ServiceInfo.ServiceID, &TransportStreamID, &RecInf->EventInfo, &RecInf->ExtEventInfo, TRUE);
          }
        }
        else continue;
      }

      Offset = FindNextPacketStart(Buffer, ReadBytes);
      if (Offset >= 0)
      {
        if (!HumaxSource && !EycosSource && !TechniSource)
        {
          //Find a PMT packet to get its PID
          p = &Buffer[Offset + PACKETOFFSET];

          if (!PMTPID)
          {
            while (p <= &Buffer[ReadBytes-188])
            {
              bool ok = TRUE;

              for (i = 0; i < (int)sizeof(PMTMask); i++)
                if((p[i] & ANDMask[i]) != PMTMask[i])
                  { ok = FALSE; break; }

              if (ok)
              {
                PMTPID = ((p[1] << 8) | p[2]) & 0x1fff;
                printf("  TS: PMTPID=%hd", PMTPID);
                break;
              }

              p += PACKETSIZE;
            }
          }

          if(PMTPID && !PMTOK)
          {
            RecInf->ServiceInfo.PMTPID = PMTPID;

            //Analyse the PMT
            PSBuffer_Init(&PMTBuffer, PMTPID, 4096, TRUE, TRUE);

//            p = &Buffer[Offset + PACKETOFFSET];
            while (p <= &Buffer[ReadBytes-188])
            {
              curPacket = (tTSPacket*)p;
              curPID = (curPacket->PID1 *256) + curPacket->PID2;
              if (curPID == PMTPID)
                PSBuffer_ProcessTSPacket(&PMTBuffer, (tTSPacket*)p);
              if ((PMTBuffer.ValidBuffer != LastPMTBuffer) || (PMTatStart && (PMTBuffer.BufferPtr > 0) && (curPID != PMTPID) && (p < &Buffer[10*PACKETSIZE])))
              {
                if(!PMTBuffer.ErrorFlag) break;
                PMTBuffer.ErrorFlag = FALSE;
                LastPMTBuffer = PMTBuffer.ValidBuffer;
              }
              p += PACKETSIZE;
            }

            if (PMTBuffer.ValidBuffer || (d < 0 || d == 10))
            {
              if ((PMTOK = AnalysePMT((PMTBuffer.ValidBuffer==2) ? PMTBuffer.Buffer2 : PMTBuffer.Buffer1, (PMTBuffer.ValidBuffer ? PMTBuffer.ValidBufLen : PMTBuffer.BufferPtr), RecInf)))
              {
                byte *pBuffer = (PMTBuffer.ValidBuffer==2) ? PMTBuffer.Buffer2 : PMTBuffer.Buffer1;
                int BufLen = (PMTBuffer.ValidBuffer) ? PMTBuffer.ValidBufLen : PMTBuffer.BufferPtr;
                int k = 0;

                memset(&PATPMTBuf[192], 0, 3 * 192 + 5);
                for (k = 0; (k == 0) || (k < 3 && 183 + (k-1)*184 < BufLen); k++)
                {
                  tTSPacket* packet = (tTSPacket*) &PATPMTBuf[4 + (k+1)*192];
                  packet->SyncByte = 'G';
                  packet->PID1 = PMTPID / 256;
                  packet->PID2 = PMTPID & 0xff;
                  packet->Payload_Exists = 1;
                  packet->Payload_Unit_Start = (k == 0) ? 1 : 0;
                  packet->ContinuityCount = k;
                  memset(packet->Data, 0xff, 184);
                  if (k == 0)
                  {
                    packet->Data[0] = 0;
                    memcpy(&packet->Data[1], &pBuffer[0], min(BufLen, 183));
                  }
                  else
                    memcpy(packet->Data, &pBuffer[183 + (k-1)*184], min(BufLen - 183 + (k-1)*184, 184));
                }
              }
            }
          }
        }
      }
      if(PMTPID && PMTOK && d >= 0) break;
    }
    PSBuffer_Reset(&PMTBuffer);
    if (!RecInf->ServiceInfo.PMTPID)
    {
      printf("  Failed to locate a PMT packet.\n");
      RecInf->ServiceInfo.ServiceID = 1;
      WriteDescPackets = TRUE;
//      ret = FALSE;
    }


    //If we're here, it should be possible to find the associated EPG event
//    if (RecInf->ServiceInfo.ServiceID)
    {
      word TeletextPID_PMT = TeletextPID;
      bool HasTeletext = FALSE;
      int NrIterations = (ExtractAllTeletext >= 2) ? 10000 : 300;
      PSBuffer_Init(&PMTBuffer, 0x0011, 4096, TRUE, TRUE);    // wird hier jetzt f�r die SDT verwendet
      PSBuffer_Init(&TtxBuffer, TeletextPID, 4096, FALSE, TRUE);  // eigentlich: 1288 / 1472
      LastPMTBuffer = 0; LastEITBuffer = 0; LastTtxBuffer = 0;

      fseeko64(fIn, FilePos + Offset, SEEK_SET);  // Hier auf 0 setzen (?)
      for (i = 0; i < NrIterations; i++)
      {
        ReadBytes = (int)fread(Buffer, PACKETSIZE, 168, fIn) * PACKETSIZE;
        p = &Buffer[PACKETOFFSET];

        while (p <= &Buffer[ReadBytes-188])
        {
          curPacket = (tTSPacket*)p;
          curPID = (curPacket->PID1 *256) + curPacket->PID2;
          if (PATPMTBuf[4] != 'G')
          {
            if ((curPID == 0) && curPacket->Payload_Unit_Start)
            {
              tTSPacket* packet = (tTSPacket*) &PATPMTBuf[4];
              memcpy(packet, p, 188);
              packet->ContinuityCount = 0;
              
//              ((tTSPAT*) packet->Data)->PMTPID1 = PMTPID / 256;
//              ((tTSPAT*) packet->Data)->PMTPID2 = PMTPID & 0xff;
            }
          }
          if (!SDTOK && (curPID == 0x0011))
          {
            PSBuffer_ProcessTSPacket(&PMTBuffer, curPacket);
            if(PMTBuffer.ValidBuffer != LastPMTBuffer)
            {
              byte* pBuffer = (PMTBuffer.ValidBuffer==2) ? PMTBuffer.Buffer2 : PMTBuffer.Buffer1;
              SDTOK = !PMTBuffer.ErrorFlag && AnalyseSDT(pBuffer, PMTBuffer.ValidBufLen, RecInf->ServiceInfo.ServiceID, RecInf);
              PMTBuffer.ErrorFlag = FALSE;
              LastPMTBuffer = PMTBuffer.ValidBuffer;
            }
          }
          if (!EITOK && (curPID == 18))
          {
            PSBuffer_ProcessTSPacket(&EITBuffer, curPacket);
            if ((EITBuffer.ValidBuffer != LastEITBuffer) || (PMTatStart && (i==0) && (EITBuffer.BufferPtr > 0) && (curPID != 18) && (p < &Buffer[10*PACKETSIZE])))
            {
              byte *pBuffer = (EITBuffer.ValidBuffer==2) ? EITBuffer.Buffer2 : EITBuffer.Buffer1;
              EPGLen = EITBuffer.ValidBufLen ? EITBuffer.ValidBufLen : EITBuffer.BufferPtr;
              if ((EITOK = (!EITBuffer.ErrorFlag && AnalyseEIT(pBuffer, EPGLen, RecInf->ServiceInfo.ServiceID, &TransportStreamID, &RecInf->EventInfo, &RecInf->ExtEventInfo, TRUE))))
              {
                // hier EPGPacks f�llen (-> nach unten verschieben?)
                pEPGBuffer = pBuffer;
              }
              EITBuffer.ErrorFlag = FALSE;
              LastEITBuffer = EITBuffer.ValidBuffer;
            }
          }
          if ((!TtxOK || TtxOK < TtxFound || (TtxOK < 2 && i < 56) || ExtractAllTeletext >= 2) && (TeletextPID != 0xffff) && (curPID == TeletextPID))
          {
            PSBuffer_ProcessTSPacket(&TtxBuffer, curPacket);
            if (TtxBuffer.ValidBuffer != LastTtxBuffer)
            {
              byte *pBuffer = (TtxBuffer.ValidBuffer==2) ? TtxBuffer.Buffer2 : TtxBuffer.Buffer1; int res = 0;
// IDEE: Hier vielleicht den Teletext-String in EventNameDescription schreiben, FALLS L�nge gr��er ist als EventNameLength und !EITOK

              if (!TtxBuffer.ErrorFlag)
              {
                if (TtxFound <= 1 || TtxOK <= 1 )
                {
                  if ((res = AnalyseTtx(pBuffer, TtxBuffer.ValidBufLen, (i > 28), RecInf->RecHeaderInfo.StartTime, &TtxTime, &TtxTimeSec, &TtxTimeZone, ((!SDTOK && !KeepHumaxSvcName) ? RecInf->ServiceInfo.ServiceName : NULL), sizeof(RecInf->ServiceInfo.ServiceName))) > TtxFound)
                  {
                    GetPTS(pBuffer, &TtxPTS, NULL);
                    TtxFound = res;
                  }
                }
                if (ExtractAllTeletext >= 2)
                  process_pes_packet(pBuffer, TtxBuffer.ValidBufLen);
              }
/*              TtxFound = !TtxBuffer.ErrorFlag && AnalyseTtx(pBuffer, TtxBuffer.ValidBufLen, &TtxTime, &TtxTimeSec, &TtxTimeZone, (!EITOK ? RecInf->EventInfo.EventNameDescription : ((!SDTOK && !KeepHumaxSvcName) ? RecInf->ServiceInfo.ServiceName : NULL)), sizeof(RecInf->ServiceInfo.ServiceName));
              if (!EITOK && *RecInf->EventInfo.EventNameDescription)
              {
                RecInf->EventInfo.EventNameLength = (byte)strlen(RecInf->EventInfo.EventNameDescription);
                if (!SDTOK && !KeepHumaxSvcName)
                  strncpy(RecInf->ServiceInfo.ServiceName, RecInf->EventInfo.EventNameDescription, sizeof(RecInf->ServiceInfo.ServiceName));
              } */
              TtxBuffer.ErrorFlag = FALSE;
              LastTtxBuffer = TtxBuffer.ValidBuffer;
            }
          }
          if((TtxFound && !TtxOK) || (TtxFound >= 2 && TtxOK <= 1))
            TtxOK = (GetPCRms(p, &TtxPCR) && (TtxPCR != 0)) ? TtxFound : TtxOK;

          if (!VidOK)
          {
            if ((!PMTPID || (curPID == VideoPID)) && curPacket->Payload_Unit_Start)
            {
              if (curPacket->Adapt_Field_Exists)
                VidOK = AnalyseVideo((byte*)&curPacket->Data + curPacket->Data[0] + 1, sizeof(curPacket->Data) - curPacket->Data[0] - 1, (curPacket->PID1 * 256 | curPacket->PID2), &VideoHeight, &VideoWidth, &VideoFPS, &VideoDAR);
              else
                VidOK = AnalyseVideo((byte*)&curPacket->Data, sizeof(curPacket->Data), (curPacket->PID1 * 256 | curPacket->PID2), &VideoHeight, &VideoWidth, &VideoFPS, &VideoDAR);

              if (VidOK)
              {
                RecInf->ServiceInfo.ServiceType = 0;  // SVC_TYPE_Tv
                if(RecInf->ServiceInfo.VideoStreamType == 0xff)
                {
                  VideoPID = curPID;
                  RecInf->ServiceInfo.VideoPID = VideoPID;
                  RecInf->ServiceInfo.PCRPID = VideoPID;
                  RecInf->ServiceInfo.VideoStreamType = (isHDVideo ? STREAM_VIDEO_MPEG4_H264 : STREAM_VIDEO_MPEG2);
                  ContinuityPIDs[0] = VideoPID;
                }
              }
            }
          }

          if (curPID && (curPID != VideoPID) && (curPID != 18) && (curPID != PMTPID) && (curPID != TeletextPID || !HasTeletext) && curPacket->Payload_Unit_Start)
          {
            int k;
            for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != curPID); k++);
            if ((k < MAXCONTINUITYPIDS) && !AudioPIDs[k].scanned /*&& !AudioPIDs[k].streamType*/)
            {
              if (AnalyseAudio((byte*)&curPacket->Data + (curPacket->Adapt_Field_Exists ? curPacket->Data[0] + 1 : 0), sizeof(curPacket->Data) - (curPacket->Adapt_Field_Exists ? curPacket->Data[0] - 1 : 0), curPID, &AudioPIDs[k]))
              {
                AudioPIDs[k].pid = curPID;
                AudioPIDs[k].scanned = 1;
                AddContinuityPids(curPID, FALSE);
                AudOK++;
                if (!TtxOK && (TeletextPID != TeletextPID_PMT) && (AudioPIDs[k].streamType == STREAMTYPE_TELETEXT))
                {
                  TeletextPID = curPID;
                  TtxBuffer.PID = TeletextPID;
                  PSBuffer_DropCurBuffer(&TtxBuffer);
                  HasTeletext = TRUE;
                }
                else if (AudioPIDs[k].streamType == STREAMTYPE_SUBTITLE)
                  SubtitlesPID = AudioPIDs[k].pid;
              }
/*                else
                if (AudioPIDs[k].pid == curPID)
                  AudioPIDs[k].streamType = STREAMTYPE_UNKNOWN;  */
            }
          }

          if(((PMTatStart && !RebuildInf && !DoInfoOnly && !DoInfFix && !DoFixPMT)                    || (EITOK && (SDTOK || (!RebuildInf && !DoInfFix)))) && (TtxOK>=2 ||                    ((PMTPID || EycosSource) && TeletextPID == 0xffff)) && ((PMTPID && (!HumaxSource && !EycosSource && !TechniSource)) || AudOK>=3)                          && ((PMTPID && !DoInfoOnly && !DoFixPMT) || VidOK) && (ExtractAllTeletext <= 1 || TeletextPID==(word)-1))
            break;
          p += PACKETSIZE;
        }
        if ((HumaxSource || EycosSource) && !AllPidsScanned)
        {
          int k;
          AllPidsScanned = TRUE;
          for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && AllPidsScanned; k++)
            if (!AudioPIDs[k].scanned) AllPidsScanned = FALSE;
        }
        if( ((PMTatStart && !RebuildInf && !DoInfoOnly && !DoInfFix && !DoFixPMT) || (AllPidsScanned) || (EITOK && (SDTOK || (!RebuildInf && !DoInfFix)))) && (TtxOK>=2 || (TtxOK && i>56) || ((PMTPID || EycosSource) && TeletextPID == 0xffff)) && ((PMTPID && (!HumaxSource && !EycosSource && !TechniSource)) || AudOK>=3 || (HumaxSource && i>56)) && ((PMTPID && !DoInfoOnly && !DoFixPMT) || VidOK))
        {
          if (ExtractAllTeletext <= 1 || TeletextPID==(word)-1)
          {
            ret = TRUE;
            break;
          }
          else if (ExtractAllTeletext == 2)
          {
            ExtractAllTeletext++;
            if ((long long int)RecFileSize < 2 * NrIterations * PACKETSIZE * 168)
            {
              fseeko64(fIn, 0 + Offset, SEEK_SET);
              PSBuffer_DropCurBuffer(&TtxBuffer);
              continue;
            }
          }
        }
        if(HumaxSource)
          fseeko64(fIn, +HumaxHeaderLaenge, SEEK_CUR);
      }

      // Setze Audio-Type in inf
      if ((AudioPIDs[0].pid > 0) && (DoFixPMT || (RecInf->ServiceInfo.AudioStreamType == 0xff)))
      {
        RecInf->ServiceInfo.AudioPID = AudioPIDs[0].pid;
        RecInf->ServiceInfo.AudioStreamType = AudioPIDs[0].type;  // (AudioPIDs[0].type <= 1) ? ((AudioPIDs[0].type == 1) ? STREAM_AUDIO_MPEG1 : STREAM_AUDIO_MPEG2) : AudioPIDs[0].type;
        switch (RecInf->ServiceInfo.AudioStreamType)
        {
          case 0:
          case 1:
          case STREAM_AUDIO_MPEG1:
          case STREAM_AUDIO_MPEG2:
            RecInf->ServiceInfo.AudioTypeFlag = 0;
            break;
          case STREAM_AUDIO_MPEG4_AC3_PLUS:
          case STREAM_AUDIO_MPEG4_AC3:
            RecInf->ServiceInfo.AudioTypeFlag = 1;
            break;
          case STREAM_AUDIO_MPEG4_AAC:
          case STREAM_AUDIO_MPEG4_AAC_PLUS:
            RecInf->ServiceInfo.AudioTypeFlag = 2;
            break;
          default:
            RecInf->ServiceInfo.AudioTypeFlag = 3;
        }
      }

      if (TeletextPID != (word) -1) AddContinuityPids(TeletextPID, FALSE);
      AddContinuityPids(0x12, FALSE);

      if(!SDTOK)
        printf ("  Failed to get service name from SDT.\n");
      if (!RecInf->ServiceInfo.ServiceID || RecInf->ServiceInfo.ServiceID==1 || !RecInf->ServiceInfo.PMTPID || !*RecInf->ServiceInfo.ServiceName)
      {
        if (!KeepHumaxSvcName)
          RecInf->ServiceInfo.ServiceID = GetSidFromMap(VideoPID, 0 /*GetMinimalAudioPID(AudioPIDs)*/, TeletextPID, RecInf->ServiceInfo.ServiceName, &RecInf->ServiceInfo.PMTPID, FALSE);  // zweiter Versuch, ggf. �berschreiben
        if(!RecInf->ServiceInfo.ServiceID) RecInf->ServiceInfo.ServiceID = 1;
        if(!RecInf->ServiceInfo.PMTPID) RecInf->ServiceInfo.PMTPID = 256;
      }
      if (!EITOK && PMTatStart && (EITBuffer.ValidBuffer == 0) && (LastEITBuffer == 0))
        EITOK = !EITBuffer.ErrorFlag && AnalyseEIT(EITBuffer.Buffer1, EITBuffer.BufferPtr, RecInf->ServiceInfo.ServiceID, &TransportStreamID, &RecInf->EventInfo, &RecInf->ExtEventInfo, TRUE);  // Versuche EIT trotzdem zu parsen (bei gestrippten Aufnahmen gibt es kein Folge-Paket, das den Payload_Unit_Start ausl�st)
      if (!EITOK)
        printf ("  Failed to get the EIT information.\n");
      if (TeletextPID != 0xffff && !TtxOK)
        printf ("  Failed to get start time from Teletext.\n");
      PSBuffer_Reset(&PMTBuffer);
      PSBuffer_Reset(&TtxBuffer);
    }


    //Read the first TS pakets to detect start PCR / PTS
    fseeko64(fIn, 0, SEEK_SET);
    for (d = 0; d < 20; d++)
    {
      // Read the first 512/504 TS packets
      ReadBytes = (int)fread(Buffer, 1, (HumaxSource ? 3*32768 : 512*PACKETSIZE), fIn);
      if(ReadBytes < (HumaxSource ? 3*32768 : 512*PACKETSIZE))
      {
        printf("  Failed to read the first %d x %d TS bytes.\n", d, (HumaxSource ? 3*32768 : 512*PACKETSIZE));
        free(Buffer);
        TRACEEXIT;
        return FALSE;
      }

      Offset = FindNextPacketStart(Buffer, ReadBytes);
      if (Offset >= 0)
      {
        p = &Buffer[Offset];
        while ((p <= &Buffer[ReadBytes-16]) && (!FirstFilePCR || ((DoInfoOnly || HumaxSource || EycosSource) && FirstFilePTSOK < 3)))
        {
          if (p[PACKETOFFSET] != 'G')
          {
            if (HumaxSource && (*((dword*)p) == HumaxHeaderAnfang))
            {
              p += HumaxHeaderLaenge;
              if (&p[HumaxHeaderLaenge] >= &Buffer[ReadBytes]) continue;
            }
            else
            {
              Offset = FindNextPacketStart(p, (int)(&Buffer[ReadBytes] - p));
              if(Offset >= 0)  p += Offset;
              else break;
            }
          }
          //Find the first PCR (for duration calculation)
          if(!FirstFilePCR) GetPCR(&p[PACKETOFFSET], &FirstFilePCR);

          // Alternative: Find the first Video PTS
          if ((DoInfoOnly || HumaxSource || EycosSource) && FirstFilePTSOK < 3)
          {
            curPacket = (tTSPacket*) &p[PACKETOFFSET];
            if ((curPacket->SyncByte == 'G') && (curPacket->PID1 * 256 + curPacket->PID2 == VideoPID) && (curPacket->Payload_Unit_Start || PTSLastPayloadStart))
            {
              curPESPacket = (tPESHeader*) &curPacket->Data[(curPacket->Adapt_Field_Exists) ? curPacket->Data[0] + 1 : 0];
              if (curPacket->Payload_Unit_Start)
                GetPTS((byte*) curPESPacket, &curPTS, NULL);
              if (FindPictureHeader((byte*) curPESPacket, 184 - ((curPacket->Adapt_Field_Exists) ? curPacket->Data[0] + 1 : 0), &FrameType, &PTSLastEndNulls))
              {
                if (FrameType == 1)  // zuerst vom I-Frame nehmen
                {
                  if (!FirstFilePTSOK++)
                    FirstFilePTS = curPTS;
                  else FirstFilePTSOK = 3;
                }
                else if (FirstFilePTSOK)
                {
                  if(FrameType == 2)
                    FirstFilePTSOK = 2;
                  if ((FirstFilePTSOK == 2) && ((int)(FirstFilePTS - curPTS) > 0))
                    FirstFilePTS = curPTS;
                }
              }
              PTSLastPayloadStart = (curPacket->Payload_Unit_Start && !FrameType);
            }
          }
          p += PACKETSIZE;
        }
      }
      if(FirstFilePCR && ((!DoInfoOnly && !HumaxSource && !EycosSource) || FirstFilePTSOK >= 3))
        break;
    }

    //Read the last RECBUFFERENTRIES TS pakets
//    FilePos = FilePos + ((((RecFileSize-FilePos)/PACKETSIZE) - RECBUFFERENTRIES) * PACKETSIZE);
    if (EycosSource)
    {
      char LastEycosPart[FBLIB_DIR_SIZE];
      int EycosNrParts = EycosGetNrParts(RecFileIn);
      if (EycosNrParts > 1)
        fIn2 = fopen(EycosGetPart(LastEycosPart, RecFileIn, EycosNrParts-1), "rb");
    }

    for (d = 1; d <= d_max; d++)
    {
      // Read the last 512/504 TS packets
      fseeko64(fIn2, -d * (HumaxSource ? 3*32768 : 512*PACKETSIZE), SEEK_END);
      ReadBytes = (int)fread(Buffer, 1, (HumaxSource ? 3*32768 : 512*PACKETSIZE), fIn2);
      if ((d > 1) && lastPacket)
      {
        memcpy(&Buffer[ReadBytes], lastPacket, PACKETSIZE);
        lastPacket = (tTSPacket*) &Buffer[ReadBytes];
      }

      if(ReadBytes < (HumaxSource ? 3*32768 : 512*PACKETSIZE))
      {
        printf ("  Failed to read the last %d x %d TS bytes.\n", d, (HumaxSource ? 3*32768 : 512*PACKETSIZE));
        free(Buffer);
        TRACEEXIT;
        return FALSE;
      }

      Offset = FindPrevPacketStart(Buffer, ReadBytes);
      if (Offset >= 0)
      {
        p = &Buffer[Offset];
        while ((p >= &Buffer[0]) && (!LastFilePCR || ((DoInfoOnly || HumaxSource || EycosSource) && !LastFilePTSOK)))
        {
          if (HumaxSource && (((p - Buffer) % HumaxHeaderIntervall) >= HumaxHeaderIntervall-HumaxHeaderLaenge))
          {
            if ((p >= Buffer + HumaxHeaderLaenge) && (p[-HumaxHeaderLaenge] == 'G'))
              p -= HumaxHeaderLaenge;
          }
          else if (p[PACKETOFFSET] != 'G')
          {
            Offset = FindPrevPacketStart(Buffer, (int)(p - &Buffer[0]));
            if (Offset >= 0)  p = &Buffer[Offset];
            else break;
          }

          //Find the last PCR
          if(!LastFilePCR) GetPCR(&p[PACKETOFFSET], &LastFilePCR);

          // Alternative: Find the last Video PTS
          if ((DoInfoOnly || HumaxSource || EycosSource) && !LastFilePTSOK)
          {
            curPacket = (tTSPacket*) &p[PACKETOFFSET];
            if ((curPacket->SyncByte == 'G') && (curPacket->PID1 * 256 + curPacket->PID2 == VideoPID))
            {
              // erstes Video Paket gefunden
              if (d_max == 128)
                d_max = d + 20;

              if (curPacket->Payload_Unit_Start)
              {
                curPESPacket = (tPESHeader*) &curPacket->Data[(curPacket->Adapt_Field_Exists) ? curPacket->Data[0] + 1 : 0];
                GetPTS((byte*) curPESPacket, &curPTS, NULL);
                if (FindPictureHeader((byte*) curPESPacket, 184 - ((curPacket->Adapt_Field_Exists) ? curPacket->Data[0] + 1 : 0), &FrameType, NULL)
                 || (lastPacket && FindPictureHeader((byte*) &lastPacket->Data[(lastPacket->Adapt_Field_Exists) ? lastPacket->Data[0] + 1 : 0], 184 - ((lastPacket->Adapt_Field_Exists) ? lastPacket->Data[0] + 1 : 0), &FrameType, NULL)))
                {
                  if (!LastFilePTS || (int)(curPTS - LastFilePTS) > 0)
                    LastFilePTS = curPTS;
                  else if (LastFilePTS && FrameType == 1)
                    LastFilePTSOK = TRUE;
                }
              }
              lastPacket = curPacket;
            }
          }
          p -= PACKETSIZE;
        }
      }
      if(LastFilePCR && ((!DoInfoOnly && !HumaxSource && !EycosSource) || LastFilePTSOK)) break;
    }
    
    if (EycosSource && (fIn2 != fIn))
      fclose(fIn2);
  }

  // hier EPGPacks f�llen (au�er bei SimpleMuxer/Medion)
  if (DoGenerateEIT && !pEPGBuffer && RecInf->EventInfo.StartTime)
  {
    GenerateEIT(RecInf->ServiceInfo.ServiceID, TF2UnixTime(RecInf->EventInfo.StartTime, 0, FALSE), RecInf->EventInfo.DurationHour, RecInf->EventInfo.DurationMin, RecInf->EventInfo.EventNameDescription, RecInf->EventInfo.EventNameLength, &RecInf->EventInfo.EventNameDescription[RecInf->EventInfo.EventNameLength], (int)strlen(&RecInf->EventInfo.EventNameDescription[RecInf->EventInfo.EventNameLength]), ExtEPGText, (int)strlen(ExtEPGText), RecInf->ServiceInfo.AudioStreamType);
    pEPGBuffer = EPGBuffer;
  }
  if (!DoInfoOnly)
  {
    free(ExtEPGText); ExtEPGText = NULL;
  }

  if (pEPGBuffer && (MedionMode != 1))
  {
    int k;

/*    if (pEPGBuffer[0] == 0)
    {
      pEPGBuffer++;
      EPGLen--;
    } */
    NrEPGPacks = ((EPGLen + 182) / 184);

    if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
    if (NrEPGPacks && ((EPGPacks = (byte*)malloc(NrEPGPacks * 192))))
    {
      memset(EPGPacks, 0, NrEPGPacks * 192);
      for (k = 0; k < NrEPGPacks; k++)
      {
        tTSPacket* packet = (tTSPacket*) &EPGPacks[4 + k*192];
        packet->SyncByte = 'G';
        packet->PID2 = 18;
        packet->Payload_Exists = 1;
        packet->Payload_Unit_Start = (k == 0) ? 1 : 0;
        packet->ContinuityCount = k;
        memset(packet->Data, 0xff, 184);
        if (k == 0)
        {
          packet->Data[0] = 0;
          memcpy(&packet->Data[1], &pEPGBuffer[0], min(EPGLen, 183));
          EPGLen -= min(EPGLen, 183);
        }
        else
        {
          memcpy(packet->Data, &pEPGBuffer[183 + (k-1)*184], min(EPGLen, 184));
          EPGLen -= min(EPGLen, 184);
        }
      }
    }
  }
  PSBuffer_Reset(&EITBuffer);

  if (EITOK)
  {
    int time_offset;
    tPVRTime StartTime = EPG2TFTime(RecInf->EventInfo.StartTime, &time_offset);

    // EPG Event Start- und EndTime an lokale Zeitzone anpassen (-> nicht n�tig! EPG wird immer in UTC gespeichert!)
//    RecInf->EventInfo.StartTime = AddTimeSec(RecInf->EventInfo.StartTime, 0, NULL, (TtxOK ? -1*TtxTimeZone : time_offset));  // GMT+1
//    RecInf->EventInfo.EndTime   = AddTimeSec(RecInf->EventInfo.EndTime,   0, NULL, (TtxOK ? -1*TtxTimeZone : time_offset));

printf("  TS: EvtStart  = %s (GMT%+d)\n", TimeStrTF(StartTime, 0), time_offset / 3600);
  }

  RecInf->RecHeaderInfo.DurationMin = 120;
  RecInf->RecHeaderInfo.DurationSec = 0;

  if(FirstFilePCR && LastFilePCR)
  {
    dword FirstPCRms = (dword)(FirstFilePCR / 27000);
    dword LastPCRms = (dword)(LastFilePCR / 27000);
    dPCR = DeltaPCRms(FirstPCRms, LastPCRms);
    RecInf->RecHeaderInfo.DurationMin = (word)(dPCR / 60000);
    RecInf->RecHeaderInfo.DurationSec = (word)abs((dPCR / 1000) % 60);
    printf("  TS: FirstPCR  = %lld (%01u:%02u:%02u,%03u), Last: %lld (%01u:%02u:%02u,%03u)\n", FirstFilePCR, (FirstPCRms/3600000), (FirstPCRms/60000 % 60), (FirstPCRms/1000 % 60), (FirstPCRms % 1000), LastFilePCR, (LastPCRms/3600000), (LastPCRms/60000 % 60), (LastPCRms/1000 % 60), (LastPCRms % 1000));
  }

  if(FirstFilePTSOK >= 1 && LastFilePTS)
  {
    dword FirstPTSms = (dword)(FirstFilePTS / 45);
    dword LastPTSms = (dword)(LastFilePTS / 45);
    dPTS = DeltaPCR(FirstFilePTS, LastFilePTS) / 45;
    RecInf->RecHeaderInfo.DurationMin = (word)(dPTS / 60000);
    RecInf->RecHeaderInfo.DurationSec = (word)abs((dPTS / 1000) % 60);
    printf("  TS: FirstPTS  = %u (%01u:%02u:%02u,%03u), Last: %u (%01u:%02u:%02u,%03u)\n", FirstFilePTS, (FirstPTSms/3600000), (FirstPTSms/60000 % 60), (FirstPTSms/1000 % 60), (FirstPTSms % 1000), LastFilePTS, (LastPTSms/3600000), (LastPTSms/60000 % 60), (LastPTSms/1000 % 60), (LastPTSms % 1000));
  }
  if (FirstFilePCR && LastFilePCR)
  {
    printf("  TS: Duration  = %01d:%02u:%02u,%03u (PCR)", dPCR / 3600000, abs(dPCR / 60000) % 60, abs(dPCR / 1000) % 60, abs(dPCR) % 1000);
    if (FirstFilePTSOK >= 1 && LastFilePTS)
      printf(" / %01d:%02u:%02u,%03u (PTS)", dPTS / 3600000, abs(dPTS / 60000) % 60, abs(dPTS / 1000) % 60, abs(dPTS) % 1000);
    printf("\n");
  }
  else if (FirstFilePTSOK >= 1 && LastFilePTS)
    printf("  TS: Duration  = %01d:%02u:%02u,%03u (PTS)\n", dPTS / 3600000, abs(dPTS / 60000) % 60, abs(dPTS / 1000) % 60, abs(dPTS) % 1000);
  else
    printf("  Duration calculation failed (missing PCR). Using 120 minutes.\n");

  if (!HumaxSource && !(EycosSource && RecInf->RecHeaderInfo.StartTime) && !(MedionMode && RecInf->RecHeaderInfo.StartTimeSec) && !(DVBViewerSrc && RecInf->RecHeaderInfo.StartTime))
  {
    tPVRTime FileTimeTF = Unix2TFTime(RecFileTimeStamp, NULL, FALSE);

    RecInf->RecHeaderInfo.StartTime = EPG2TFTime(RecInf->EventInfo.StartTime, NULL);  // EventStart in lokale Zeit konvertieren und als StartTime setzen

    // Wenn rec-Datei am selben oder n�chsten Tag ge�ndert wurde, wie das EPG-Event -> nimm Datum der Datei (TODO: ABER inf dann nicht �berschreiben?)
    if (!RecInf->EventInfo.StartTime || ((MJD(FileTimeTF) - MJD(RecInf->RecHeaderInfo.StartTime) <= 1) && (FileTimeTF > RecInf->RecHeaderInfo.StartTime)))
    {
      byte sec;
      printf("  TS: Setting start time to file time instead EPG event start.\n");
      FileTimeTF = Unix2TFTime(RecFileTimeStamp /*- (RecInf->RecHeaderInfo.DurationMin*60 + RecInf->RecHeaderInfo.DurationSec)*/, &sec, TRUE);
      RecInf->RecHeaderInfo.StartTime = FileTimeTF;
      RecInf->RecHeaderInfo.StartTimeSec = sec;
    }
  }
  if(TtxTime && TtxPCR)
  {
    dword FirstPCRms = (dword)(FirstFilePCR / 27000);
    int dPCR = 0;

    if (MJD(TtxTime) < 40952)
      TtxTime = DATE(MJD(RecInf->RecHeaderInfo.StartTime), HOUR(TtxTime), MINUTE(TtxTime));

    if(FirstFilePCR && LastFilePCR)
    {
      dword LastPCRms = (dword)(LastFilePCR / 27000);
      if (LastPCRms < FirstPCRms) LastPCRms += 95443718;  // �berlauf
      if (((TtxPCR + 10000 >= FirstPCRms) && (TtxPCR <= LastPCRms)) || ((TtxPCR+95443718 + 10000 >= FirstPCRms) && (TtxPCR+95443718 <= LastPCRms)))
        dPCR = DeltaPCRms(FirstPCRms, TtxPCR);
    }
    RecInf->RecHeaderInfo.StartTime = AddTimeSec(TtxTime, TtxTimeSec, &RecInf->RecHeaderInfo.StartTimeSec, -dPCR / 1000);

    if ((HumaxSource || EycosSource) && TtxPTS && FirstFilePCR && FirstFilePTSOK)
    {
      TtxPTSOffset = FirstFilePTS - (TtxPTS - (45 * (dword)DeltaPCRms(FirstPCRms, TtxPCR)));
      if (abs((int)TtxPTSOffset) > 450000)
        printf("    -> Enable special PTS fixing mode, Teletext PTS offset=%u.\n", TtxPTSOffset);
      else
        TtxPTSOffset = 0;
    }
  }
printf("  TS: StartTime = %s\n", (TimeStrTF(RecInf->RecHeaderInfo.StartTime, RecInf->RecHeaderInfo.StartTimeSec)));

  // Schreibe PAT/PMT in "Datenbank"
//#if defined(_WIN32) && defined(_DEBUG)
/*  if ((PATPMTBuf[4] == 'G') && (PATPMTBuf[196] == 'G'))
  {
    FILE *fPMT = NULL;
    char ServiceString[100];
    char *p = TimeStr_DB(RecInf->RecHeaderInfo.StartTime, 0);

    snprintf(ServiceString, sizeof(ServiceString), "%hu_%hd_%hd_%.10s_%.2s-%.2s_%.70s.pmt", RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.VideoPID, RecInf->ServiceInfo.AudioPID, p, &p[11], &p[14], RecInf->ServiceInfo.ServiceName);
    if ((fPMT = fopen(ServiceString, "wb")))
    {
      fwrite(PATPMTBuf, 1, 4*192, fPMT);
      fclose(fPMT);
    }
  } */
//#endif

  free(Buffer);
  TRACEEXIT;
  return ret;
}


void SortAudioPIDs(tAudioTrack AudioPIDs[])
{
  tAudioTrack           tmp;
  int                   curPid, minPid, minPos, StreamTypeSort;
  int                   i, j, k;

  for (i = 0; (i < MAXCONTINUITYPIDS) && (AudioPIDs[i].pid != 0) && AudioPIDs[i].sorted; i++);  // sortierte �berspringen

  for (j = i; (j < MAXCONTINUITYPIDS) && (AudioPIDs[j].pid != 0) && !AudioPIDs[j].sorted; j++)  // PIDs der Gr��e nach sortieren (au�er die un-gescannten)
  {
    minPid = 0x7fffffff;
    minPos = j;
    for (k = j; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && !AudioPIDs[k].sorted; k++)
    {
      StreamTypeSort = (AudioPIDs[j].streamType) ? AudioPIDs[j].streamType : 4;
      curPid = (StreamTypeSort << 17) + (!AudioPIDs[j].scanned ? (int)1 << 16 : 0) + ((strncmp(AudioPIDs[j].desc, "deu", 3)==0 || strncmp(AudioPIDs[j].desc, "ger", 3)==0) ? 0 : ((int)1 << (AudioPIDs[j].streamType + 15))) + AudioPIDs[j].pid;
      if(curPid < minPid && AudioPIDs[j].streamType != STREAMTYPE_TELETEXT && AudioPIDs[j].streamType != STREAMTYPE_SUBTITLE)
      {
        minPid = curPid;
        minPos = k;
      }
    }
    if ((minPos != j) && AudioPIDs[j].streamType != STREAMTYPE_TELETEXT && AudioPIDs[j].streamType != STREAMTYPE_SUBTITLE)  // setze Track mit minimaler PID an die aktuelle Position j
    {
      tmp = AudioPIDs[j];
      AudioPIDs[j] = AudioPIDs[minPos];
      AudioPIDs[minPos] = tmp;
    }
  }
}

// Generate a PMT
void GeneratePatPmt(byte *const PATPMTBuf, word ServiceID, word TransportID, word PMTPid, word VideoPID, word PCRPID, tAudioTrack AudioPIDs[], bool PATonly)
{
  tTSPacket            *Packet = NULL;
  tTSPAT               *PAT = NULL;
  tTSPMT               *PMT = NULL;
  tElemStream          *Elem = NULL;
  tTSStreamDesc        *Desc0 = NULL;
  dword                *CRC = NULL;
  const char*           LangArr[] = {"deu", "mis", "mul"};
  int                   Offset = 0;
  int                   StreamTag = 1, k;
  bool                  TeletextDone = FALSE, SubtitlesDone = FALSE;

  TRACEENTER;
  memset(PATPMTBuf, 0, 192 * (PATonly ? 1 : 4));
  printf("  SID=%hu, PMTPid=%hd, PCRPID=%hd\n", ServiceID, PMTPid, PCRPID);
  
  // PAT/PMT initialisieren
  Packet = (tTSPacket*) &PATPMTBuf[4];
  PAT = (tTSPAT*) &Packet->Data[1 /*+ Packet->Data[0]*/];

  Packet->SyncByte      = 'G';
  Packet->PID1          = 0;
  Packet->PID2          = 0;
  Packet->Payload_Unit_Start = 1;
  Packet->Payload_Exists = 1;

  PAT->TableID          = TABLE_PAT;
  PAT->SectionLen1      = 0;
  PAT->SectionLen2      = sizeof(tTSPAT) - 3;
  PAT->Reserved1        = 3;
  PAT->Private          = 0;
  PAT->SectionSyntax    = 1;
  PAT->TS_ID1           = TransportID / 256;
  PAT->TS_ID2           = (TransportID & 0xff);  // 6 ??
  PAT->CurNextInd       = 1;
  PAT->VersionNr        = 1;  // 3 ??
  PAT->Reserved2        = 3;
  PAT->SectionNr        = 0;
  PAT->LastSection      = 0;
  PAT->ProgramNr1       = ServiceID / 256;
  PAT->ProgramNr2       = (ServiceID & 0xff);
  PAT->PMTPID1          = PMTPid / 256;
  PAT->PMTPID2          = (PMTPid & 0xff);
  PAT->Reserved111      = 7;
//  PAT->CRC32            = rocksoft_crc((byte*)PAT, sizeof(tTSPAT)-4);    // CRC: 0x786989a2
//  PAT->CRC32            = crc32m((byte*)PAT, sizeof(tTSPAT)-4);          // CRC: 0x786989a2
  PAT->CRC32            = crc32m_tab((byte*)PAT, sizeof(tTSPAT)-4);      // CRC: 0x786989a2
  
  Offset = 1 + /*Packet->Data[0] +*/ sizeof(tTSPAT);
  memset(&Packet->Data[Offset], 0xff, 184 - Offset);

  if(PATonly) { TRACEEXIT; return; }

  Packet = (tTSPacket*) &PATPMTBuf[196];
  PMT = (tTSPMT*) &Packet->Data[1 /*+ Packet->Data[0]*/];

  Packet->SyncByte      = 'G';
  Packet->PID1          = (byte)PAT->PMTPID1;
  Packet->PID2          = (byte)PAT->PMTPID2;
  Packet->Payload_Unit_Start = 1;
  Packet->Payload_Exists = 1;

  PMT->TableID          = TABLE_PMT;
  PMT->SectionLen1      = 0;
  PMT->SectionLen2      = sizeof(tTSPMT) - 3 + 4;
  PMT->Reserved1        = 3;
  PMT->Private          = 0;
  PMT->SectionSyntax    = 1;
  PMT->ProgramNr1       = ServiceID / 256;
  PMT->ProgramNr2       = (ServiceID & 0xff);
  PMT->CurNextInd       = 1;
  PMT->VersionNr        = 1;
  PMT->Reserved2        = 3;
  PMT->SectionNr        = 0;
  PMT->LastSection      = 0;

  PMT->PCRPID1          = PCRPID / 256;
  PMT->Reserved3        = 7;
  PMT->PCRPID2          = (PCRPID & 0xff);

  PMT->ProgInfoLen1     = 0;
  PMT->ProgInfoLen2     = 0;
  PMT->Reserved4        = 15;

  Offset = 1 + /*Packet->Data[0] +*/ sizeof(tTSPMT);
  
  // Video-PID
  Elem = (tElemStream*) &Packet->Data[Offset];
  Elem->Reserved1       = 7;
  Elem->Reserved2       = 0xf;
  Offset               += sizeof(tElemStream);

  Elem->stream_type     = isHDVideo ? STREAM_VIDEO_MPEG4_H264 : STREAM_VIDEO_MPEG2;
  Elem->ESPID1          = VideoPID / 256;
  Elem->ESPID2          = (VideoPID & 0xff);
  Elem->ESInfoLen1      = 0;
  Elem->ESInfoLen2      = 0;

  Desc0 = (tTSStreamDesc*) &Packet->Data[Offset];
  Desc0->DescrTag       = DESC_StreamIdentifier;
  Desc0->DescrLength    = 1;
  Desc0->ComponentTag   = StreamTag++;
  Elem->ESInfoLen2     += sizeof(tTSStreamDesc);

  Offset               += Elem->ESInfoLen2;
  PMT->SectionLen2     += sizeof(tElemStream) + Elem->ESInfoLen2;
  printf("  Video Track:    PID=%d, %s, Type=0x%x\n", VideoPID, (isHDVideo ? "HD" : "SD"), Elem->stream_type);

  // Sortiere Audio-PIDs
//  SortAudioPIDs(AudioPIDs);

  // Audio-PIDs
  for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0); k++)
  {
    if (AudioPIDs[k].scanned && AudioPIDs[k].streamType == STREAMTYPE_AUDIO)
    {
      tTSAudioDesc *Desc    = NULL;

      Elem = (tElemStream*) &Packet->Data[Offset];
      Elem->ESPID1          = AudioPIDs[k].pid / 256;
      Elem->ESPID2          = (AudioPIDs[k].pid & 0xff);
      Elem->Reserved1       = 7;
      Elem->Reserved2       = 0xf;
      Elem->ESInfoLen1      = 0;
      Elem->ESInfoLen2      = sizeof(tTSStreamDesc);
      Offset               += sizeof(tElemStream);

      Desc0 = (tTSStreamDesc*) &Packet->Data[Offset];

      if ((AudioPIDs[k].type == STREAM_AUDIO_MPEG4_AC3_PLUS) || (AudioPIDs[k].type == STREAM_AUDIO_MPEG4_AC3) || (strncmp(AudioPIDs[k].desc, "AC", 2) == 0) || (strncmp(AudioPIDs[k].desc, "ac", 2) == 0))
      {
        tTSAC3Desc *Desc1   = (tTSAC3Desc*) &Packet->Data[Offset + sizeof(tTSStreamDesc)];
//        Desc0               = (tTSStreamDesc*) &Packet->Data[Offset];
        Desc                = (tTSAudioDesc*) &Packet->Data[Offset + sizeof(tTSStreamDesc) + sizeof(tTSAC3Desc)];

        Elem->stream_type   = AudioPIDs[k].type;  // STREAM_AUDIO_MPEG4_AC3_PLUS;
        Elem->ESInfoLen2   += (sizeof(tTSAC3Desc) + sizeof(tTSAudioDesc));
        Desc1->DescrTag     = DESC_AC3;
        Desc1->DescrLength  = 1;
        printf("  Audio Track %d:  PID=%d, AC3, Type=0x%x", (k + 1), AudioPIDs[k].pid, Elem->stream_type);
      }
      else if (AudioPIDs[k].type <= 4)
      {
        Desc = (tTSAudioDesc*) &Packet->Data[Offset + sizeof(tTSStreamDesc)];
        Elem->stream_type   = AudioPIDs[k].type;  // (AudioPIDs[k].type <= 1) ? ((AudioPIDs[k].type == 1) ? STREAM_AUDIO_MPEG1 : STREAM_AUDIO_MPEG2) : AudioPIDs[k].type;
        Elem->ESInfoLen2   += sizeof(tTSAudioDesc);
        printf("  Audio Track %d:  PID=%d, MPEG-%s, Type=0x%x", (k + 1), AudioPIDs[k].pid, (Elem->stream_type==STREAM_AUDIO_MPEG1 ? "1" : (Elem->stream_type==STREAM_AUDIO_MPEG2 ? "2" : "?")), Elem->stream_type);
      }
      else
      {
        Desc = (tTSAudioDesc*) &Packet->Data[Offset + sizeof(tTSStreamDesc)];
        Elem->stream_type   = AudioPIDs[k].type;
        Elem->ESInfoLen2   += sizeof(tTSAudioDesc);
        printf("  Audio Track %d:  PID=%d, Unknown Type: 0x%x", (k + 1), AudioPIDs[k].pid, Elem->stream_type);
      }
      if (Desc)
      {
        tTSSupplAudioDesc *Desc2 = (tTSSupplAudioDesc*) &Packet->Data[Offset + Elem->ESInfoLen2];
        Desc0->DescrTag       = DESC_StreamIdentifier;
        Desc0->DescrLength    = 1;
        Desc0->ComponentTag   = StreamTag++;

        Desc->DescrTag      = DESC_AudioLang;
        Desc->DescrLength   = 4;
        if (*AudioPIDs[k].desc)
          strncpy(Desc->LanguageCode, AudioPIDs[k].desc, sizeof(Desc->LanguageCode));
        else
          strncpy(Desc->LanguageCode, ((AudioPIDs[k].pid==5113 || AudioPIDs[k].pid==6222) ? "fra" : LangArr[(k<3) ? k : 0]), sizeof(Desc->LanguageCode));
        Desc->AudioFlag     = (AudioPIDs[k].desc_flag) ? AudioPIDs[k].desc_flag - 1 : (((strncmp(Desc->LanguageCode, "mul", 3) == 0) || (strncmp(Desc->LanguageCode, "qks", 3) == 0)) ? 2 : 0);
        printf(" [%.3s]", Desc->LanguageCode);

        if ((Desc->AudioFlag > 0) || (strncmp(Desc->LanguageCode, "mis", 3) == 0) || (strncmp(Desc->LanguageCode, "mul", 3) == 0) || (strncmp(Desc->LanguageCode, "qks", 3) == 0))
        {
          Elem->ESInfoLen2   += sizeof(tTSSupplAudioDesc);
          Desc2->DescrTag     = DESC_Extension;
          Desc2->DescrTagExt  = 0x06;
          Desc2->DescrLength  = 4;
          Desc2->mix_type     = 1;
          Desc2->reserved     = 1;
          Desc2->editorial_classification = (Desc->AudioFlag > 1) ? Desc->AudioFlag - 1 : ((strncmp(Desc->LanguageCode, "mis", 3) == 0) ? 1 : 2);   // 0=main audio, 1=audio description for vis. imp., 2=clean audio for hear. imp., 3=spoken subtitles
          Desc2->language_code_present = 1;
          strcpy(Desc->LanguageCode, ((strncmp(Desc->LanguageCode, "mul", 3) == 0) ? "mul" : "deu"));
          printf(" -> editorial class: 0x%hhx", Desc2->editorial_classification);
        }
      }

      printf("\n");
      Offset               += Elem->ESInfoLen2;
      PMT->SectionLen2     += sizeof(tElemStream) + Elem->ESInfoLen2;
    }

    else if ((AudioPIDs[k].streamType == STREAMTYPE_TELETEXT) /* && !RemoveTeletext */)
    {
      // Teletext-PID
      tTSTtxDesc *ttxDesc;

      Elem = (tElemStream*) &Packet->Data[Offset];
      Elem->Reserved1         = 7;
      Elem->Reserved2         = 0xf;
      Elem->stream_type       = 6;
      Elem->ESPID1            = AudioPIDs[k].pid / 256;
      Elem->ESPID2            = (AudioPIDs[k].pid & 0xff);
      Elem->ESInfoLen1        = 0;
      Elem->ESInfoLen2        = sizeof(tTSTtxDesc);
      Offset                 += sizeof(tElemStream);

      Desc0 = (tTSStreamDesc*) &Packet->Data[Offset];
      Desc0->DescrTag         = DESC_StreamIdentifier;
      Desc0->DescrLength      = 1;
      Desc0->ComponentTag     = StreamTag++;
      Elem->ESInfoLen2       += sizeof(tTSStreamDesc);

      ttxDesc = (tTSTtxDesc*) &Packet->Data[Offset + sizeof(tTSStreamDesc)];
      ttxDesc->DescrTag       = DESC_Teletext;

      ttxDesc->DescrLength    = 5;
      ttxDesc->ttx[0].teletext_type = 1;
      ttxDesc->ttx[0].magazine_nr = 1;
      ttxDesc->ttx[0].page_nr = 0;
      if (*AudioPIDs[k].desc)
        strcpy(ttxDesc->ttx[0].LanguageCode, AudioPIDs[k].desc);
      else
        strcpy(ttxDesc->ttx[0].LanguageCode, "deu");

      if (AudioPIDs[k].desc_flag)
      {
        Elem->ESInfoLen2     += 5;
        ttxDesc->DescrLength += 5;
        ttxDesc->ttx[1].teletext_type = 1;
        ttxDesc->ttx[1].magazine_nr = 1;
        ttxDesc->ttx[1].page_nr = AudioPIDs[k].desc_flag - 1;
        strcpy(ttxDesc->ttx[1].LanguageCode, ttxDesc->ttx[0].LanguageCode);
      }

      Offset                 += Elem->ESInfoLen2;
      PMT->SectionLen2       += sizeof(tElemStream) + Elem->ESInfoLen2;
      if(AudioPIDs[k].pid == TeletextPID) TeletextDone = TRUE;
      printf("  Teletext Track: PID=%d [%.3s]\n", AudioPIDs[k].pid, ttxDesc->ttx[0].LanguageCode);
    }
  
    else if (AudioPIDs[k].streamType == STREAMTYPE_SUBTITLE)
    {
      // Subtitles-PID
      tTSSubtDesc *subtDesc;

      Elem = (tElemStream*) &Packet->Data[Offset];
      Elem->Reserved1         = 7;
      Elem->Reserved2         = 0xf;
      Elem->stream_type       = 6;
      Elem->ESPID1            = AudioPIDs[k].pid / 256;
      Elem->ESPID2            = (AudioPIDs[k].pid & 0xff);
      Elem->ESInfoLen1        = 0;
      Elem->ESInfoLen2        = sizeof(tTSSubtDesc);
      Offset                 += sizeof(tElemStream);

      Desc0 = (tTSStreamDesc*) &Packet->Data[Offset];
      Desc0->DescrTag         = DESC_StreamIdentifier;
      Desc0->DescrLength      = 1;
      Desc0->ComponentTag     = StreamTag++;
      Elem->ESInfoLen2       += sizeof(tTSStreamDesc);

      subtDesc = (tTSSubtDesc*) &Packet->Data[Offset + sizeof(tTSStreamDesc)];
      subtDesc->DescrTag      = DESC_Subtitle;
      subtDesc->DescrLength   = 8;
      subtDesc->subtitling_type = (AudioPIDs[k].desc_flag) ? AudioPIDs[k].desc_flag - 1 : 36;
      subtDesc->composition_page_id2 = 1;
      subtDesc->ancillary_page_id2 = 1;

      if (*AudioPIDs[k].desc)
        strcpy(subtDesc->LanguageCode, AudioPIDs[k].desc);
      else
        strcpy(subtDesc->LanguageCode, "deu");

      Offset                 += Elem->ESInfoLen2;
      PMT->SectionLen2       += sizeof(tElemStream) + Elem->ESInfoLen2;
      if(AudioPIDs[k].pid == SubtitlesPID) SubtitlesDone = TRUE;
      printf("  Subtitle Track: PID=%d [%.3s]\n", AudioPIDs[k].pid, subtDesc->LanguageCode);
    }
  }

  if (((TeletextPID && TeletextPID != (word)-1) && !TeletextDone && !RemoveTeletext) || ((SubtitlesPID != (word)-1) && !SubtitlesDone))
    printf("ASSERTION ERROR! TeletextPID was not included in PMT since not found in AudioPIDs!\n");

  CRC                   = (dword*) &Packet->Data[Offset];
//  *CRC                  = rocksoft_crc((byte*)PMT, (int)CRC - (int)PMT);    // CRC: 0x0043710d  (0xb3ad75b7?)
//  *CRC                  = crc32m((byte*)PMT, (int)CRC - (int)PMT);          // CRC: 0x0043710d  (0xb3ad75b7?)
  *CRC                  = crc32m_tab((byte*)PMT, (byte*)CRC - (byte*)PMT);    // CRC: 0x0043710d  (0xb3ad75b7?)
  Offset               += 4;
  memset(&Packet->Data[Offset], 0xff, 184 - Offset);

  TRACEEXIT;
}

// Generate an artificial EIT
void GenerateEIT(word ServiceID, time_t StartTimeUnix, byte DurationHour, byte DurationMin, char *EventName, int EventNameLen, char *EventDesc, int EventDescLen, char *ExtEventText, int ExtEventTextLen, byte AudioStreamType)
{
  const char*           AudioModes[] = {"stereo", "joint stereo", "dual channel", "single channel"};
  int                   BufSize = 88 + EventNameLen + EventDescLen + ExtEventTextLen + 8 * (ExtEventTextLen / 255), i;
  byte                 *EITBuf = (byte*) malloc(BufSize);
  char                 *p = (char*) &EITBuf[(MedionMode == 1 ? 1 : 0)];                    //  1 byte (leading zero)
  tTSEIT               *eit  = (tTSEIT*) p;                                                // 14 bytes
  tEITEvent            *evt  = (tEITEvent*) &p[sizeof(tTSEIT)];                            // 12 bytes
  tShortEvtDesc        *desc = (tShortEvtDesc*) &p[sizeof(tTSEIT) + sizeof(tEITEvent)];    //  7 bytes + EventNameLen (<= 255) + EventDescLen (<= 255)
  tComponentDesc       *comp = NULL;                                                       //  8 bytes + AR (<= 4) + 8 bytes + AudioMode (<= 14)
  tExtEvtDesc          *desc2 = NULL;                                                      //  8 bytes * (ExtEventTextLen / 255) + ExtEventTextLen
  tUserDesc            *user = NULL;                                                       //  2 bytes + 13 bytes
  tPVRTime              StartTimeTF = Unix2TFTime(StartTimeUnix, NULL, FALSE);             //  4 bytes CRC
                                                                                           // = max 87 + EventNameLen + EventDescLen + ExtEventTextLen + 8 * (ExtEventTextLen / 255)
  if (EITBuf && StartTimeUnix)
  {
    printf("  -> Generating artificial EIT Event Table from map data!\n");
    memset(EITBuf, 0, BufSize);
    eit->TableID        = 0x4e;
    eit->SectionSyntax  = 1;
    eit->Private        = 1;
    eit->VersionNr      = 1;
    eit->CurNextInd     = TRUE;
    eit->ServiceID1     = ServiceID >> 8;
    eit->ServiceID2     = ServiceID & 0xff;
    eit->Reserved1      = 3;  // (all 1)
    eit->Reserved2      = 3;  // (all 1)
//   eit->TS_ID1         = 1101 >> 8;
//   eit->TS_ID2         = 1101 & 0xff;
    eit->OriginalID2    = 1;
    eit->LastSection    = 1;
    eit->SegLastSection = 1;
    eit->LastTable      = 0x4e;

//   evt->EventID1       = 0x3f;
//   evt->EventID2       = 0xa6;
    evt->EventID2       = 1;  // !! Identifier
    evt->RunningStatus  = 4;
    evt->StartTime[0]   = MJD(StartTimeTF) >> 8;
    evt->StartTime[1]   = MJD(StartTimeTF) & 0xff;
    evt->StartTime[2]   = BIN2BCD(HOUR(StartTimeTF));
    evt->StartTime[3]   = BIN2BCD(MINUTE(StartTimeTF));
    evt->DurationSec[0] = BIN2BCD(DurationHour);
    evt->DurationSec[1] = BIN2BCD(DurationMin);
      
    p = (char*) desc;
    desc->DescrTag = DESC_EITShortEvent;
    memcpy(desc->LanguageCode, "deu", 3);
    desc->EvtNameLen = EventNameLen;
    p += sizeof(tShortEvtDesc);
    memcpy(p, EventName, EventNameLen);
    p += EventNameLen;
    *((byte*)p++) = EventDescLen;
    memcpy(p, EventDesc, EventDescLen);
    p += EventDescLen;
    desc->DescrLength = (int)(p - (char*)desc) - 2;

    if (VideoDAR)
    {
      comp = (tComponentDesc*) p;
      comp->DescrTag       = DESC_Component;
      comp->reserved       = 0xf;
      comp->stream_content = 1;  // Video
      comp->component_type = 1;
      comp->component_tag  = 1;
      memcpy(comp->language_code, "deu", 3);
      p += sizeof(tComponentDesc);

      p[0] = 0;
      if ((VideoDAR - 1.777 <= 0.001) && (VideoDAR - 1.777 >= 0))
        strncpy(p, "16:9", 5);
      else if ((VideoDAR - 1.363 <= 0.001) && (VideoDAR - 1.333 >= 0))
        strncpy(p, "4:3", 4);
      else if ((VideoDAR - 1.0 <= 0.001) && (1.0 - VideoDAR <= 0.001))
        strncpy(p, "1:1", 4);
      p += strlen(p);
      comp->DescrLength = (int)(p - (char*)comp) - 2;
    }

    if (AudioStreamType)
    {
      comp = (tComponentDesc*) p;
      comp->DescrTag       = DESC_Component;
      comp->reserved       = 0xf;
      comp->stream_content = 2;  // Audio
      comp->component_type = AudioStreamType;  // 3
      comp->component_tag  = 2;
      memcpy(comp->language_code, "deu", 3);
      p += sizeof(tComponentDesc);
      memcpy(p, AudioModes[AudioPIDs[0].mode], strlen(AudioModes[AudioPIDs[0].mode]));
      p += strlen(p);
      comp->DescrLength = (int)(p - (char*)comp) - 2;
    }

/*      memcpy(p, "T\x0A\x15\0\x30\0\xBF\x25\xF0\x42\xF0\x43", 12);  // Component Descriptor
    p += 12; */

    for (i = 0; 249 * i < ExtEventTextLen; i++)
    {
      desc2 = (tExtEvtDesc*) p;
      desc2->DescrTag = DESC_EITExtEvent;
      desc2->DescrNr = i;
      desc2->LastDescrNr = (i > 0) ? i - 1 : 0;
      memcpy(desc2->LanguageCode, "deu", 3);
      p = (char*)desc2 + 7;
      *((byte*)p) = min(ExtEventTextLen-i*249, 249);
      memcpy(&p[1], &ExtEventText[i * 249], *(byte*)p);
      p += *(byte*)p + 1;
      desc2->DescrLength = (int)(p - (char*)desc2) - 2;
    }

/*      memcpy(p, "\x69\3\xF3\x62\x23", 5);  // Reserved Descriptor
    p += 5;
    memcpy(p, "\x5F\4\0\0\0\5", 6);  // Private Data Specifier Descriptor
    p += 6; */

//    if (StartDay && StartMonth)
    {
      struct tm timeinfo = Unix2TimeStruct(StartTimeUnix, FALSE);
      user = (tUserDesc*) p;
      user->DescrTag = DESC_UserDefined;
      user->DescrLength = 13;
      p += sizeof(tUserDesc);
      snprintf(p, 14, "%02hu:%02hu%02hu.%02hu#00", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
      p += 13;
    }

    evt->DescriptorLoopLen1 = ((p - (char*)desc) >> 8) & 0x0f;
    evt->DescriptorLoopLen2 = (p - (char*)desc) & 0xff;

    eit->SectionLen1 = ((p - (char*)eit +4 -3) >> 8) & 0x0f;
    eit->SectionLen2 = (p - (char*)eit +4 -3) & 0xff;

    *((dword*)p) = crc32m_tab((byte*)eit, (p - (char*)eit));
    p += 4;

    if (EPGBuffer)
    {
      free(EPGBuffer); EPGBuffer = NULL;
    }
    EPGBuffer = EITBuf;
    EPGLen = (int)(p - (char*)eit);
  }
  else if (EITBuf)
    free(EITBuf);
}


/* static void GenerateDummyPatPmt(byte *const PATPMTBuf)
{
  tTSPacket *Packet = (tTSPacket*) &PATPMTBuf[4];
  memset(PATPMTBuf, 0, 2*192);

  Packet->SyncByte        = 'G';
  Packet->PID1            = 0x1F;  // Filler Packet
  Packet->PID2            = 0xFF;

  Packet = (tTSPacket*) &PATPMTBuf[196];
  Packet->SyncByte        = 'G';
  Packet->PID1            = 0x1F;  // Filler Packet
  Packet->PID2            = 0xFF;
  Packet->ContinuityCount = 1;
} */
