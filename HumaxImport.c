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


bool GetPidsFromMap(word ServiceID, word *const OutPMTPID, word *const OutVidPID, word *const OutAudPID, word *const OutTtxPID, word *const OutSubtPID)
{
  FILE *fMap;
  char LineBuf[FBLIB_DIR_SIZE], *pPid, *pLng, *NameStr, *p;
  word Sid, PPid, VPid, APid=0, TPid=0, SPid=0;
  int APidStr = 0, ALangStr = 0, k;
  strncpy(LineBuf, ExePath, sizeof(LineBuf));

  if ((p = strrchr(LineBuf, '/'))) p[1] = '\0';
  else if ((p = strrchr(LineBuf, '\\'))) p[1] = '\0';
  else LineBuf[0] = 0;

  strncat(LineBuf, "/SenderMap.txt", sizeof(LineBuf) - (p ? (p-LineBuf+1) : 0));
  
  if ((fMap = fopen(LineBuf, "r")))
  {
    while (fgets(LineBuf, sizeof(LineBuf), fMap))
    {
      if(LineBuf[0] == '#') continue;

      // Remove line breaks in the end
      k = (int)strlen(LineBuf);
      while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
        LineBuf[--k] = '\0';
      
//      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %hu %*1[/] %*15[^;/] ; %*20[^;/] ; %hu ; %hu ; %n %*70[^;\r\n]", &Sid, &PPid, &VPid, &APid, &TPid, &SPid, &BytesRead) == 6)
      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %n %*19[^;] ; %n %*19[^;] ; %hu ; %hu ; %*70[^;\r\n]", &Sid, &PPid, &VPid, &APidStr, &ALangStr, &TPid, &SPid) >= 3)
      {
        if (Sid == ServiceID)
        {
          NameStr = strrchr(LineBuf, ';') + 1;
          if((p = strchr(&LineBuf[APidStr], ';'))) p[0] = '\0';
          pPid = strtok(&LineBuf[APidStr], "/;");
          if(!APid) APid = (word) strtol(pPid, NULL, 10);
          pLng = &LineBuf[ALangStr];
          if(pLng[0] == ';') pLng = NULL;
          for (k = 0; pPid && pLng && (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0); k++)
          {
            // Set AudioPIDs according to Map
            AudioPIDs[k].pid = (word) strtol(pPid, NULL, 10);
            strncpy(AudioPIDs[k].desc, pLng, 3);

            pPid = strtok(NULL, "/;");
            pLng = (pLng && pLng[3] == '/') ? pLng + 4 : NULL;
          }

          printf("  Found service from map: PMTPid=%hd, VidPID=%hd, AudPID=%hd, TtxPID=%hd (%s)\n", PPid, VPid, APid, TPid, NameStr);
          if (OutPMTPID && PPid) *OutPMTPID = PPid;
          if (OutVidPID && VPid) *OutVidPID = VPid;
          if (OutAudPID && APid) *OutAudPID = APid;
          if (OutTtxPID && TPid) *OutTtxPID = TPid;
          if (OutSubtPID && SPid) *OutSubtPID = SPid;
          fclose(fMap);
          return TRUE;
        }
      }
    }
    fclose(fMap);
  }
  return FALSE;
}

/* bool GetSvcNameFromMap(word ServiceID, char *const OutSvcName, char maxOutLen)
{
  FILE *fMap;
  char LineBuf[FBLIB_DIR_SIZE], *p;
  word Sid, PPid, VPid, APid, TPid;
  int BytesRead;
  strncpy(LineBuf, ExePath, sizeof(LineBuf));

  if ((p = strrchr(LineBuf, '/'))) p[1] = '\0';
  else if ((p = strrchr(LineBuf, '\\'))) p[1] = '\0';
  else LineBuf[0] = 0;

  strncat(LineBuf, "/SenderMap.txt", sizeof(LineBuf) - (p ? (p-LineBuf+1) : 0));
  
  if ((fMap = fopen(LineBuf, "r")))
  {
    while (fgets(LineBuf, sizeof(LineBuf), fMap))
    {
      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %hu %*1[/] %*15[^;/] ; %*20[^;/] ; %hu ; %hu ; %n %*70[^;\r\n]", &Sid, &PPid, &VPid, &APid, &TPid, &SPid, &BytesRead) == 6)    
        if (Sid == ServiceID)
        {
          printf("  Detected service name from Map: %s", &LineBuf[BytesRead]);
          if (OutSvcName) {
            strncpy(OutSvcName, &LineBuf[BytesRead], maxOutLen-1);
            OutSvcName[maxOutLen-1] = '\0';
          }
          return TRUE;
        }
    }
    fclose(fMap);
  }
  return FALSE;
} */

word GetSidFromMap(word VidPID, word AudPID, word TtxPID, char *InOutServiceName, word *const OutPMTPID)
{
  FILE *fMap;
  char LineBuf[100], SenderFound[32], PidsFound[20], LangFound[20];
  char *pPid, *pLng, *p;
  word Sid, PPid, VPid, APid = 0, TPid = 0, SPid = 0, curPid;
  int APidStr = 0, ALangStr = 0, k;
  word CandidateFound = 0, PMTFound = 0;
  strncpy(LineBuf, ExePath, sizeof(LineBuf));

  if ((p = strrchr(LineBuf, '/'))) p[1] = '\0';
  else if ((p = strrchr(LineBuf, '\\'))) p[1] = '\0';
  else LineBuf[0] = 0;

  strncat(LineBuf, "SenderMap.txt", sizeof(LineBuf) - (p ? (p-LineBuf+1) : 0));
  
  if ((fMap = fopen(LineBuf, "rb")))
  {
    while (fgets(LineBuf, sizeof(LineBuf), fMap) != 0)
    {
      if(LineBuf[0] == '#') continue;

      // Remove line breaks in the end
      k = (int)strlen(LineBuf);
      while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
        LineBuf[--k] = '\0';

      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %n %*19[^;] ; %n %*19[^;] ; %hu ; %hu ; %*70[^;\r\n]", &Sid, &PPid, &VPid, &APidStr, &ALangStr, &TPid, &SPid) >= 3)
      {
        APid = (word) strtol(&LineBuf[APidStr], NULL, 10);
        p = strrchr(LineBuf, ';') + 1;
        if ((VPid == VidPID) && ((APid == AudPID) || !AudPID || AudPID == (word)-1) && ((TPid == TtxPID) || !TtxPID || TtxPID == (word)-1)
         && ((VidPID != 101 && VidPID != 201 && VidPID != 401 && VidPID != 601) || (*InOutServiceName && ((strncmp(p, InOutServiceName, 2) == 0) || (strncmp(p, "Das", 3)==0 && strncmp(InOutServiceName, "ARD", 3)==0) || (strncmp(p, "BR", 2)==0 && strncmp(InOutServiceName, "Bay", 3)==0)))))
        {
          strncpy(SenderFound, p, sizeof(SenderFound));
          SenderFound[sizeof(SenderFound)-1] = '\0';
          strncpy(PidsFound, &LineBuf[APidStr], sizeof(PidsFound));
          if ((p = strchr(PidsFound, ';'))) p[0] = '\0';
          strncpy(LangFound, &LineBuf[ALangStr], sizeof(LangFound));
          PMTFound = PPid;
          if(CandidateFound) printf("\n");
          printf("  Found ServiceID in Map: SID=%hu (%s), PMTPid=%hu", Sid, SenderFound, PPid);
          if(CandidateFound) { printf(" (ambiguous) -> skipping\n"); return 1; }
          CandidateFound = Sid;
        }
        else if (CandidateFound)  // Nutzt aus, dass die SenderMap nach VideoPID sortiert ist
        {
          printf(" (unique) -> using SID\n");

          pPid = strtok(PidsFound, "/;");
          pLng = LangFound;
          if(pLng[0] == ';') pLng = NULL;
          while (pPid && pLng)
          {
            curPid = (word) strtol(pPid, NULL, 10);
            
            // Add Language-Code from Map to AudioPIDs
            for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != curPid); k++);
            if (AudioPIDs[k].pid == curPid /*&& !AudioPIDs[k].desc*/)
              strncpy(AudioPIDs[k].desc, pLng, 3);

            pPid = strtok(NULL, "/;");
            pLng = (pLng && pLng[3] == '/') ? pLng + 4 : NULL;
          }

          if(OutPMTPID && (!*OutPMTPID || *OutPMTPID==256)) *OutPMTPID = PMTFound;
          if(InOutServiceName && !*InOutServiceName) strncpy(InOutServiceName, SenderFound, sizeof(((TYPE_Service_Info*)NULL)->ServiceName));
          fclose(fMap);
          return CandidateFound;
        }
      }
    }
    fclose(fMap);
  }
  return 1;
}


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
          RecInf->ServiceInfo.PMTPID          = 0;  // vorher: (HumaxHeader.Allgemein.AudioPID != 256) ? 256 : 100;
          RecInf->ServiceInfo.VideoPID        = VideoPID;
          RecInf->ServiceInfo.PCRPID          = VideoPID;
          RecInf->ServiceInfo.AudioPID        = HumaxHeader.Allgemein.AudioPID;
          RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG2;
          RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG2;
          RecInf->ServiceInfo.AudioTypeFlag   = 0;
          RecInf->RecHeaderInfo.StartTime     = DATE(HumaxHeader.Allgemein.Datum, HumaxHeader.Allgemein.Zeit / 60, HumaxHeader.Allgemein.Zeit % 60);
          RecInf->RecHeaderInfo.DurationMin   = (word)(HumaxHeader.Allgemein.Dauer / 60);
          RecInf->RecHeaderInfo.DurationSec   = (word)(HumaxHeader.Allgemein.Dauer % 60);
          ContinuityPIDs[0] = VideoPID;
          printf("    PMTPID=%hd, SID=%hu, PCRPID=%hd, Stream=0x%hhx, VPID=%hd, APID=%hd, TtxPID=%hd\n", RecInf->ServiceInfo.PMTPID, RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID, RecInf->ServiceInfo.VideoStreamType, VideoPID, HumaxHeader.Allgemein.AudioPID, TeletextPID);

          printf("    Start Time: %s\n", TimeStrTF(RecInf->RecHeaderInfo.StartTime, 0));

          if(p) *p = '\0';
          strncpy(FirstSvcName, HumaxHeader.Allgemein.Dateiname, sizeof(FirstSvcName));
          FirstSvcName[sizeof(FirstSvcName)-1] = '\0';
// manuelle Ausnahme (IMGARTENEDEN2_0601112255.vid):
if(VideoPID == 660 && TeletextPID == 130 && ExtractTeletext && DoStrip) RemoveTeletext = TRUE;
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
          else
            printf("    Assertion error: Humax rec name without sender!\n");
        }
        else if (HumaxHeader.ZusInfoID == HumaxBookmarksID)  // Header 3: Bookmarks
        {
          tHumaxBlock_Bookmarks* HumaxBookmarks = (tHumaxBlock_Bookmarks*)HumaxHeader.ZusInfos;
          RecInf->BookmarkInfo.NrBookmarks = HumaxBookmarks->Anzahl;
          for (j = 0; j < HumaxBookmarks->Anzahl; j++)
            RecInf->BookmarkInfo.Bookmarks[j] = (dword) ((long long)HumaxBookmarks->Items[j] * 32768 / 9024);
        }
        if ((i == 4) || (HumaxHeader.ZusInfoID == HumaxTonSpurenID))  // Header 4: Tonspuren
        {
          AudioPIDs[0].pid = HumaxHeader.Allgemein.AudioPID;
          AudioPIDs[0].sorted = TRUE;
          
          if (HumaxHeader.ZusInfoID == HumaxTonSpurenID)
          {
            tHumaxBlock_Tonspuren* HumaxTonspuren = (tHumaxBlock_Tonspuren*)HumaxHeader.ZusInfos;
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
                for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != HumaxTonspuren->Items[j].PID); k++);
                if (k < MAXCONTINUITYPIDS)
                {
                  AudioPIDs[k].pid = HumaxTonspuren->Items[j].PID;
                  AudioPIDs[k].sorted = TRUE;
                  strncpy(AudioPIDs[k].desc, HumaxTonspuren->Items[j].Name, 3);
                }
              }
              AddContinuityPids(HumaxTonspuren->Items[j].PID, FALSE);
            }
          }
          if(!*AudioPIDs[0].desc) strncpy(AudioPIDs[0].desc, "deu", 3);
        }
      }
    }
  }
  RecInf->ServiceInfo.ServiceID = GetSidFromMap(VideoPID, 0 /*GetMinimalAudioPID(AudioPIDs)*/, 0, RecInf->ServiceInfo.ServiceName, &RecInf->ServiceInfo.PMTPID);
  if (!RecInf->ServiceInfo.PMTPID)
    RecInf->ServiceInfo.PMTPID = (HumaxHeader.Allgemein.AudioPID != 256) ? 256 : 100;
  AddContinuityPids(TeletextPID, FALSE);

  for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != TeletextPID); k++);
  if (k < MAXCONTINUITYPIDS)
  {
    AudioPIDs[k].pid = TeletextPID;
    AudioPIDs[k].sorted = TRUE;
  }

//  fseeko64(fIn, FilePos, SEEK_SET);
  if (!ret)
    printf("  Failed to read the Humax header from rec.\n");
  TRACEEXIT;
  return ret;
}
