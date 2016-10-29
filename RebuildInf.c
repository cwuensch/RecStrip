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
#include "RecStrip.h"
#include "RecHeader.h"
#include "RebuildInf.h"
#include "NavProcessor.h"


static inline byte BCD2BIN(byte BCD)
{
  return (BCD >> 4) * 10 + (BCD & 0x0f);
}

static inline dword Unix2TFTime(dword UnixTimeStamp)
{
  dword ret = (((int)(UnixTimeStamp / 86400) + 0x9e8b) << 16) | (((int)(UnixTimeStamp / 3600) % 24) << 8) | ((UnixTimeStamp / 60) % 60);
  return ret;
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


//------ TS to PS converter
static void PSBuffer_Reset(tPSBuffer *PSBuffer)
{
  TRACEENTER;
  if(PSBuffer)
  {
    if(PSBuffer->Buffer1)
    {
      free(PSBuffer->Buffer1);
      PSBuffer->Buffer1 = NULL;
    }

    if(PSBuffer->Buffer2)
    {
      free(PSBuffer->Buffer2);
      PSBuffer->Buffer2 = NULL;
    }

    memset(PSBuffer, 0, sizeof(tPSBuffer));
  }
  TRACEEXIT;
}

static void PSBuffer_Init(tPSBuffer *PSBuffer, word PID, int BufferSize)
{
  TRACEENTER;

  memset(PSBuffer, 0, sizeof(tPSBuffer));
  PSBuffer->PID = PID;
  PSBuffer->BufferSize = BufferSize;

  PSBuffer->Buffer1 = (byte*) malloc(BufferSize);
  PSBuffer->Buffer2 = (byte*) malloc(BufferSize);
  PSBuffer->pBuffer = &PSBuffer->Buffer1[0];
  PSBuffer->LastCCCounter = 255;

  TRACEEXIT;
}

static void PSBuffer_ProcessTSPacket(tPSBuffer *PSBuffer, byte *TSBuffer)
{
  word                  PID;
  byte                  RemainingBytes;
  
  TRACEENTER;

  //Adaptation field gibt es nur bei PES Paketen

  //Stimmt die PID?
  PID = ((TSBuffer[1] & 0x1f) << 8) | TSBuffer[2];
  if(PID == PSBuffer->PID)
  {
    if(PSBuffer->BufferPtr >= PSBuffer->BufferSize)
    {
      if((PSBuffer->ErrorFlag & 0x01) == 0)
      {
        printf("  PS buffer overflow while parsing PID 0x%4.4x\n", PSBuffer->PID);
        PSBuffer->ErrorFlag |= 1;
      }
    }
    else
    {
      //Continuity Counter ok? Falls nicht Buffer komplett verwerfen
      if((PSBuffer->LastCCCounter = 255) || ((TSBuffer[3] & 0x0f) == ((PSBuffer->LastCCCounter + 1) & 0x0f)))
      {
        //Startet ein neues PES-Paket?
        if((TSBuffer[1] & 0x40) != 0)
        {
          RemainingBytes = TSBuffer[4];
          if(PSBuffer->BufferPtr != 0)
          {
            //Restliche Bytes umkopieren und neuen Buffer beginnen
            if(RemainingBytes != 0)
            {
              memcpy(PSBuffer->pBuffer, &TSBuffer[5], RemainingBytes);
              PSBuffer->BufferPtr += RemainingBytes;
            }

            //Puffer mit den abfragbaren Daten markieren
            switch(PSBuffer->ValidBuffer)
            {
              case 0:
              case 2: PSBuffer->ValidBuffer = 1; break;
              case 1: PSBuffer->ValidBuffer = 2; break;
            }

            PSBuffer->PSFileCtr++;
          }

          //Neuen Puffer aktivieren
          switch(PSBuffer->ValidBuffer)
          {
            case 0:
            case 2: PSBuffer->pBuffer = PSBuffer->Buffer1; break;
            case 1: PSBuffer->pBuffer = PSBuffer->Buffer2; break;
          }
          PSBuffer->BufferPtr = 0;

          //Erste Daten kopieren
          memset(PSBuffer->pBuffer, 0, PSBuffer->BufferSize);
          memcpy(PSBuffer->pBuffer, &TSBuffer[5 + RemainingBytes], 183 - RemainingBytes);
          PSBuffer->pBuffer += (183 - RemainingBytes);
          PSBuffer->BufferPtr += (183 - RemainingBytes);
        }
        else
        {
          //Weiterkopieren
          if(PSBuffer->BufferPtr != 0)
          {
            memcpy(PSBuffer->pBuffer, &TSBuffer[4], 184);
            PSBuffer->pBuffer += 184;
            PSBuffer->BufferPtr += 184;
          }
        }
      }
      else
      {
        //Unerwarteter Continuity Counter, Daten verwerfen
        if(PSBuffer->LastCCCounter == 255)
        {
          printf("  CC error while parsing PID 0x%4.4x\n", PSBuffer->PID);
          switch(PSBuffer->ValidBuffer)
          {
            case 0:
            case 2: PSBuffer->pBuffer = PSBuffer->Buffer1; break;
            case 1: PSBuffer->pBuffer = PSBuffer->Buffer2; break;
          }
          memset(PSBuffer->pBuffer, 0, PSBuffer->BufferSize);
          PSBuffer->BufferPtr = 0;
        }
      }

      PSBuffer->LastCCCounter = TSBuffer[3] & 0x0f;
    }
  }
  TRACEEXIT;
}


//------ RebuildINF
static void InitInfStruct(TYPE_RecHeader_TMSS *RecInf)
{
  TRACEENTER;
  memset(RecInf, 0, sizeof(TYPE_RecHeader_TMSS));

  RecInf->RecHeaderInfo.Magic[0]     = 'T';
  RecInf->RecHeaderInfo.Magic[1]     = 'F';
  RecInf->RecHeaderInfo.Magic[2]     = 'r';
  RecInf->RecHeaderInfo.Magic[3]     = 'c';
  RecInf->RecHeaderInfo.Version      = 0x8000;
  RecInf->RecHeaderInfo.CryptFlag    = 0;
  RecInf->RecHeaderInfo.FlagUnknown  = 1;
  RecInf->ServiceInfo.ServiceType    = 1;  // SVC_TYPE_Radio
  RecInf->ServiceInfo.SatIndex       = 1;
  RecInf->ServiceInfo.FlagTuner      = 3;
  strcpy(RecInf->ServiceInfo.ServiceName, "RecStrip");
  TRACEEXIT;
}

static bool AnalysePMT(byte *PSBuffer, TYPE_RecHeader_TMSS *RecInf)
{
  dword                 DescrPt;
  short                 SectionLength, DescriptorLength, ProgramInfoLength;  // DescriptorType
  word                  PID;
  char                  Log[512];
  bool                  VideoFound = FALSE;

  TRACEENTER;

  //The following variables have a constant distance from the packet header
  SectionLength = (((PSBuffer[0x01] << 8) | PSBuffer[0x02]) & 0xfff) - 7;

  RecInf->ServiceInfo.ServiceID = ((PSBuffer[0x03] << 8) | PSBuffer[0x04]);
  RecInf->ServiceInfo.PCRPID = ((PSBuffer[0x08] << 8) | PSBuffer [0x09]) & 0x1fff;
  snprintf(Log, sizeof(Log), ", SID=0x%4.4x, PCRPID=0x%4.4x", RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID);

  ProgramInfoLength = ((PSBuffer[0x0a] << 8) | PSBuffer[0x0b]) & 0xfff;

  //Program info needed to decode all ECMs
  DescrPt = 0x0c;

  while(ProgramInfoLength > 0)
  {
//    DescriptorType   = PSBuffer[DescrPt];
    DescriptorLength = PSBuffer[DescrPt + 1];
    DescrPt           += (DescriptorLength + 2);
    SectionLength     -= (DescriptorLength + 2);
    ProgramInfoLength -= (DescriptorLength + 2);
  }

  //Loop through all elementary stream descriptors and search for the Audio and Video descriptor
  while (SectionLength > 0)
  {
    PID = ((PSBuffer [DescrPt + 1] << 8) | PSBuffer [DescrPt + 2]) & 0x1fff;
    DescriptorLength = ((PSBuffer[DescrPt + 3] << 8) | PSBuffer [DescrPt + 4]) & 0xfff;

    switch (PSBuffer[DescrPt])
    {
      case STREAM_VIDEO_MPEG4_PART2:
      case STREAM_VIDEO_MPEG4_H263:
      case STREAM_VIDEO_MPEG4_H264:
      case STREAM_VIDEO_VC1:
      case STREAM_VIDEO_VC1SM:
        isHDVideo = TRUE;  // fortsetzen...
 
      case STREAM_VIDEO_MPEG1:
      case STREAM_VIDEO_MPEG2:
      {
        VideoFound = TRUE;
        RecInf->ServiceInfo.ServiceType = 0;  // SVC_TYPE_Tv
        if(RecInf->ServiceInfo.VideoStreamType == 0)
        {
          VideoPID = PID;
          RecInf->ServiceInfo.VideoPID = PID;
          RecInf->ServiceInfo.VideoStreamType = PSBuffer[DescrPt];
          snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), ", Stream=0x%x, VPID=0x%4.4x, HD=%d", RecInf->ServiceInfo.VideoStreamType, VideoPID, isHDVideo);
        }
        break;
      }

      case STREAM_AUDIO_MPEG4_AC3_PLUS:  // Teletext?
      {
        int i;
        for (i = 0; i < DescriptorLength-1; i++)
          if ((PSBuffer[DescrPt+5+i] == 'V') && (PSBuffer[DescrPt+5+i+1] == '\x05'))
          {
            TeletextPID = PID;
            PID = 0;
            snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), "\n  TS: TeletxtPID=0x%x", TeletextPID);
            break;
          }
        if (PID == 0) break;
        // sonst fortsetzen mit Audio
      }

      //case STREAM_AUDIO_MP3:  //Ignored because it crashes with STREAM_VIDEO_MPEG1
      case STREAM_AUDIO_MPEG1:
      case STREAM_AUDIO_MPEG2:
      case STREAM_AUDIO_MPEG4_AAC:
      case STREAM_AUDIO_MPEG4_AAC_PLUS:
      case STREAM_AUDIO_MPEG4_AC3:
      case STREAM_AUDIO_MPEG4_DTS:
      {
        if(RecInf->ServiceInfo.AudioStreamType == 0)
        {
          RecInf->ServiceInfo.AudioStreamType = PSBuffer[DescrPt];
          RecInf->ServiceInfo.AudioPID = PID;
        }
        break;
      }
    }
    SectionLength -= (DescriptorLength + 5);
    DescrPt += (DescriptorLength + 5);
  }

  printf("%s\n", Log);
//  isHDVideo = HDFound;

  TRACEEXIT;
  return VideoFound;
}

static bool AnalyseEIT(byte *Buffer, word ServiceID, TYPE_RecHeader_TMSS *RecInf)
{
  word                  SectionLength, p;
  byte                  RunningStatus;
  word                  DescriptorLoopLen;
  byte                  Descriptor;
  byte                  DescriptorLen;
  byte                  StartTimeUTC[5];
  time_t                StartTimeUnix = 0;
  byte                  Duration[3];
  word                  EventID;

  TRACEENTER;

  if((Buffer[0] == 0x4e) && (((Buffer[3] << 8) | Buffer[4]) == ServiceID))
  {
    SectionLength = ((Buffer[1] << 8) | Buffer[2]) & 0x0fff;
    SectionLength -= 11;
    p = 14;
    while(SectionLength > 4)
    {
      EventID = (Buffer[p] << 8) | Buffer[p + 1];
      memcpy(&StartTimeUTC, &Buffer[p + 2], 5);
      memcpy(&Duration, &Buffer[p + 7], 3);
      RunningStatus = Buffer[p + 10] >> 5;
      DescriptorLoopLen = ((Buffer[p + 10] << 8) | Buffer[p + 11]) & 0x0fff;

      SectionLength -= 12;
      p += 12;
      if(RunningStatus == 4) // *CW-debug* richtig: 4
      {
        //Found the event we were looking for
        while(DescriptorLoopLen > 0)
        {
          Descriptor = Buffer[p];
          DescriptorLen = Buffer[p + 1];

          if(Descriptor == 0x4d)
          {
            byte        NameLen, TextLen;

//            RecInf->EventInfo.ServiceID = ServiceID;
            RecInf->EventInfo.EventID = EventID;
            RecInf->EventInfo.RunningStatus = RunningStatus;
            RecInf->EventInfo.StartTime = (StartTimeUTC[0] << 24) | (StartTimeUTC[1] << 16) | (BCD2BIN(StartTimeUTC[2]) << 8) | BCD2BIN(StartTimeUTC[3]);
            RecInf->EventInfo.DurationHour = BCD2BIN(Duration[0]);
            RecInf->EventInfo.DurationMin = BCD2BIN(Duration[1]);
            RecInf->EventInfo.EndTime = AddTime(RecInf->EventInfo.StartTime, RecInf->EventInfo.DurationHour * 60 + RecInf->EventInfo.DurationMin);
            StartTimeUnix = 86400*((RecInf->EventInfo.StartTime>>16) - 40587) + 3600*BCD2BIN(StartTimeUTC[2]) + 60*BCD2BIN(StartTimeUTC[3]);
printf("  TS: StartTime = %s", ctime(&StartTimeUnix));

            NameLen = Buffer[p + 5];
            TextLen = Buffer[p + 5 + NameLen+1];
            RecInf->EventInfo.EventNameLength = NameLen;
            strncpy(RecInf->EventInfo.EventNameDescription, &Buffer[p + 6], NameLen);
printf("  TS: EventName = %s\n", RecInf->EventInfo.EventNameDescription);

            strncpy(&RecInf->EventInfo.EventNameDescription[NameLen], &Buffer[p + 6 + NameLen+1], TextLen);
printf("  TS: EventDesc = %s\n", &RecInf->EventInfo.EventNameDescription[NameLen]);

//            StrMkUTF8(RecInf.RecInfEventInfo.EventNameAndDescription, 9);
          }

          else if(Descriptor == 0x4e)
          {
            RecInf->ExtEventInfo.ServiceID = ServiceID;
            RecInf->ExtEventInfo.EventID = EventID;
            if ((RecInf->ExtEventInfo.TextLength > 0) && (Buffer[p+8] < 0x20))
            {
              strncpy(&RecInf->ExtEventInfo.Text[RecInf->ExtEventInfo.TextLength], &Buffer[p+8+1], Buffer[p+7] - 1);
              RecInf->ExtEventInfo.TextLength += Buffer[p+7] - 1;
            }
            else
            {
              strncpy(&RecInf->ExtEventInfo.Text[RecInf->ExtEventInfo.TextLength], &Buffer[p+8], Buffer[p+7]);
              RecInf->ExtEventInfo.TextLength += Buffer[p+7];
            }
printf("  TS:  ExtEvent = %s\n", RecInf->ExtEventInfo.Text);
          }

          SectionLength -= (DescriptorLen + 2);
          DescriptorLoopLen -= (DescriptorLen + 2);
          p += (DescriptorLen + 2);
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

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf)
{
  tPSBuffer             PMTBuffer;
  tPSBuffer             EITBuffer;
  byte                 *Buffer = NULL;
  int                   LastBuffer = 0;
  word                  PMTPID = 0;
  dword                 FileTimeStamp;
  dword                 FirstPCR = 0, LastPCR = 0, dPCR = 0;
  int                   ReadPackets, Offset;
  bool                  EITOK;
  byte                 *p;
  long long             FilePos = 0;
  int                   i, j;
  bool                  ret = FALSE;

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
  FilePos = ftello64(fIn);
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
  if(ReadPackets != RECBUFFERENTRIES)
  {
    printf("  Failed to read the first %d TS packets.\n", RECBUFFERENTRIES);
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
      printf("  TS: PMTPID=0x%4.4x", PMTPID);

      //Analyse the PMT
      PSBuffer_Init(&PMTBuffer, PMTPID, 16384);

//    p = &Buffer[Offset + PACKETOFFSET];
      for(i = p-Buffer; i < ReadPackets*PACKETSIZE; i+=PACKETSIZE)
      {
        if(PMTBuffer.ValidBuffer == 0)
          PSBuffer_ProcessTSPacket(&PMTBuffer, p);
        else
          break;
        p += PACKETSIZE;
      }

      AnalysePMT(PMTBuffer.Buffer1, RecInf);
      PSBuffer_Reset(&PMTBuffer);
    }
    else
    {
      printf("  Failed to locate a PMT packet.\n");
      ret = FALSE;
    }

    //If we're here, it should be possible to find the associated EPG event
    PSBuffer_Init(&EITBuffer, 0x0012, 16384);
    LastBuffer = 0;
    EITOK = FALSE;

    fseeko64(fIn, FilePos + ((RecFileSize/2)/PACKETSIZE * PACKETSIZE) + Offset, SEEK_SET);
    for(j = 0; j < 10; j++)
    {
      ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
      p = &Buffer[PACKETOFFSET];

      for(i = 0; i < ReadPackets; i++)
      {
        PSBuffer_ProcessTSPacket(&EITBuffer, p);
        if(EITBuffer.ValidBuffer != LastBuffer)
        {
          byte       *pBuffer;

          if(EITBuffer.ValidBuffer == 1)
            pBuffer = EITBuffer.Buffer1;
          else
            pBuffer = EITBuffer.Buffer2;

          EITOK = AnalyseEIT(pBuffer, RecInf->ServiceInfo.ServiceID, RecInf);
          if(EITOK) break;

          LastBuffer = EITBuffer.ValidBuffer;
        }
        p += PACKETSIZE;
      }
      if(EITOK) break;
    }
    if(!EITOK)
      printf ("  Failed to locate an EIT packet.\n");
    PSBuffer_Reset(&EITBuffer);
  }


  //Read the last RECBUFFERENTRIES TS pakets
  fseeko64(fIn, FilePos + ((((RecFileSize-FilePos)/PACKETSIZE) - RECBUFFERENTRIES) * PACKETSIZE), SEEK_SET);
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
  fseeko64(fIn, FilePos, SEEK_SET);
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
  RecInf->RecHeaderInfo.StartTime = RecInf->EventInfo.StartTime + 0x0100;  // GMT+1
  if (!RecInf->EventInfo.StartTime || ((FileTimeStamp >> 16) - (RecInf->EventInfo.StartTime >> 16) <= 1))
    RecInf->RecHeaderInfo.StartTime = AddTime(FileTimeStamp, -RecInf->RecHeaderInfo.DurationMin);
printf("  TS: Duration = %2.2d min %2.2d sec\n", RecInf->RecHeaderInfo.DurationMin, RecInf->RecHeaderInfo.DurationSec);

  free(Buffer);
  TRACEEXIT;
  return ret;
}
