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
#include "type.h"
#include "crcmodel.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "HumaxHeader.h"


char PATPMTBuf[2*192];  // Generiert eine PAT/PMT aus der Humax Header-Information


dword rocksoft_crc(byte data[], int len)
{
  cm_t cm;
  ulong crc;
  int i;

  cm.cm_width = 32;
  cm.cm_poly  = 0x04c11db7;
  cm.cm_init  = 0xffffffff;
  cm.cm_refin = FALSE;
  cm.cm_refot = FALSE;
  cm.cm_xorot = 0;
  cm_ini(&cm);

  for (i = 0; i < len; i++)
    cm_nxt(&cm, data[i]);

  crc = cm_crc(&cm);
  crc = ((crc & 0x000000ff) << 24 | (crc & 0x0000ff00) << 8 | (crc & 0x00ff0000) >> 8 | (crc & 0xff000000) >> 24);
  return crc;
}


bool LoadHumaxHeader(FILE *fIn, TYPE_RecHeader_TMSS *RecInf)
{
  THumaxHeader          HumaxHeader;
  tTSPacket            *packet = NULL;
  TTSPAT               *pat = NULL;
  TTSPMT               *pmt = NULL;
  TElemStream          *elem = NULL;
  dword                *CRC = NULL;
  int                   offset = 0;
  long long             FilePos = ftello64(fIn);
  int                   i, j;
  bool                  ret = TRUE;

  TRACEENTER;
  memset(PATPMTBuf, 0, 2*192);

  packet = (tTSPacket*) &PATPMTBuf[4];
  pat = (TTSPAT*) &packet->Data[1 + packet->Data[0]];

  packet->SyncByte      = 'G';
  packet->PID1          = 0;
  packet->PID2          = 0;
  packet->Payload_Unit_Start = 1;
  packet->Payload_Exists = 1;

  pat->TableID          = 0;
  pat->SectionLen1      = 0;
  pat->SectionLen2      = sizeof(TTSPAT) - 3;
  pat->Reserved1        = 3;
  pat->Reserved0        = 0;
  pat->SectionSyntax    = 1;
  pat->TS_ID1           = 0;
  pat->TS_ID2           = 6;  // ??
  pat->CurNextInd       = 1;
  pat->VersionNr        = 3;
  pat->Reserved11       = 3;
  pat->SectionNr        = 0;
  pat->LastSection      = 0;
  pat->ProgramNr1       = 0;
  pat->ProgramNr2       = 1;
  pat->PMTPID1          = 0;
  pat->PMTPID2          = 0xb1;  // ??
  pat->Reserved111      = 7;
  pat->CRC32            = rocksoft_crc((byte*)pat, sizeof(TTSPAT)-4);    // CRC: 0x786989a2
  
  offset = 1 + packet->Data[0] + sizeof(TTSPAT);
  memset(&packet->Data[offset], 0xff, 184 - offset);


  packet = (tTSPacket*) &PATPMTBuf[196];
  pmt = (TTSPMT*) &packet->Data[1 + packet->Data[0]];

  packet->SyncByte      = 'G';
  packet->PID1          = 0;
  packet->PID2          = 0xb1;
  packet->Payload_Unit_Start = 1;
  packet->Payload_Exists = 1;

  pmt->TableID          = 2;
  pmt->SectionLen1      = 0;
  pmt->SectionLen2      = sizeof(TTSPMT) - 3 + 4;
  pmt->Reserved1        = 3;
  pmt->Reserved0        = 0;
  pmt->SectionSyntax    = 1;
  pmt->ProgramNr1       = 0;
  pmt->ProgramNr2       = 1;
  pmt->CurNextInd       = 1;
  pmt->VersionNr        = 0;
  pmt->Reserved11       = 3;
  pmt->SectionNr        = 0;
  pmt->LastSection      = 0;

  pmt->ReservedX        = 7;

  pmt->ProgInfoLen1     = 0;
  pmt->ProgInfoLen2     = 0;
  pmt->ReservedY        = 15;

  offset = 1 + packet->Data[0] + sizeof(TTSPMT);
  

  rewind(fIn);
  for (i = 1; ret && (i <= 4); i++)
  {
    fseeko64(fIn, (i*HumaxHeaderIntervall) - HumaxHeaderLaenge, SEEK_SET);
    if ((ret = (ret && fread(&HumaxHeader, sizeof(THumaxHeader), 1, fIn))))
    {
      ret = ret && (HumaxHeader.Allgemein.Anfang == HumaxHeaderAnfang);
      for (j = 0; ret && (j < 8); j++)
        ret = ret && (HumaxHeader.Allgemein.Anfang2[j] == HumaxHeaderAnfang2);

      if (ret)
      {
        if (i == 1)
        {
          printf("  Importing Humax header\n");
          VideoPID = HumaxHeader.Allgemein.VideoPID;
          TeletextPID = HumaxHeader.Allgemein.TeletextPID;
          RecInf->ServiceInfo.ServiceType     = 0;  // SVC_TYPE_Tv
          RecInf->ServiceInfo.ServiceID       = 1;
          RecInf->ServiceInfo.PMTPID          = 0xb1;
          RecInf->ServiceInfo.VideoPID        = VideoPID;
          RecInf->ServiceInfo.PCRPID          = VideoPID;
          RecInf->ServiceInfo.AudioPID        = HumaxHeader.Allgemein.AudioPID;
          RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG2;
          RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG2;
          RecInf->RecHeaderInfo.StartTime     = (HumaxHeader.Allgemein.Datum << 16) | ((HumaxHeader.Allgemein.Zeit/60) << 8) | (HumaxHeader.Allgemein.Zeit%60);
          RecInf->RecHeaderInfo.DurationMin   = (word)(HumaxHeader.Allgemein.Dauer / 60);
          RecInf->RecHeaderInfo.DurationSec   = (word)(HumaxHeader.Allgemein.Dauer % 60);
        }
        if (HumaxHeader.ZusInfoID == HumaxBookmarksID)
        {
          THumaxBlock_Bookmarks* HumaxBookmarks = (THumaxBlock_Bookmarks*)HumaxHeader.ZusInfos;
          RecInf->BookmarkInfo.NrBookmarks = HumaxBookmarks->Anzahl;
          for (j = 0; j < HumaxBookmarks->Anzahl; j++)
            RecInf->BookmarkInfo.Bookmarks[j] = HumaxBookmarks->Items[j] * 32768 / 9024;
        }
        else if (HumaxHeader.ZusInfoID == HumaxTonSpurenID)
        {
          THumaxBlock_Tonspuren* HumaxTonspuren = (THumaxBlock_Tonspuren*)HumaxHeader.ZusInfos;

          for (j = HumaxTonspuren->Anzahl; j >= -1; j--)
          {
            elem = (TElemStream*) &packet->Data[offset];
            elem->ReservedZ       = 7;
            elem->ReservedQ       = 0xf;
            offset                += sizeof(TElemStream);

            if (j >= HumaxTonspuren->Anzahl)
            {
              elem->stream_type     = STREAM_VIDEO_MPEG2;
              elem->ESPID1          = VideoPID / 256;
              elem->ESPID2          = VideoPID % 256;
              elem->ESInfoLen1      = 0;
              elem->ESInfoLen2      = 0;
            }
            else if (j >= 0)
            {
              printf("    Tonspur %d: %d (%s) \n", j, HumaxTonspuren->Items[j].PID, HumaxTonspuren->Items[j].Name);
              elem->ESPID1          = HumaxTonspuren->Items[j].PID / 256;
              elem->ESPID2          = HumaxTonspuren->Items[j].PID % 256;
              elem->ESInfoLen1      = 0;
              if ((j >= 1) && (strstr(HumaxTonspuren->Items[j].Name, "2ch") == 0))
              {
                elem->stream_type   = STREAM_AUDIO_MPEG4_AC3;
                elem->ESInfoLen2    = 12;
                strcpy(&packet->Data[offset], "\x05\x04" "AC-3" "\x0A\x04" "deu");
              }
              else
              {
                elem->stream_type   = STREAM_AUDIO_MPEG2;
                elem->ESInfoLen2    = 6;
                strcpy(&packet->Data[offset], "\x0A\x04" "deu");
              }
            }
            else
            {
              elem->stream_type     = 6;
              elem->ESPID1          = HumaxHeader.Allgemein.TeletextPID / 256;
              elem->ESPID2          = HumaxHeader.Allgemein.TeletextPID % 256;
              elem->ESInfoLen1      = 0;
              elem->ESInfoLen2      = 7;
              strcpy(&packet->Data[offset], "V" "\x05" "deu" "\x09");
            }

            offset                += elem->ESInfoLen2;
            pmt->SectionLen2      += sizeof(TElemStream) + elem->ESInfoLen2;
          }
        }
      }
    }
  }

  pmt->PCRPID1          = VideoPID / 256;
  pmt->PCRPID2          = VideoPID % 256;
  CRC                   = (dword*) &packet->Data[offset];
  *CRC                  = rocksoft_crc((byte*)pmt, (int)CRC - (int)pmt);   // CRC: 0xb3ad75b7
  offset               += 4;
  memset(&packet->Data[offset], 0xff, 184 - offset);

  fseeko64(fIn, FilePos, SEEK_SET);
  if (!ret)
    printf("  Failed to read the Humax header from rec.\n");
  TRACEEXIT;
  return ret;
}
