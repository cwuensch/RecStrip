#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "type.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "RebuildInf.h"
#include "TtxProcessor.h"
#include "HumaxHeader.h"


static const dword crc_table[] = {
  0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9,
  0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
  0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61,
  0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD
};

dword crc32m_tab(const unsigned char *buf, size_t len)
{
  dword crc = 0xffffffff;
  while (len--) {
    crc ^= (dword)(*buf++) << 24;
    crc = (crc << 4) ^ crc_table[crc >> 28];
    crc = (crc << 4) ^ crc_table[crc >> 28];
  }

  crc = ((crc & 0x000000ff) << 24 | (crc & 0x0000ff00) << 8 | (crc & 0x00ff0000) >> 8 | (crc & 0xff000000) >> 24);
  return crc;
}

static dword crc32m(const unsigned char *buf, size_t len)
{
  dword crc = 0xffffffff;
  int i;

  while (len--) {
    crc ^= (dword)(*buf++) << 24;
    for (i = 0; i < 8; i++)
      crc = crc & 0x80000000 ? (crc << 1) ^ 0x04c11db7 : crc << 1;
  }

  crc = ((crc & 0x000000ff) << 24 | (crc & 0x0000ff00) << 8 | (crc & 0x00ff0000) >> 8 | (crc & 0xff000000) >> 24);
  return crc;
}

/* static dword rocksoft_crc(const byte data[], int len)
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
} */

bool SaveHumaxHeader(char *const VidFileName, char *const OutFileName)
{
  FILE                 *fIn, *fOut;
  byte                  HumaxHeader[HumaxHeaderLaenge];
  int                   i;
  bool                  ret = TRUE;

  if ((fIn = fopen(VidFileName, "rb")))
  {
    if ((fOut = fopen(OutFileName, "wb")))
    {
      for (i = 1; i <= 4; i++)
      {
        fseeko64(fIn, (i*HumaxHeaderIntervall) - HumaxHeaderLaenge, SEEK_SET);
        if (fread(&HumaxHeader, HumaxHeaderLaenge, 1, fIn))
        {
          if (*(dword*)HumaxHeader == HumaxHeaderAnfang)
            ret = fwrite(&HumaxHeader, HumaxHeaderLaenge, 1, fOut) && ret;
          else
            ret = FALSE;
        }
        else
          ret = FALSE;
      }
      ret = (fclose(fOut) == 0) && ret;
    }
    fclose(fIn);
  }
  return ret;
}

bool LoadHumaxHeader(FILE *fIn, TYPE_RecHeader_TMSS *RecInf)
{
  tHumaxHeader          HumaxHeader;
  char                  FirstSvcName[32];
  int                   i, j, k;
  bool                  ret = TRUE;

  TRACEENTER;
  InitInfStruct(RecInf);

//  rewind(fIn);
  for (i = 1; ret && (i <= 4); i++)
  {
    fseeko64(fIn, (i*HumaxHeaderIntervall) - HumaxHeaderLaenge, SEEK_SET);
    if ((ret = (ret && fread(&HumaxHeader, sizeof(tHumaxHeader), 1, fIn))))
    {
      ret = ret && (HumaxHeader.Allgemein.Anfang == HumaxHeaderAnfang);
      for (j = 0; ret && (j < 8); j++)
        ret = ret && (HumaxHeader.Allgemein.Anfang2[j] == HumaxHeaderAnfang2);

      if (ret)
      {
        char *p = strrchr(HumaxHeader.Allgemein.Dateiname, '_');

        if (i == 1)  // Header 1: Programm-Information
        {
          printf("  Importing Humax header\n");

          VideoPID                            = HumaxHeader.Allgemein.VideoPID;
          TeletextPID                         = HumaxHeader.Allgemein.TeletextPID;
          RecInf->ServiceInfo.ServiceType     = 0;  // SVC_TYPE_Tv
          RecInf->ServiceInfo.ServiceID       = 1;
          RecInf->ServiceInfo.PMTPID          = (HumaxHeader.Allgemein.AudioPID != 256) ? 256 : 100;
          RecInf->ServiceInfo.VideoPID        = VideoPID;
          RecInf->ServiceInfo.PCRPID          = VideoPID;
          RecInf->ServiceInfo.AudioPID        = HumaxHeader.Allgemein.AudioPID;
          RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG2;
          RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG2;
          RecInf->RecHeaderInfo.StartTime     = DATE(HumaxHeader.Allgemein.Datum, HumaxHeader.Allgemein.Zeit / 60, HumaxHeader.Allgemein.Zeit % 60);
          RecInf->RecHeaderInfo.DurationMin   = (word)(HumaxHeader.Allgemein.Dauer / 60);
          RecInf->RecHeaderInfo.DurationSec   = (word)(HumaxHeader.Allgemein.Dauer % 60);
          ContinuityPIDs[0] = VideoPID;
          printf("    PMTPID=%hd, SID=%hu, PCRPID=%hd, Stream=0x%hhx, VPID=%hd, TtxPID=%hd\n", RecInf->ServiceInfo.PMTPID, RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID, RecInf->ServiceInfo.VideoStreamType, VideoPID, TeletextPID);

          printf("    Start Time: %s\n", TimeStrTF(RecInf->RecHeaderInfo.StartTime, 0));

          if(p) *p = '\0';
          strncpy(FirstSvcName, HumaxHeader.Allgemein.Dateiname, sizeof(FirstSvcName));
          FirstSvcName[sizeof(FirstSvcName)-1] = '\0';
        }
        else if (i == 2)  // Header 2: Original-Dateiname
        {
          printf("    Orig Rec Name: %s\n", HumaxHeader.Allgemein.Dateiname);
          if(p) *p = '\0';
          if (strcmp(HumaxHeader.Allgemein.Dateiname, FirstSvcName) != 0)
          {
            strncpy(RecInf->ServiceInfo.ServiceName, HumaxHeader.Allgemein.Dateiname, sizeof(RecInf->ServiceInfo.ServiceName));
            RecInf->ServiceInfo.ServiceName[sizeof(RecInf->ServiceInfo.ServiceName)-1] = '\0';
          }
        }
        else if (HumaxHeader.ZusInfoID == HumaxBookmarksID)  // Header 3: Bookmarks
        {
          tHumaxBlock_Bookmarks* HumaxBookmarks = (tHumaxBlock_Bookmarks*)HumaxHeader.ZusInfos;
          RecInf->BookmarkInfo.NrBookmarks = HumaxBookmarks->Anzahl;
          for (j = 0; j < HumaxBookmarks->Anzahl; j++)
            RecInf->BookmarkInfo.Bookmarks[j] = (dword) ((long long)HumaxBookmarks->Items[j] * 32768 / 9024);
        }
        else if ((i == 4) || (HumaxHeader.ZusInfoID == HumaxTonSpurenID))  // Header 4: Tonspuren
        {
          tHumaxBlock_Tonspuren* HumaxTonspuren = (tHumaxBlock_Tonspuren*)HumaxHeader.ZusInfos;

          AudioPIDs[0].pid = HumaxHeader.Allgemein.AudioPID;
          AudioPIDs[0].sorted = TRUE;
          
          for (j = 0; j < HumaxTonspuren->Anzahl; j++)
          {
            if (HumaxTonspuren->Items[j].PID == AudioPIDs[0].pid)
            {
              strncpy(AudioPIDs[0].desc, HumaxTonspuren->Items[j].Name, 3);
              if ((j >= 1) && (strncmp(AudioPIDs[0].desc, "AC", 2) != 0 || strncmp(AudioPIDs[0].desc, "ac", 2) != 0))   // (strstr(AudioPIDs[k].desc, "2ch") == 0) && (strstr(AudioPIDs[k].desc, "mis") == 0) && (strstr(AudioPIDs[k].desc, "fra") == 0)
              {
                RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG4_AC3_PLUS;
                RecInf->ServiceInfo.AudioTypeFlag = 1;
              }
            }
            else
            {
              for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0); k++);
              if (k < MAXCONTINUITYPIDS)
              {
                AudioPIDs[k].pid = HumaxTonspuren->Items[j].PID;
                AudioPIDs[k].sorted = TRUE;
                strncpy(AudioPIDs[k].desc, HumaxTonspuren->Items[j].Name, 3);
              }
            }

            if (NrContinuityPIDs < MAXCONTINUITYPIDS)
            {
              for (k = 1; (k < NrContinuityPIDs) && (ContinuityPIDs[k] != HumaxTonspuren->Items[j].PID); k++);
              if (k <= NrContinuityPIDs)
                ContinuityPIDs[NrContinuityPIDs++] = HumaxTonspuren->Items[j].PID;
            }
          }
        }
      }
    }
  }

  if(NrContinuityPIDs < MAXCONTINUITYPIDS && TeletextPID != 0xffff)  ContinuityPIDs[NrContinuityPIDs++] = TeletextPID;

//  fseeko64(fIn, FilePos, SEEK_SET);
  if (!ret)
    printf("  Failed to read the Humax header from rec.\n");
  TRACEEXIT;
  return ret;
}
