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
#include "RebuildInf.h"
#include "RecStrip.h"

#ifdef _WIN32
  #define fseeko64 _fseeki64
  #define ftello64 _ftelli64
#endif


//------ TS to PS converter
/*void PSBuffer_Reset(tPSBuffer *PSBuffer)
{
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
}

void PSBuffer_Init(tPSBuffer *PSBuffer, word PID, int BufferSize)
{
  memset(PSBuffer, 0, sizeof(tPSBuffer));
  PSBuffer->PID = PID;
  PSBuffer->BufferSize = BufferSize;

  PSBuffer->Buffer1 = malloc(BufferSize);
  PSBuffer->Buffer2 = malloc(BufferSize);
  PSBuffer->pBuffer = &PSBuffer->Buffer1[0];
  PSBuffer->LastCCCounter = 255;
}

void PSBuffer_ProcessTSPacket(tPSBuffer *PSBuffer, byte *TSBuffer)
{
  word                  PID;
  byte                  RemainingBytes;

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
      PSBuffer->LastCCCounter = (PSBuffer->LastCCCounter + 1) & 0x0f;
      if((TSBuffer[3] & 0x0f) == PSBuffer->LastCCCounter)
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
} */


void AnalyzePMT(byte *PSBuffer)
{
  dword                 DescrPt;
  short                 SectionLength, DescriptorType, DescriptorLength, ProgramInfoLength;
  word                  PID;
  char                  Log[512];
  bool                  VideoFound = FALSE, HDFound = FALSE;

  TRACEENTER;
  Log[0] = '\0';

  //The following variables have a constant distance from the packet header
  SectionLength = (((PSBuffer[0x01] << 8) | PSBuffer[0x02]) & 0xfff) - 7;

  snprintf(Log, sizeof(Log), ", ServiceID=0x%4.4x", ((PSBuffer[0x03] << 8) | PSBuffer[0x04]));
  snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), ", PCRPID=0x%4.4x", ((PSBuffer[0x08] << 8) | PSBuffer [0x09]) & 0x1fff);

  ProgramInfoLength = ((PSBuffer[0x0a] << 8) | PSBuffer[0x0b]) & 0xfff;

  //Program info needed to decode all ECMs
  DescrPt = 0x0c;

  while(ProgramInfoLength > 0)
  {
    DescriptorType   = PSBuffer[DescrPt];
    DescriptorLength = PSBuffer[DescrPt + 1];
    DescrPt           += (DescriptorLength + 2);
    SectionLength     -= (DescriptorLength + 2);
    ProgramInfoLength -= (DescriptorLength + 2);
  }

  //Loop through all elementary stream descriptors and search for the Audio and Video descriptor
  while ((SectionLength > 0) && !VideoFound)
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
        HDFound = TRUE;  // fortsetzen...

      case STREAM_VIDEO_MPEG1:
      case STREAM_VIDEO_MPEG2:
        VideoFound = TRUE;
        VideoPID = PID;
        snprintf(&Log[strlen(Log)], sizeof(Log)-strlen(Log), ", VideoStream=0x%x, VideoPID=0x%4.4x, HD=%d", PSBuffer[DescrPt], VideoPID, isHDVideo);
        break;
    }

    SectionLength -= (DescriptorLength + 5);
    DescrPt += (DescriptorLength + 5);
  }
  printf("%s\n", Log);
  if (HDFound != isHDVideo)
    printf("ERROR! Inconsistent video format. inf: %s, PMT(mid): %s\n", isHDVideo ? "HD" : "SD", HDFound ? "HD" : "SD");
  isHDVideo = HDFound;
  TRACEEXIT;
}

bool GetVideoInfos(FILE* fIn)
{
  byte*                 Buffer = NULL;
//  tPSBuffer             PMTBuffer;
  unsigned long long    RecFileSize;
  size_t                ReadPackets;
  byte                  ANDMask [6] = {0xFF, 0xC0, 0x00, 0xD0, 0xFF, 0xFF};
  byte                  PMTMask [6] = {0x47, 0x40, 0x00, 0x10, 0x00, 0x02};
  word                  PMTPID;
  byte                 *p;
  int                   i, j;

  TRACEENTER;
  Buffer = (byte*) malloc(RECBUFFERENTRIES * PACKETSIZE);
  if (!Buffer)
  {
    printf("  Failed to allocate the buffer.\n");
    TRACEEXIT;
    return FALSE;
  }

  // Springe in die Mitte der Aufnahme
  HDD_GetFileSize(RecFileIn, &RecFileSize);
  fseeko64(fIn, ((RecFileSize/2))/PACKETSIZE*PACKETSIZE, SEEK_SET);

  //Read the first RECBUFFERENTRIES TS pakets for analysis
  ReadPackets = fread(Buffer, PACKETSIZE, RECBUFFERENTRIES, fIn);
  rewind(fIn);
  if(ReadPackets != RECBUFFERENTRIES)
  {
    printf("  Failed to read the first %d TS packets.\n", RECBUFFERENTRIES);
    free(Buffer);
    TRACEEXIT;
    return FALSE;
  }

  //Find a PMT packet to get its PID
  PMTPID = 0;

  p = &Buffer[PACKETOFFSET];
  for(i = 0; i < RECBUFFERENTRIES; i++)
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
    printf("  PMTPID=0x%4.4x", PMTPID);
  else
  {
    printf("  Failed to locate a PMT packet.\n");
    free(Buffer);
    TRACEEXIT;
    return FALSE;
  }

  //Analyze the PMT
/*  PSBuffer_Init(&PMTBuffer, PMTPID, 16384);

  p = &Buffer[PACKETOFFSET];
  for(i = 0; i < RECBUFFERENTRIES; i++)
  {
    if(PMTBuffer.ValidBuffer == 0) PSBuffer_ProcessTSPacket(&PMTBuffer, p);
    p += PACKETSIZE;
  } */

  AnalyzePMT(p+5);
//  AnalyzePMT(PMTBuffer.Buffer1);
//  PSBuffer_Reset(&PMTBuffer);

  free(Buffer);
  TRACEEXIT;
  return TRUE;
}
