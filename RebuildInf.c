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
#include "type.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "PESProcessor.h"
#include "RebuildInf.h"
#include "NavProcessor.h"
#include "TtxProcessor.h"

#ifdef _WIN32
  #define timezone _timezone
#else
  extern long timezone;
#endif


static inline byte BCD2BIN(byte BCD)
{
  return (BCD >> 4) * 10 + (BCD & 0x0f);
}

static inline dword Unix2TFTime(dword UnixTimeStamp)
{
  dword ret = (((int)(UnixTimeStamp / 86400) + 0x9e8b) << 16) | (((int)(UnixTimeStamp / 3600) % 24) << 8) | ((UnixTimeStamp / 60) % 60);
  return ret;
}

static inline time_t TF2UnixTime(dword TFTimeStamp)
{ 
  return ((TFTimeStamp >> 16) - 0x9e8b) * 86400 + ((TFTimeStamp >> 8 ) & 0xff) * 3600 + (TFTimeStamp & 0xff) * 60;
}

static dword AddTime(dword pvrDate, int addMinutes)  //add minutes to the day
{
  word                  day;
  short                 hour, min;

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
  return ((day<<16)|(hour<<8)|min);
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
static byte byte_reverse(byte b)
{
  b = (((b >> 1) & 0x55) | ((b << 1) & 0xaa));
  b = (((b >> 2) & 0x33) | ((b << 2) & 0xcc));
  b = (((b >> 4) & 0x0f) | ((b << 4) & 0xf0));
  return b;
}

static byte nibble_reverse(byte b)
{
  return byte_reverse(b << 4);
}

static byte hamming_decode(byte b)
{
  switch (b)
  {
    case 0xa8: return 0;
    case 0x0b: return 1;
    case 0x26: return 2;
    case 0x85: return 3;
    case 0x92: return 4;
    case 0x31: return 5;
    case 0x1c: return 6;
    case 0xbf: return 7;
    case 0x40: return 8;
    case 0xe3: return 9;
    case 0xce: return 10;
    case 0x6d: return 11;
    case 0x7a: return 12;
    case 0xd9: return 13;
    case 0xf4: return 14;
    case 0x57: return 15;
    default:
      return 0xFF;     // decode error , not yet corrected
  }
}


//------ RebuildINF
static void InitInfStruct(TYPE_RecHeader_TMSS *RecInf)
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
  strcpy(RecInf->ServiceInfo.ServiceName, "RecStrip");
  TRACEEXIT;
}

static bool AnalysePMT(byte *PSBuffer, TYPE_RecHeader_TMSS *RecInf)
{
  tTSPMT               *PMT = (tTSPMT*)PSBuffer;
  dword                 ElemPt;
  int                   SectionLength, ProgramInfoLength, ElemLength;
  word                  PID;
  char                  Log[512];
  bool                  VideoFound = FALSE;

  if(PMT->TableID != TABLE_PMT) return FALSE;

  TRACEENTER;

  //The following variables have a constant distance from the packet header
  SectionLength = PMT->SectionLen1 * 256 | PMT->SectionLen2;

  RecInf->ServiceInfo.ServiceID = PMT->ProgramNr1 * 256 | PMT->ProgramNr2;
  RecInf->ServiceInfo.PCRPID = PMT->PCRPID1 * 256 | PMT->PCRPID2;
  snprintf(Log, sizeof(Log), ", SID=%hu, PCRPID=%hu", RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID);

  ProgramInfoLength = PMT->ProgInfoLen1 * 256 | PMT->ProgInfoLen2;

  //Program info (-> �berspringen)
  ElemPt = sizeof(tTSPMT);
  SectionLength -= (sizeof(tTSPMT) - 3);  // SectionLength z�hlt ab Byte 3, bisherige Bytes abziehen

/*  while(ProgramInfoLength > 0)
  {
    tTSDesc* Desc = (tTSDesc*) &PSBuffer[ElemPt];
    ElemPt            += (Desc->DescrLength + 2);
    ProgramInfoLength -= (Desc->DescrLength + 2);
    SectionLength     -= (Desc->DescrLength + 2);
  } */

  // Einfacher:
  ElemPt += ProgramInfoLength;
  SectionLength -= ProgramInfoLength;

  //Loop through all elementary stream descriptors and search for the Audio and Video descriptor
  while (SectionLength > 4)         // 4 Bytes CRC am Ende
  {
    tElemStream* Elem = (tElemStream*) &PSBuffer[ElemPt];
    PID = Elem->ESPID1 * 256 | Elem->ESPID2;
    ElemLength = Elem->ESInfoLen1 * 256 | Elem->ESInfoLen2;

//    if (ElemLength > 0)  // Ist das n�tig??
    {
      switch (Elem->stream_type)
      {
        case STREAM_VIDEO_MPEG4_PART2:
        case STREAM_VIDEO_MPEG4_H263:
        case STREAM_VIDEO_MPEG4_H264:
        case STREAM_VIDEO_VC1:
        case STREAM_VIDEO_VC1SM:
          if(RecInf->ServiceInfo.VideoStreamType == 0xff)
            isHDVideo = TRUE;  // fortsetzen...
          // (fall-through!)
 
        case STREAM_VIDEO_MPEG1:
        case STREAM_VIDEO_MPEG2:
        {
          VideoFound = TRUE;
          RecInf->ServiceInfo.ServiceType = 0;  // SVC_TYPE_Tv
          if(RecInf->ServiceInfo.VideoStreamType == 0xff)
          {
            VideoPID = PID;
            RecInf->ServiceInfo.VideoPID = PID;
            RecInf->ServiceInfo.VideoStreamType = Elem->stream_type;
            ContinuityPIDs[0] = PID;
            snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), ", Stream=0x%x, VPID=%hu, HD=%d", RecInf->ServiceInfo.VideoStreamType, VideoPID, isHDVideo);
          }
          break;
        }

        case 0x06:  // Teletext!
        {
          dword DescrPt = ElemPt + sizeof(tElemStream);
          int DescrLength = ElemLength;

          while (DescrLength > 0)
          {
            tTSDesc* Desc = (tTSDesc*) &PSBuffer[DescrPt];

            if (Desc->DescrTag == DESC_Teletext)
            {
              TeletextPID = PID;
              PID = 0;
              snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), "\n  TS: TeletxtPID=%hu", TeletextPID);
              break;
            }
            else if (Desc->DescrTag == DESC_Subtitle)
            {
              // DVB-Subtitles
              snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), "\n  TS: SubtitlesPID=%hu", PID);
              PID = 0;
              break;
            }

            DescrPt     += (Desc->DescrLength + sizeof(tTSDesc));
            DescrLength -= (Desc->DescrLength + sizeof(tTSDesc));
          }
          if (PID == 0) break;
          // sonst fortsetzen mit Audio (fall-through)
        }

        //case STREAM_AUDIO_MP3:  //Ignored because it crashes with STREAM_VIDEO_MPEG1
        case STREAM_AUDIO_MPEG1:
        case STREAM_AUDIO_MPEG2:
        case STREAM_AUDIO_MPEG4_AAC:
        case STREAM_AUDIO_MPEG4_AAC_PLUS:
        case STREAM_AUDIO_MPEG4_AC3:
        case STREAM_AUDIO_MPEG4_DTS:
        {
          int k;
          if(RecInf->ServiceInfo.AudioStreamType == 0xff)
          {
            RecInf->ServiceInfo.AudioStreamType = Elem->stream_type;
            RecInf->ServiceInfo.AudioPID = PID;
            switch (RecInf->ServiceInfo.AudioStreamType)
            {
              case STREAM_AUDIO_MPEG1:
              case STREAM_AUDIO_MPEG2:
                RecInf->ServiceInfo.AudioTypeFlag = 0;
                break;
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

          if (NrContinuityPIDs < MAXCONTINUITYPIDS)
          {
            for (k = 1; k < NrContinuityPIDs; k++)
            {
              if (ContinuityPIDs[k] == PID)
                break;
            }
            if (k >= NrContinuityPIDs)
              ContinuityPIDs[NrContinuityPIDs++] = PID;
          }
          break;
        }
      }
    }
    SectionLength -= (ElemLength + sizeof(tElemStream));
    ElemPt += (ElemLength + sizeof(tElemStream));
  }

  if(NrContinuityPIDs < MAXCONTINUITYPIDS && TeletextPID != 0xffff)  ContinuityPIDs[NrContinuityPIDs++] = TeletextPID;
  if(NrContinuityPIDs < MAXCONTINUITYPIDS)                           ContinuityPIDs[NrContinuityPIDs++] = 0x12;
  printf("%s\n", Log);

//  isHDVideo = HDFound;

  TRACEEXIT;
  return VideoFound;
}

static bool AnalyseSDT(byte *PSBuffer, word ServiceID, TYPE_RecHeader_TMSS *RecInf)
{
  tTSSDT               *SDT = (tTSSDT*)PSBuffer;
  tTSService           *pService = NULL;
  tTSServiceDesc       *pServiceDesc = NULL;
  int                   SectionLength, p;

  TRACEENTER;

  if (SDT->TableID == TABLE_SDT)
  {
    SectionLength = SDT->SectionLen1 * 256 | SDT->SectionLen2;

    p = sizeof(tTSSDT);
    while (p + (int)sizeof(tTSService) <= SectionLength)
    {
      pService = (tTSService*) &PSBuffer[p];
      pServiceDesc = (tTSServiceDesc*) &PSBuffer[p + sizeof(tTSService)];
      
      if (pServiceDesc->DescrTag==DESC_Service)
      {
        if ((pService->ServiceID1 * 256 | pService->ServiceID2) == ServiceID)
        {
          memset(RecInf->ServiceInfo.ServiceName, 0, sizeof(RecInf->ServiceInfo.ServiceName));
          strncpy(RecInf->ServiceInfo.ServiceName, (&pServiceDesc->ProviderName + pServiceDesc->ProviderNameLen+1), min(((&pServiceDesc->ProviderNameLen)[pServiceDesc->ProviderNameLen+1]), sizeof(RecInf->ServiceInfo.ServiceName)-1));
printf("  TS: SvcName   = %s\n", RecInf->ServiceInfo.ServiceName);
          TRACEEXIT;
          return TRUE;
        }
      }
      p += sizeof(tTSService) + (pService->DescriptorsLen1 * 256 | pService->DescriptorsLen2);
    }
  }
  TRACEEXIT;
  return FALSE;
}

static bool AnalyseEIT(byte *Buffer, word ServiceID, TYPE_RecHeader_TMSS *RecInf)
{
  tTSEIT               *EIT = (tTSEIT*)Buffer;
  tEITEvent            *Event = NULL;
  tTSDesc              *Desc = NULL;
  tShortEvtDesc        *ShortDesc = NULL;
  tExtEvtDesc          *ExtDesc = NULL;
  int                   SectionLength, DescriptorLoopLen, p;
  word                  EventID;

  TRACEENTER;

  if ((EIT->TableID == TABLE_EIT) && ((EIT->ServiceID1 * 256 | EIT->ServiceID2) == ServiceID))
  {
    SectionLength = EIT->SectionLen1 * 256 | EIT->SectionLen2;
    SectionLength -= (sizeof(tTSEIT) - 3);
    p = sizeof(tTSEIT);
    while(SectionLength > 4)
    {
      Event = (tEITEvent*) &Buffer[p];
      EventID = Event->EventID1 * 256 | Event->EventID2;
      DescriptorLoopLen = Event->DescriptorLoopLen1 * 256 | Event->DescriptorLoopLen2;

      SectionLength -= sizeof(tEITEvent);
      p += sizeof(tEITEvent);
      if(Event->RunningStatus == 4) // *CW-debug* richtig: 4
      {
        //Found the event we were looking for
        while(DescriptorLoopLen > 0)
        {
          Desc = (tTSDesc*) &Buffer[p];
          
          if(Desc->DescrTag == DESC_EITShortEvent)
          {
            // Short Event Descriptor
            byte        NameLen, TextLen;
            time_t      StartTimeUnix;
            ShortDesc = (tShortEvtDesc*) Desc;

            RecInf->EventInfo.ServiceID = ServiceID;
            RecInf->EventInfo.EventID = EventID;
            RecInf->EventInfo.RunningStatus = Event->RunningStatus;
            RecInf->EventInfo.StartTime = (Event->StartTime[0] << 24) | (Event->StartTime[1] << 16) | (BCD2BIN(Event->StartTime[2]) << 8) | BCD2BIN(Event->StartTime[3]);
            RecInf->EventInfo.DurationHour = BCD2BIN(Event->DurationSec[0]);
            RecInf->EventInfo.DurationMin = BCD2BIN(Event->DurationSec[1]);
            RecInf->EventInfo.EndTime = AddTime(RecInf->EventInfo.StartTime, RecInf->EventInfo.DurationHour * 60 + RecInf->EventInfo.DurationMin);
//            StartTimeUnix = 86400*((RecInf->EventInfo.StartTime>>16) - 40587) + 3600*BCD2BIN(Event->StartTime[2]) + 60*BCD2BIN(Event->StartTime[3]);
            StartTimeUnix = TF2UnixTime(RecInf->EventInfo.StartTime);
printf("  TS: EvtStart  = %s", ctime(&StartTimeUnix));

            NameLen = ShortDesc->EvtNameLen;
            TextLen = Buffer[p + sizeof(tShortEvtDesc) + NameLen];
            RecInf->EventInfo.EventNameLength = NameLen;
            strncpy(RecInf->EventInfo.EventNameDescription, (char*)&Buffer[p + sizeof(tShortEvtDesc)], NameLen);
printf("  TS: EventName = %s\n", RecInf->EventInfo.EventNameDescription);

            strncpy(&RecInf->EventInfo.EventNameDescription[NameLen], (char*)&Buffer[p + sizeof(tShortEvtDesc) + NameLen+1], TextLen);
printf("  TS: EventDesc = %s\n", &RecInf->EventInfo.EventNameDescription[NameLen]);

//            StrMkUTF8(RecInf.RecInfEventInfo.EventNameAndDescription, 9);
          }

          else if(Desc->DescrTag == DESC_EITExtEvent)
          {
            // Extended Event Descriptor
            ExtDesc = (tExtEvtDesc*) Desc;
            RecInf->ExtEventInfo.ServiceID = ServiceID;
//            RecInf->ExtEventInfo.EventID = EventID;
            if ((RecInf->ExtEventInfo.TextLength > 0) && (ExtDesc->ItemDesc < 0x20))
            {
              strncpy(&RecInf->ExtEventInfo.Text[RecInf->ExtEventInfo.TextLength], (&ExtDesc->ItemDesc) + 1, ExtDesc->ItemDescLen - 1);
              RecInf->ExtEventInfo.TextLength += ExtDesc->ItemDescLen - 1;
            }
            else
            {
              strncpy(&RecInf->ExtEventInfo.Text[RecInf->ExtEventInfo.TextLength], &ExtDesc->ItemDesc, ExtDesc->ItemDescLen);
              RecInf->ExtEventInfo.TextLength += ExtDesc->ItemDescLen;
            }
printf("  TS: ExtEvent  = %s\n", RecInf->ExtEventInfo.Text);
          }

          SectionLength -= (Desc->DescrLength + sizeof(tTSDesc));
          DescriptorLoopLen -= (Desc->DescrLength + sizeof(tTSDesc));
          p += (Desc->DescrLength + sizeof(tTSDesc));
        }

        TRACEEXIT;
        return TRUE;
      }
      else
      {
        //Not this one
        SectionLength -= DescriptorLoopLen;
        p += DescriptorLoopLen;
      }
    }
  }
  TRACEEXIT;
  return FALSE;
}

static bool AnalyseTtx(byte *PSBuffer, dword *TtxTime, char *ServiceName)
{
  char                  programme[50];
  int                   PESLength = 0, p = 0;
  int                   magazin, row;
  byte                  b1, b2;
  dword                 pvrTime = 0;
  byte                 *data_block = NULL;

  TRACEENTER;

  if (PSBuffer[0]==0 && PSBuffer[1]==0 && PSBuffer[2]==1)
  {
    p = 6;
    PESLength = PSBuffer[4] * 256 + PSBuffer[5];
    if ((PSBuffer[p] & 0xf0) == 0x80)  // Additional header
      p += PSBuffer[8] + 3;
  }

  if (PSBuffer[p] == 0x10)
  {
    p++;
    while (p < PESLength - 46)
    {
      if (PSBuffer[p] == 0x02 && PSBuffer[p+1] == 44)
      {
        b1 = PSBuffer[p+4];
        b2 = PSBuffer[p+5];
        data_block = &PSBuffer[p+6];

        row = byte_reverse(((hamming_decode(b1) & 0x0f) << 4) | (hamming_decode(b2) & 0x0f));
        magazin = row & 7;
        if (magazin == 0) magazin = 8;
        row = row >> 3;

        if (magazin == 8 && row == 30 && data_block[1] == 0xA8)
        {
          byte dc, packet_format;
          int i;
          dc = hamming_decode(data_block[0]);

          switch (dc & 0x0e)
          {
            case 0: packet_format = 1; break;
            case 4: packet_format = 2; break;
            default: packet_format = 0; break;
          }

          if (packet_format == 1)
          {
            // get initial page
/*            byte initialPageUnits = nibble_reverse(hamming_decode(data_block[1]));
            byte initialPageTens  = nibble_reverse(hamming_decode(data_block[2]));
            //byte initialPageSub1  = nibble_reverse(hammingDecode(data_block[3]));
            byte initialPageSub2  = nibble_reverse(hamming_decode(data_block[4]));
            //byte initialPageSub3  = nibble_reverse(hamming_decode(data_block[5]));
            byte initialPageSub4  = nibble_reverse(hamming_decode(data_block[6]));
            dword InitialPage = initialPageUnits + 10 * initialPageTens + 100 * (((initialPageSub2 >> 3) & 1) + ((initialPageSub4 >> 1) & 6));
*/
            // offset in half hours
            byte timeOffsetCode   = byte_reverse(data_block[9]);
            byte timeOffsetH2     = ((timeOffsetCode >> 1) & 0x1F);

            // get current time
            byte mjd1             = byte_reverse(data_block[10]);
            byte mjd2             = byte_reverse(data_block[11]);
            byte mjd3             = byte_reverse(data_block[12]);
            dword mdj = ((mjd1 & 0x0F)-1)*10000 + ((mjd2 >> 4)-1)*1000 + ((mjd2 & 0x0F)-1)*100 + ((mjd3 >> 4)-1)*10 + ((mjd3 & 0x0F)-1);

            byte utc1             = byte_reverse(data_block[13]);
            byte utc2             = byte_reverse(data_block[14]);
            byte utc3             = byte_reverse(data_block[15]);

            int localH = 10 * ((utc1 >> 4) - 1) + ((utc1 & 0x0f) - 1);
            int localM = 10 * ((utc2 >> 4) - 1) + ((utc2 & 0x0f) - 1);
            int localS = 10 * ((utc3 >> 4) - 1) + ((utc3 & 0x0f) - 1);

            pvrTime = (mdj & 0xffff) << 16 | localH << 8 | localM;

            if ((timeOffsetCode & 0x40) == 0)
              pvrTime = AddTime(pvrTime, timeOffsetH2 * 30);
            else
              pvrTime = AddTime(pvrTime, -timeOffsetH2 * 30);

            if(TtxTime) *TtxTime = pvrTime;

            if ((timeOffsetCode & 0x40) == 0)
            {
              // positive offset polarity
              localM += (timeOffsetH2 * 30);
              localH += (localM / 60);
              mdj    += (localH / 24);
              localM  = localM % 60;
              localH  = localH % 24;
            }
            else
            {
              // negative offset polarity
              localM -= (timeOffsetH2 * 30);
              while (localM < 0) 
              {
                localM += 60;
                localH--;
              }
              while (localH < 0)
              {
                localH += 24;
                mdj--;
              }
            }

            // Programme Identification
            programme[0] = '\0';
            for (i = 20; i < 40; i++)
            {
              char u[4] = { 0, 0, 0, 0 };
              word c = telx_to_ucs2(byte_reverse(data_block[i]));
              // strip any control codes from PID, eg. TVP station
              if (c < 0x20) continue;
              ucs2_to_utf8(u, c);
              strcat(programme, u);
            }
            rtrim(programme);
            if(ServiceName) strcpy(ServiceName, programme);
printf("  TS: Teletext Programme Identification Data: '%s'\n", programme);
printf("  TS: Teletext date: mdj=%u, %02hhu:%02hhu:%02hhu\n", mdj, localH, localM, localS);
            
            TRACEEXIT;
            return TRUE;
          }
        }
      }
      p += max(PSBuffer[p+1] + 2, 1);
    }
  }

  TRACEEXIT;
  return FALSE;
}

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf)
{
  tPSBuffer             PMTBuffer, EITBuffer, TtxBuffer;
  byte                 *Buffer = NULL;
  int                   LastBuffer = 0, LastTtxBuffer = 0;
  word                  PMTPID = 0;
  dword                 FileTimeStamp, TtxTime = 0;
  dword                 FirstPCR = 0, LastPCR = 0, TtxPCR = 0, dPCR = 0;
  int                   ReadPackets, Offset, FirstOffset;
  bool                  EITOK = FALSE, SDTOK = FALSE, TtxFound = FALSE, TtxOK = FALSE;
  time_t                StartTimeUnix;
  byte                 *p;
  long long             FilePos = 0;
  int                   i, j;
  bool                  ret = TRUE;

  const byte            ANDMask[6] = {0xFF, 0xC0, 0x00, 0xD0, 0xFF, 0xFF};
  const byte            PMTMask[6] = {0x47, 0x40, 0x00, 0x10, 0x00, 0x02};

  TRACEENTER;
  Buffer = (byte*) malloc(RECBUFFERENTRIES * PACKETSIZE);
  if (!Buffer)
  {
    printf("  Failed to allocate the buffer.\n");
    TRACEEXIT;
    return FALSE;
  }

  InitInfStruct(RecInf);

  //Get the time stamp of the .rec. We assume that this is the time when the recording has finished
  {
    struct stat64 statbuf;
    fstat64(fileno(fIn), &statbuf);
    FileTimeStamp = Unix2TFTime((dword) statbuf.st_mtime);
  }

  // Read the first RECBUFFERENTRIES TS packets
//  FilePos = ftello64(fIn);
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
  if(ReadPackets != RECBUFFERENTRIES)
  {
    printf("  Failed to read the first %d TS packets.\n", RECBUFFERENTRIES);
    free(Buffer);
    TRACEEXIT;
    return FALSE;
  }

  FirstOffset = FindNextPacketStart(Buffer, ReadPackets*PACKETSIZE);
  Offset = FirstOffset;
  if (Offset >= 0)
  {
    p = &Buffer[Offset];
    for(i = Offset; i < ReadPackets*PACKETSIZE - 14; i+=PACKETSIZE)
    {
      if ((p[PACKETOFFSET] != 'G') && (i < ReadPackets*PACKETSIZE-5573))
      {
        Offset = FindNextPacketStart(p, ReadPackets*PACKETSIZE - i);
        if(Offset >= 0)
          { p += Offset;  i += Offset; }
        else break;
      }
      //Find the first PCR (for duration calculation)
      if (GetPCRms(&p[PACKETOFFSET], &FirstPCR) && FirstPCR != 0)
        break;
      p += PACKETSIZE;
    }
  }


  // Springe in die Mitte der Aufnahme
  fseeko64(fIn, FilePos + ((RecFileSize/2)/PACKETSIZE * PACKETSIZE), SEEK_SET);

  //Read RECBUFFERENTRIES TS pakets for analysis
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
  if(ReadPackets < RECBUFFERENTRIES)
  {
    printf("  Failed to read %d TS packets from the middle.\n", RECBUFFERENTRIES);
    free(Buffer);
    TRACEEXIT;
    return FALSE;
  }

  Offset = FindNextPacketStart(Buffer, ReadPackets*PACKETSIZE);
  if (Offset >= 0)
  {
    //Find a PMT packet to get its PID
    p = &Buffer[Offset + PACKETOFFSET];

    for(i = Offset; i < ReadPackets*PACKETSIZE; i+=PACKETSIZE)
    {
      bool ok = TRUE;

      for(j = 0; j < 6; j++)
        if((p[j] & ANDMask[j]) != PMTMask[j])
        {
          ok = FALSE;
          break;
        }

      if(ok)
      {
        PMTPID = ((p[1] << 8) | p[2]) & 0x1fff;
        break;
      }

      p += PACKETSIZE;
    }

    if(PMTPID)
    {
      RecInf->ServiceInfo.PMTPID = PMTPID;
      printf("  TS: PMTPID=%hu", PMTPID);

      //Analyse the PMT
      PSBuffer_Init(&PMTBuffer, PMTPID, 16384, TRUE);

//    p = &Buffer[Offset + PACKETOFFSET];
      for(i = p-Buffer; i < ReadPackets*PACKETSIZE; i+=PACKETSIZE)
      {
        PSBuffer_ProcessTSPacket(&PMTBuffer, (tTSPacket*)p);
        if(PMTBuffer.ValidBuffer != 0)
          break;
        p += PACKETSIZE;
      }

      AnalysePMT(PMTBuffer.Buffer1, /*PMTBuffer.ValidBufLen,*/ RecInf);
      PSBuffer_Reset(&PMTBuffer);
    }
    else
    {
      printf("  Failed to locate a PMT packet.\n");
      ret = FALSE;
    }

    //If we're here, it should be possible to find the associated EPG event
    if (RecInf->ServiceInfo.ServiceID)
    {
      PSBuffer_Init(&PMTBuffer, 0x0011, 16384, TRUE);
      PSBuffer_Init(&EITBuffer, 0x0012, 16384, TRUE);
      if (TeletextPID != 0xffff)
        PSBuffer_Init(&TtxBuffer, TeletextPID, 16384, FALSE);
      LastBuffer = 0; LastTtxBuffer = 0;

      fseeko64(fIn, FilePos + ((RecFileSize/2)/PACKETSIZE * PACKETSIZE) + Offset, SEEK_SET);
      for(j = 0; j < 10; j++)
      {
        ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
        p = &Buffer[PACKETOFFSET];

        for(i = 0; i < ReadPackets; i++)
        {
          if (!SDTOK)
          {
            PSBuffer_ProcessTSPacket(&PMTBuffer, (tTSPacket*)p);
            if(PMTBuffer.ValidBuffer)
            {
              byte* pBuffer = (PMTBuffer.ValidBuffer==1) ? PMTBuffer.Buffer1 : PMTBuffer.Buffer2;
              SDTOK = AnalyseSDT(pBuffer, RecInf->ServiceInfo.ServiceID, RecInf);
            }
          }
          if (!EITOK)
          {
            PSBuffer_ProcessTSPacket(&EITBuffer, (tTSPacket*)p);
            if(EITBuffer.ValidBuffer != LastBuffer)
            {
              byte *pBuffer = (EITBuffer.ValidBuffer==1) ? EITBuffer.Buffer1 : EITBuffer.Buffer2;
              EITOK = AnalyseEIT(pBuffer, RecInf->ServiceInfo.ServiceID, RecInf);
              LastBuffer = EITBuffer.ValidBuffer;
            }
          }
          if (TeletextPID != 0xffff && !TtxOK)
          {
            if (!TtxFound)
            {
              PSBuffer_ProcessTSPacket(&TtxBuffer, (tTSPacket*)p);
              if(TtxBuffer.ValidBuffer != LastTtxBuffer)
              {
                byte *pBuffer = (TtxBuffer.ValidBuffer==1) ? TtxBuffer.Buffer1 : TtxBuffer.Buffer2;
                TtxFound = AnalyseTtx(pBuffer, &TtxTime, (RecInf->ServiceInfo.ServiceName[0] ? NULL : RecInf->ServiceInfo.ServiceName));
                LastTtxBuffer = TtxBuffer.ValidBuffer;
              }
            }
          }
          if(TtxFound && !TtxOK)
            TtxOK = (GetPCRms(p, &TtxPCR) && TtxPCR != 0);

          if(EITOK && SDTOK && (TtxOK || TeletextPID == 0xffff)) break;
          p += PACKETSIZE;
        }
        if(EITOK && SDTOK && (TtxOK || TeletextPID == 0xffff)) break;
      }
      if(!SDTOK)
        printf ("  Failed to get service name from SDT.\n");
      if(!EITOK)
        printf ("  Failed to get the EIT information.\n");
      if(TeletextPID != 0xffff && !TtxOK)
        printf ("  Failed to get start time from Teletext.\n");
      PSBuffer_Reset(&PMTBuffer);
      PSBuffer_Reset(&EITBuffer);
      if(TeletextPID != 0xffff)
        PSBuffer_Reset(&TtxBuffer);
    }
  }


  //Read the last RECBUFFERENTRIES TS pakets
  fseeko64(fIn, FilePos + ((((RecFileSize-FilePos)/PACKETSIZE) - RECBUFFERENTRIES) * PACKETSIZE), SEEK_SET);
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
//  fseeko64(fIn, FilePos, SEEK_SET);
  if(ReadPackets != RECBUFFERENTRIES)
  {
    printf ("  Failed to read the last %d TS packets.\n", RECBUFFERENTRIES);
    free(Buffer);
    TRACEEXIT;
    return FALSE;
  }

  Offset = FindNextPacketStart(Buffer, ReadPackets*PACKETSIZE);
  if (Offset >= 0)
  {
    p = &Buffer[Offset];
    for(i = Offset; i < ReadPackets*PACKETSIZE - 14; i+=PACKETSIZE)
    {
      if ((p[PACKETOFFSET] != 'G') && (i < ReadPackets*PACKETSIZE-5573))
      {
        Offset = FindNextPacketStart(p, ReadPackets*PACKETSIZE - i);
        if(Offset >= 0)
          { p += Offset;  i += Offset; }
        else break;
      }
      //Find the last PCR
      GetPCRms(&p[PACKETOFFSET], &LastPCR);
      p += PACKETSIZE;
    }
  }

  if(!FirstPCR || !LastPCR)
  {
    printf("  Duration calculation failed (missing PCR). Using 120 minutes.\n");
    RecInf->RecHeaderInfo.DurationMin = 120;
    RecInf->RecHeaderInfo.DurationSec = 0;
  }
  else
  {
    dPCR = DeltaPCR(FirstPCR, LastPCR);
    RecInf->RecHeaderInfo.DurationMin = (int)(dPCR / 60000);
    RecInf->RecHeaderInfo.DurationSec = (dPCR / 1000) % 60;
  }
printf("  TS: Duration  = %2.2d min %2.2d sec\n", RecInf->RecHeaderInfo.DurationMin, RecInf->RecHeaderInfo.DurationSec);

  if(TtxTime && TtxPCR)
  {
    dPCR = DeltaPCR(FirstPCR, TtxPCR);
    RecInf->RecHeaderInfo.StartTime = AddTime(TtxTime, -1 * (int)((dPCR/1000+59)/60));
  }
  else
  {
    tzset();
    RecInf->RecHeaderInfo.StartTime = AddTime(RecInf->EventInfo.StartTime, -1*timezone/60);  // GMT+1
    if (!RecInf->EventInfo.StartTime || ((FileTimeStamp >> 16) - (RecInf->EventInfo.StartTime >> 16) <= 1))
      RecInf->RecHeaderInfo.StartTime = AddTime(FileTimeStamp, -1 * (int)RecInf->RecHeaderInfo.DurationMin);
  }
  StartTimeUnix = TF2UnixTime(RecInf->RecHeaderInfo.StartTime) - 3600;
  printf("  TS: StartTime = %s", (ctime(&StartTimeUnix)));

  free(Buffer);
  TRACEEXIT;
  return ret;
}
