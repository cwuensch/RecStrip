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
#include <limits.h>
#include <string.h>
#include "type.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "PESProcessor.h"
#include "RebuildInf.h"
#include "TtxProcessor.h"
#include "CutProcessor.h"
#include "HumaxHeader.h"

#include <sys/stat.h>
#ifdef _WIN32
  #undef PATH_SEPARATOR
  #define PATH_SEPARATOR "\\"
  #define PATH_DELIMITER ';'
  #define S_IFLNK 0
  #define PATH_MAX FBLIB_DIR_SIZE
#else
  #include <unistd.h>
  #undef PATH_SEPARATOR
  #define PATH_SEPARATOR "/"
  #define PATH_DELIMITER ':'
#endif

#ifdef _MSC_VER
  #define strncasecmp _strnicmp
#endif

static const dword crc_table[] = {
  0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9,
  0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
  0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61,
  0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD
};

bool KeepHumaxSvcName;


static inline byte BIN2BCD(byte BIN)
{
  return (((BIN / 10) << 4) & 0xf0) | ((BIN % 10) & 0x0f);
}

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

static bool GetRealPath(const char* RelativePath, char *const OutAbsPath, int OutputSize)
{
  #ifdef _WIN32
    return (_fullpath(OutAbsPath, RelativePath, OutputSize) != NULL);
  #else
  {
    bool ret = FALSE;
    char *FullPath;
    if ((FullPath = realpath(RelativePath, NULL)))
    {
      ret = (strncpy(OutAbsPath, FullPath, OutputSize) != NULL);
      OutAbsPath[OutputSize-1] = '\0';
      free(FullPath);
    }
    return ret;
  }
  #endif
}

bool FindExePath(const char* CalledExe, char *const OutExePath, int OutputSize)
{
  struct stat statbuf = {0};
  char *curPath, *pPath, *p;
  bool WinExe = FALSE, ret = TRUE;

  if (!GetRealPath(CalledExe, OutExePath, OutputSize))
    strncpy(OutExePath, CalledExe, OutputSize);

  // Under Windows, check if ".exe" needs to be appended
  #ifdef _WIN32
    if((strlen(CalledExe) <= 4) || (strncasecmp(&CalledExe[strlen(CalledExe) - 4], ".exe", 4) != 0))
    {
      if ((int)strlen(OutExePath) + 4 < OutputSize)  strcat(OutExePath, ".exe");
      WinExe = TRUE;
    }
  #endif

  // Is CalledExe a valid file?
  if ((stat(OutExePath, &statbuf) != 0) || (((statbuf.st_mode & S_IFMT) != S_IFREG) && ((statbuf.st_mode & S_IFMT) != S_IFLNK)))
  {
    ret = FALSE;
    // First, get the PATH environment variable
    if ((pPath = getenv("PATH")) && *pPath && ((curPath = (char*) malloc(PATH_MAX))))
    {
      curPath[PATH_MAX - 1] = '\0';
      // Prepend each item of PATH variable before CalledExe
      for (p = strchr(pPath, PATH_DELIMITER); pPath && *pPath; p = strchr(((pPath = p+1)), PATH_DELIMITER))
      {
        int len = min(((p) ? (int)(p - pPath) : (int)strlen(pPath)), PATH_MAX-1);
        strncpy(curPath, pPath, len);
        snprintf(&curPath[len], PATH_MAX - len, (WinExe ? PATH_SEPARATOR "%s.exe" : PATH_SEPARATOR "%s"), CalledExe);

        if ((stat(curPath, &statbuf) == 0) && (((statbuf.st_mode & S_IFMT) == S_IFREG) || ((statbuf.st_mode & S_IFMT) == S_IFLNK)))
          { GetRealPath(curPath, OutExePath, OutputSize); ret = TRUE; break; }
      }
      free(curPath);
    }
  }

  // Remove Exe file name from path
  if ((p = strrchr(OutExePath, '/'))) p[1] = '\0';
  else if ((p = strrchr(OutExePath, '\\'))) p[1] = '\0';
  else strcpy(OutExePath, "." PATH_SEPARATOR);
  return ret;
}


bool GetPidsFromMap(word *const InOutServiceID, word *const OutPMTPID, word *const OutVidPID, word *const OutAudPID, word *const OutTtxPID, word *const OutSubtPID, char *const OutServiceName)
{
  FILE *fMap = NULL, *fMap2;
  char LineBuf[FBLIB_DIR_SIZE], *pPid, *pLng, *NameStr, *p;
  word Sid, PPid, VPid, APid=0, TPid=0, SPid=0;
  int APidStr = 0, ALangStr = 0, k;

  if(!InOutServiceID) return FALSE;

//  strncpy(LineBuf, ExePath, sizeof(LineBuf));
  FindExePath(ExePath, LineBuf, sizeof(LineBuf));
  k = (int)strlen(LineBuf);
  if (MedionMode == 1)
  {
    strncpy(&LineBuf[k], "SenderMap_Medion.txt", (int)sizeof(LineBuf) - k - 1);
    LineBuf[sizeof(LineBuf) - 1] = '\0';
    fMap = fopen(LineBuf, "r");
  }
  if (!fMap)
  {
    strncpy(&LineBuf[k], "SenderMap.txt", (int)sizeof(LineBuf) - k - 1);
    LineBuf[sizeof(LineBuf) - 1] = '\0';
    fMap = fopen(LineBuf, "r");
  }

  if (fMap)
  {
    // bei Medion: ServiceID aus HumaxMap holen, falls nicht bekannt
    if ((MedionMode == 1) && (*InOutServiceID <= 1))
    {
      strncpy(&LineBuf[k], "HumaxMap.txt", (int)sizeof(LineBuf) - k - 1);
      if ((fMap2 = fopen(LineBuf, "rb")))
      {
        int len;
        if ((p = strrchr(RecFileIn, '/'))) p++;
        else if ((p = strrchr(RecFileIn, '\\'))) p++;
        else p = RecFileIn;
        len = (int)strlen(p);
        while (fgets(LineBuf, sizeof(LineBuf), fMap2) != 0)
        {
          if (strncmp(LineBuf, p, len) == 0)
          {
            if (sscanf(&LineBuf[len + 1], "%hu %*70[^;\r\n]", &Sid) >= 1)
              if(Sid > 1) *InOutServiceID = Sid;
            break;
          }
        }
        fclose(fMap2);
      }
    }

    while (fgets(LineBuf, sizeof(LineBuf), fMap))
    {
      if(LineBuf[0] == '#') continue;

      // Remove line breaks in the end
      k = (int)strlen(LineBuf);
      while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
        LineBuf[--k] = '\0';
      
      Sid = 0; PPid = 0; VPid = 0; APid = 0; APidStr = 0; ALangStr = 0; TPid = 0; SPid = 0;
//      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %hu %*1[/] %*15[^;/] ; %*20[^;/] ; %hu ; %hu ; %n %*70[^;\r\n]", &Sid, &PPid, &VPid, &APid, &TPid, &SPid, &BytesRead) == 6)
      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %n %*19[^;] ; %n %*19[^;] ; %hu ; %hu ; %*70[^;\r\n]", &Sid, &PPid, &VPid, &APidStr, &ALangStr, &TPid, &SPid) >= 3)
      {
        if (InOutServiceID && (Sid == *InOutServiceID))
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
//            AudioPIDs[k].streamType = STREAMTYPE_AUDIO;
            strncpy(AudioPIDs[k].desc, pLng, 3);

            pPid = strtok(NULL, "/;");
            pLng = (pLng && pLng[3] == '/') ? pLng + 4 : NULL;
          }

          printf("  Found service from map: ServiceID=%hu, PMTPid=%hd, VidPID=%hd, AudPID=%hd, TtxPID=%hd (%s)\n", Sid, PPid, VPid, APid, TPid, NameStr);
          if (OutPMTPID && PPid) *OutPMTPID = PPid;
          if (OutVidPID && VPid) *OutVidPID = VPid;
          if (OutAudPID && APid) *OutAudPID = APid;
          if (OutTtxPID && TPid) *OutTtxPID = TPid;
          if (OutSubtPID && SPid) *OutSubtPID = SPid;
          if (OutServiceName && *NameStr) strncpy(OutServiceName, NameStr, sizeof(((TYPE_Service_Info*)NULL)->ServiceName));
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

word GetSidFromMap(word VidPID, word AudPID, word TtxPID, char *const InOutServiceName, word *const OutPMTPID, bool UseHumaxMap)
{
  FILE *fMap = NULL, *fMap2;
  char LineBuf[100], SenderFound[32], PidsFound[20], LangFound[20];
  char *pPid, *pLng, *p = NULL;
  word Sid, PPid, VPid, APid = 0, TPid = 0, SPid = 0, curPid;
  int APidStr = 0, ALangStr = 0, k;
  word CandidateFound = FALSE, SidFound = 0, PMTFound = 0;

//  strncpy(LineBuf, ExePath, sizeof(LineBuf));
  FindExePath(ExePath, LineBuf, sizeof(LineBuf));
  k = (int)strlen(LineBuf);

  if (HumaxSource)
  {
    p = strstr(RecFileIn, "Humax-");
    if (p && *p && (p[6] == '1' || p[6] == '2' || p[6] == '3' || (p[6] == '4' && ((strncmp(strchr(RecFileIn, '_'), "_070910", 7) < 0) || (strncmp(strchr(RecFileIn, '_'), "_070923", 7) > 0)))))
    {
      strncpy(&LineBuf[k], "SenderMap_Humax1-4.txt", (int)sizeof(LineBuf) - k - 1);
      LineBuf[sizeof(LineBuf) - 1] = '\0';
      fMap = fopen(LineBuf, "rb");
    }
  }
  else if (MedionMode)
  {
    strncpy(&LineBuf[k], "SenderMap_Medion.txt", (int)sizeof(LineBuf) - k - 1);
    LineBuf[sizeof(LineBuf) - 1] = '\0';
    fMap = fopen(LineBuf, "rb");
  }
  if (!fMap)
  {
    strncpy(&LineBuf[k], "SenderMap.txt", (int)sizeof(LineBuf) - k - 1);
    LineBuf[sizeof(LineBuf) - 1] = '\0';
    fMap = fopen(LineBuf, "rb");
  }

  if (fMap)
  {
    // bei Humax: zuerst Sender aus Dateinamen ermitteln
    if (UseHumaxMap && HumaxSource && InOutServiceName)
    {
      strncpy(&LineBuf[k], "HumaxMap.txt", (int)sizeof(LineBuf) - k - 1);
      if ((fMap2 = fopen(LineBuf, "rb")))
      {
        int len;
        if ((p = strrchr(RecFileIn, '/'))) p++;
        else if ((p = strrchr(RecFileIn, '\\'))) p++;
        else p = RecFileIn;
        len = (int)strlen(p);
        while (fgets(LineBuf, sizeof(LineBuf), fMap2) != 0)
        {
          if (strncmp(LineBuf, p, len) == 0)
          {
            // Remove line breaks in the end
            k = (int)strlen(LineBuf);
            while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
              LineBuf[--k] = '\0';
            strncpy(InOutServiceName, &LineBuf[len+1], sizeof(((TYPE_Service_Info*)NULL)->ServiceName));
            KeepHumaxSvcName = TRUE;
            break;
          }
        }
        fclose(fMap2);
      }
    }

    while (fgets(LineBuf, sizeof(LineBuf), fMap) != 0)
    {
      if(LineBuf[0] == '#') continue;

      // Remove line breaks in the end
      k = (int)strlen(LineBuf);
      while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
        LineBuf[--k] = '\0';

      Sid = 0; PPid = 0; VPid = 0; APidStr = 0; ALangStr = 0; APid = 0; TPid = 0; SPid = 0;
      if (sscanf(LineBuf, "%hu ; %hu ; %hu ; %n %*19[^;] ; %n %*19[^;] ; %hu ; %hu ; %*70[^;\r\n]", &Sid, &PPid, &VPid, &APidStr, &ALangStr, &TPid, &SPid) >= 3)
      {
        if(APidStr) APid = (word) strtol(&LineBuf[APidStr], NULL, 10);
        p = strrchr(LineBuf, ';') + 1;
        if ((VPid == VidPID) && ((APid == AudPID) || !AudPID || AudPID == (word)-1) && ((TPid == TtxPID) || !TtxPID || TtxPID == (word)-1 || !TPid)
         && ((VidPID != 101 && VidPID != 201 && VidPID != 255 && VidPID != 301 && VidPID != 401 && VidPID != 501 && VidPID != 511 && VidPID != 601) || (InOutServiceName && *InOutServiceName && ((strncasecmp(p, InOutServiceName, 2) == 0) || (strncmp(p, "Das", 3)==0 && strncmp(InOutServiceName, "ARD", 3)==0) || (strncmp(p, "BR", 2)==0 && strncmp(InOutServiceName, "Bay", 3)==0) || (strncmp(p, "hr", 2)==0 && strncasecmp(InOutServiceName, "hes", 3)==0)))))
        {
          strncpy(SenderFound, p, sizeof(SenderFound));
          SenderFound[sizeof(SenderFound)-1] = '\0';
          strncpy(PidsFound, &LineBuf[APidStr], sizeof(PidsFound));
          if ((p = strchr(PidsFound, ';'))) p[0] = '\0';
          strncpy(LangFound, &LineBuf[ALangStr], sizeof(LangFound));
          if(CandidateFound) printf("\n");
          printf("  Found ServiceID in Map: SID=%hu (%s), PMTPid=%hu", Sid, SenderFound, PPid);
          if(CandidateFound) { printf(" (ambiguous) -> skipping\n"); return 1; }
          SidFound = Sid;
          PMTFound = PPid;
          CandidateFound = TRUE;
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

          if(OutPMTPID && (!*OutPMTPID || *OutPMTPID==100 || *OutPMTPID==256)) *OutPMTPID = PMTFound;
          if(InOutServiceName /*&& !*InOutServiceName*/) strncpy(InOutServiceName, SenderFound, sizeof(((TYPE_Service_Info*)NULL)->ServiceName));
          KeepHumaxSvcName = TRUE;
          fclose(fMap);
          return SidFound;
        }
      }
    }
    fclose(fMap);
  }
  return 1;
}

bool GetEPGFromMap(char *VidFileName, word ServiceID, word *OutTransportID, TYPE_RecHeader_TMSS *RecInf)
{
  FILE *fMap, *fRefEPG = NULL;
  char DescStr[FBLIB_DIR_SIZE];
  time_t StartTime = 0;
  unsigned int StartYear=0, StartMonth, StartDay, StartHour, StartMin, DurationH, DurationM, n0=0, n1, n2, n3;
  int ReadBytes, k;
  bool RefEPGMedion = FALSE, ret = FALSE;
  char *LineBuf = (char*) malloc(4096), *p;
  memset(DescStr, 0, sizeof(DescStr));

  if (LineBuf)
  {
    FindExePath(ExePath, LineBuf, 2048);
    k = (int)strlen(LineBuf);
    strncpy(&LineBuf[k], "EPGMap.txt", 2048 - k - 1);
    LineBuf[2048 - 1] = '\0';

    if ((fMap = fopen(LineBuf, "rb")))
    {
      int len_dir = 0, len_name;
      if ((p = strrchr(VidFileName, '/'))) p++;
      else if ((p = strrchr(VidFileName, '\\'))) p++;
      
      if(p)
      {
        if ((VidFileName[0] == '/') || (VidFileName[1] == ':') || (VidFileName[0]=='\\' && VidFileName[1]=='\\'))
        {
          len_dir = (int)(p-VidFileName);
          strncpy(DescStr, VidFileName, min(len_dir, (int)sizeof(DescStr)));
        }
      }
      else p = VidFileName;

      #ifdef _WIN32
        StrToUTF8(&DescStr[len_dir], p, max((int)sizeof(DescStr)-len_dir, 0), 15);
      #else
        strncpy(&DescStr[len_dir], p, max((int)sizeof(DescStr)-len_dir-1, 0));
      #endif
      VidFileName = &DescStr[len_dir];
      len_name = (int)strlen(VidFileName);

      while (fgets(LineBuf, 4096, fMap) != 0)
      {
        if(LineBuf[0] == '#') continue;
        if (strncmp(LineBuf, VidFileName, len_name) == 0)
        {
          StartYear=0; StartMonth=0, StartDay=0, StartHour=0; StartMin=0; DurationH=0; DurationM=0; n1=0; n2=0; n3=0;
          printf("  Found EPGEvent in Map:\n");
          memset(RecInf->EventInfo.EventNameDescription, 0, sizeof(RecInf->EventInfo.EventNameDescription));
          memset(RecInf->ExtEventInfo.Text, 0, sizeof(RecInf->ExtEventInfo.Text));
          RecInf->ExtEventInfo.TextLength = 0;

          if ((p = strchr(&LineBuf[len_name+1], ';')))
          {
            n0 = (unsigned int)(p - &LineBuf[len_name+1]);
            *p = '\0';
            if ((LineBuf[len_name+1] != '\0') && (LineBuf[len_name+1] != ';') && (LineBuf[len_name+1] != '-'))
            {
              if (LineBuf[p-LineBuf-2] == ':')
              {
                RefEPGMedion = LineBuf[p-LineBuf-1] - '0';
                LineBuf[p-LineBuf-2] = '\0';
              }
              if (strncmp(&LineBuf[p - LineBuf - (RefEPGMedion ? 10 : 8)], "_epg.txt", 8) == 0)
                if(!RefEPGMedion) RefEPGMedion = 1;
              #ifdef _WIN32
                strncpy(&DescStr[len_dir], &LineBuf[len_name+1], sizeof(DescStr)-len_dir-1);
              #else
                StrToUTF8(&DescStr[len_dir], &LineBuf[len_name+1], sizeof(DescStr)-len_dir, 15);
              #endif
              if ((fRefEPG = fopen(DescStr, "rb")))
                printf("    Loading EIT event from reference file '%s' (%d)...\n", DescStr, RefEPGMedion);
              else
                printf("    Failed to open reference file '%s' (%d)!\n", DescStr, RefEPGMedion);
            }
            *p = ';';
          }
          else continue;

          if (!fRefEPG)
          {
            DescStr[0] = '\0';
            if (sscanf(p+1, " %4u-%2u-%2u %2u:%2u ; %2u:%2u ; %n%256[^;] ; %n%256[^;]; %n", &StartYear, &StartMonth, &StartDay, &StartHour, &StartMin, &DurationH, &DurationM, &n1, RecInf->EventInfo.EventNameDescription, &n2, DescStr, &n3) >= 7)
            {
              StartTime = MakeUnixTime((word)StartYear, (byte)StartMonth, (byte)StartDay, (byte)StartHour, (byte)StartMin, 0, NULL);
              if (RecInf && StartYear)
              {
                RecInf->EventInfo.ServiceID = ServiceID;
                RecInf->EventInfo.EventID = 1;
                RecInf->EventInfo.RunningStatus = 4;
                RecInf->EventInfo.StartTime = Unix2TFTime(StartTime, NULL, FALSE);
                printf("    EvtStart  = %s (UTC)\n", TimeStrTF(RecInf->EventInfo.StartTime, 0));
                RecInf->EventInfo.DurationHour = (byte)DurationH;
                RecInf->EventInfo.DurationMin = (byte)DurationM;
                printf("    EvtDuration = %02hhu:%02hhu\n", DurationH, DurationM);
                RecInf->EventInfo.EndTime = AddTimeSec(RecInf->EventInfo.StartTime, 0, NULL, 3600*DurationH + 60*DurationM);
                if(strcmp(RecInf->EventInfo.EventNameDescription, "-") == 0)  RecInf->EventInfo.EventNameDescription[0] = '\0';
                RecInf->EventInfo.EventNameLength = (byte)strlen(RecInf->EventInfo.EventNameDescription);
                printf("    EventName = %s\n", RecInf->EventInfo.EventNameDescription);
                if (((unsigned int)RecInf->EventInfo.EventNameLength + 1 < sizeof(RecInf->EventInfo.EventNameDescription)) && (strcmp(DescStr, "-") != 0))
                  strncpy(&RecInf->EventInfo.EventNameDescription[RecInf->EventInfo.EventNameLength], DescStr, sizeof(RecInf->EventInfo.EventNameDescription) - RecInf->EventInfo.EventNameLength-1);
                else
                  RecInf->EventInfo.EventNameDescription[RecInf->EventInfo.EventNameLength + 1] = '\0';
                printf("    EventDesc = %s\n", DescStr);

                // Remove line breaks in the end
                k = (int)strlen(LineBuf);
                while (k && (LineBuf[k-1] == '\r' || LineBuf[k-1] == '\n' || LineBuf[k-1] == ';'))
                  LineBuf[--k] = '\0';

                k = (int)strlen(&LineBuf[len_name + n0 + (max(max(n1+2, n2+1), n3)) + 2]);
                if(ExtEPGText) free(ExtEPGText);
                if (!(ExtEPGText = (char*) malloc(k + 1)))
                {
                  printf("  Could not allocate memory for ExtEPGText.\n");
                  free(LineBuf);
                  return FALSE;
                }
                strncpy(ExtEPGText, &LineBuf[len_name + n0 + (max(max(n1+2, n2+1), n3)) + 2], k);
                ExtEPGText[k] = '\0';
                strncpy(RecInf->ExtEventInfo.Text, ExtEPGText, sizeof(RecInf->ExtEventInfo.Text));
                if (RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] != '\0')
                  strncpy(&RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 4], "...", 4);
                RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] = '\0';
                RecInf->ExtEventInfo.TextLength = (word)strlen(RecInf->ExtEventInfo.Text);
                if (RecInf->ExtEventInfo.TextLength > 0)
                  RecInf->ExtEventInfo.ServiceID = ServiceID;
                printf("    EPGExtEvt = %s\n", ExtEPGText);
              }
              if ((p = strchr(&LineBuf[len_name+1], ';')))
                *p = '\0';
              ret = TRUE;
            }
            break;
          }
        }
      }
      fclose(fMap);
    }

    if (fRefEPG)
    {
      if (RefEPGMedion)
      {
        tPVRTime MidTimeUTC = Unix2TFTime(TF2UnixTime(RecInf->RecHeaderInfo.StartTime, RecInf->RecHeaderInfo.StartTimeSec, TRUE) + 30*RecInf->RecHeaderInfo.DurationMin + RecInf->RecHeaderInfo.DurationSec/2, NULL, FALSE);
        int i = 0;
        memset(LineBuf, 0, 4096);

        while ((fread(LineBuf, 1, 8, fRefEPG) == 8) && (*(int*)LineBuf == 0x12345678))
        {
          int EITLen = *(int*)(&LineBuf[4]);

          if ((ReadBytes = (int)fread(LineBuf, 1, min(EITLen + 53, 4096), fRefEPG)) > 0)
          {
            if(++i >= RefEPGMedion)
            {
              // Dirty hack: Hier die ServiceID im EIT-Paket an die Aufnahme anpassen (z.B. arte -> arte HD)
              // Entfernt, denn: In vielen Fällen (z.B. Humax) ist die ServiceID gar nicht bekannt!
/*              tTSEIT *eit = (tTSEIT*) LineBuf;
              if ((eit->TableID == 0x4e) && (eit->ServiceID1*256 + eit->ServiceID2 != ServiceID))
              {
                printf("  GetEPGFromMap: Changing ServiceID from %hu to %hu.\n", eit->ServiceID1*256 + eit->ServiceID2, ServiceID);
                eit->ServiceID1 = (byte)(ServiceID >> 8);
                eit->ServiceID2 = (byte)(ServiceID & 0xff);
                eit->TS_ID1 = (byte)(TransportStreamID >> 8);
                eit->TS_ID2 = (byte)(TransportStreamID & 0xff);
                *(dword*)&LineBuf[eit->SectionLen1*256 + eit->SectionLen2] = crc32m_tab((byte*)eit, eit->SectionLen1*256 + eit->SectionLen2);  // testen!!
              } */
              if (AnalyseEIT((byte*)LineBuf, min(EITLen, ReadBytes), ServiceID, OutTransportID, &RecInf->EventInfo, &RecInf->ExtEventInfo, TRUE))
              {
                EPGLen = 0;
                if(EPGBuffer) { free(EPGBuffer); EPGBuffer = NULL; }
                if (EITLen && ((EPGBuffer = (byte*)malloc(EITLen + 1))))
                {
//                  RecInf->EventInfo.ServiceID = ServiceID;
//                  RecInf->ExtEventInfo.ServiceID = ServiceID;
//                  EPGBuffer[0] = 0;  // Pointer field (=0) vor der TableID (nur im ersten TS-Paket der Tabelle, gibt den Offset an, an der die Tabelle startet, z.B. wenn noch Reste der vorherigen am Paketanfang stehen)
                  memcpy(&EPGBuffer[0], LineBuf, min(EITLen, ReadBytes));
                  EPGLen = min(EITLen, ReadBytes); // + 1;
                }
              }
              if (((RefEPGMedion > 1) && (i >= RefEPGMedion)) || (RecInf->RecHeaderInfo.StartTime && /*(RecInf->EventInfo.StartTime <= MidTimeUTC) &&*/ (RecInf->EventInfo.EndTime >= MidTimeUTC)))
                break;
            }
          }
          else
            printf("    -> Loading reference EIT (Medion) failed.\n");
        }
        free(LineBuf);
      }
      else
      {
        byte Buffer[192];
        tPSBuffer EITBuffer;
        tTSPacket *CurPacket = (tTSPacket*) &Buffer[4];
        byte RefPacketSize = 0, FirstEPGPack = 0;
        
        free(LineBuf);

        if (fread(Buffer, 8, 1, fRefEPG))
        {
          if ((Buffer[0] == 'G') && (((tTSPacket*)Buffer)->PID1 == 0) && (((tTSPacket*)Buffer)->PID2 == 0))
            RefPacketSize = 188;
          else if ((CurPacket->SyncByte == 'G') && (CurPacket->PID1 == 0) && (CurPacket->PID2 == 0))
            RefPacketSize = 192;
          else
          {
            fclose(fRefEPG);
            printf("    -> Loading reference EIT from file start failed.\n");
            return ret;
          }
        }
        
        PSBuffer_Init(&EITBuffer, 0x0012, 16384, TRUE, TRUE);
        memset(CurPacket, 0, sizeof(tTSPacket));

        NrEPGPacks = 0;
        fseeko64(fRefEPG, 0, SEEK_SET);
        for (k = 0; k < 30; k++)
        {
          if (fread(&Buffer[(RefPacketSize == 188) ? 4 : 0], RefPacketSize, 1, fRefEPG))
          {
            if ((CurPacket->SyncByte == 'G') && (CurPacket->PID1 == 0) && (CurPacket->PID2 == 18))
              NrEPGPacks++;
            else if (NrEPGPacks == 0)
              FirstEPGPack++;
            else
              break;
          }
        }

        if (NrEPGPacks > 0)
        {
          if(EPGPacks) { free(EPGPacks); EPGPacks = NULL; }
          if (NrEPGPacks && ((EPGPacks = (byte*)malloc(NrEPGPacks * 192))))
          {
            memset(EPGPacks, 0, NrEPGPacks * 192);
            fseeko64(fRefEPG, FirstEPGPack * RefPacketSize, SEEK_SET);
            for (k = 0; k < NrEPGPacks; k++)
            {
              if (fread(&EPGPacks[k*192 + ((RefPacketSize == 188) ? 4 : 0)], RefPacketSize, 1, fRefEPG))
              {
                // Dirty hack: Hier die ServiceID im EIT-Paket an die Aufnahme anpassen (z.B. arte -> arte HD)
                // Entfernt, denn: In vielen Fällen (z.B. Humax) ist die ServiceID gar nicht bekannt!
/*                tTSPacket *pack = (tTSPacket*) &EPGPacks[k*192 + 4];
                tTSEIT *eit = (tTSEIT*) &pack->Data[1];
                if ((k == 0) && (eit->TableID == 0x4e) && (eit->ServiceID1*256 + eit->ServiceID2 != ServiceID))
                {
                  printf("  GetEPGFromMap: Changing ServiceID from %hu to %hu.\n", eit->ServiceID1*256 + eit->ServiceID2, ServiceID);
                  eit->ServiceID1 = (byte)(ServiceID >> 8);
                  eit->ServiceID2 = (byte)(ServiceID & 0xff);
                  eit->TS_ID1 = (byte)(TransportStreamID >> 8);
                  eit->TS_ID2 = (byte)(TransportStreamID & 0xff);
                } */
                PSBuffer_ProcessTSPacket(&EITBuffer, (tTSPacket*) (&EPGPacks[k*192 + 4]));
              }
            }
            if (AnalyseEIT(EITBuffer.Buffer1, EITBuffer.ValidBufLen, ServiceID, OutTransportID, &RecInf->EventInfo, &RecInf->ExtEventInfo, TRUE))
            {
/*              tTSPacket *pack = (tTSPacket*) &EPGPacks[(NrEPGPacks-1)*192 + 4];
              tTSEIT *eit = (tTSEIT*) EITBuffer.Buffer1;
              *(dword*)&pack->Data[(eit->SectionLen1*256 + eit->SectionLen2 - 183) % 184] = crc32m_tab((byte*)eit, eit->SectionLen1*256 + eit->SectionLen2); */
            }
            else printf("    -> Loading reference EIT from file start failed.\n");
          }
        }
        PSBuffer_Reset(&EITBuffer);
      }
      fclose(fRefEPG);
    }
/*    else if (StartYear)
    {
      GenerateEIT(ServiceID, StartTime, DurationH, DurationM, RecInf->EventInfo.EventNameDescription, RecInf->EventInfo.EventNameLength, DescStr, strlen(DescStr), ExtEPGText, strlen(ExtEPGText), RecInf->ServiceInfo.AudioStreamType);
      free(LineBuf);
    } */
    else
      free(LineBuf);
  }
  return ret;
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
  int                   i, j, k, n=0;
  bool                  ret = TRUE;

  TRACEENTER;
//  InitInfStruct(RecInf);
  KeepHumaxSvcName = FALSE;

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
          RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG1;
          RecInf->ServiceInfo.AudioTypeFlag   = 0;
          RecInf->RecHeaderInfo.StartTime     = DATE(HumaxHeader.Allgemein.Datum, HumaxHeader.Allgemein.Zeit / 60, HumaxHeader.Allgemein.Zeit % 60);
          RecInf->RecHeaderInfo.DurationMin   = (word)(HumaxHeader.Allgemein.Dauer / 60);
          RecInf->RecHeaderInfo.DurationSec   = (word)(HumaxHeader.Allgemein.Dauer % 60);
          ContinuityPIDs[0] = VideoPID;
          printf("    PMTPID=%hd, SID=%hu, PCRPID=%hd, Stream=0x%hhx, VPID=%hd, APID=%hd, TtxPID=%hd\n", RecInf->ServiceInfo.PMTPID, RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID, RecInf->ServiceInfo.VideoStreamType, VideoPID, HumaxHeader.Allgemein.AudioPID, TeletextPID);

          printf("    Start Time: %s\n", TimeStrTF(RecInf->RecHeaderInfo.StartTime, 0));

          if(p) *p = '\0';
          StrToUTF8(FirstSvcName, HumaxHeader.Allgemein.Dateiname, sizeof(FirstSvcName), 0);
//          FirstSvcName[sizeof(FirstSvcName)-1] = '\0';
// manuelle Ausnahme (IMGARTENEDEN2_0601112255.vid), da falscher Teletext bei ZDFdoku:
//if(VideoPID == 660 && TeletextPID == 130 && ExtractTeletext && DoStrip) RemoveTeletext = TRUE;
        }
        else if (i == 2)  // Header 2: Original-Dateiname
        {
          printf("    Orig Rec Name: %s\n", HumaxHeader.Allgemein.Dateiname);
          if(p) *p = '\0';
          if (strcmp(HumaxHeader.Allgemein.Dateiname, FirstSvcName) != 0)
          {
            StrToUTF8(RecInf->ServiceInfo.ServiceName, HumaxHeader.Allgemein.Dateiname, sizeof(RecInf->ServiceInfo.ServiceName), 0);
//            RecInf->ServiceInfo.ServiceName[sizeof(RecInf->ServiceInfo.ServiceName)-1] = '\0';
          }
          else
            printf("    Assertion error: Humax rec name without sender!\n");
        }
        else if (HumaxHeader.ZusInfoID == HumaxBookmarksID)  // Header 3: Bookmarks
        {
          tHumaxBlock_Bookmarks* HumaxBookmarks = (tHumaxBlock_Bookmarks*)HumaxHeader.ZusInfos;
          RecInf->BookmarkInfo.NrBookmarks = HumaxBookmarks->Anzahl;
          printf("    Bookmarks: %s", (HumaxBookmarks->Anzahl == 0) ? "-" : "");
          for (j = 0; j < HumaxBookmarks->Anzahl; j++)
          {
            RecInf->BookmarkInfo.Bookmarks[n++] = (dword) ((long long)HumaxBookmarks->Items[j] * 32768 / 9024);
            printf((j > 0) ? ", %u" : "%u", HumaxBookmarks->Items[j]);
          }
          printf("\n");
        }
        if ((i == 4) || (HumaxHeader.ZusInfoID == HumaxTonSpurenID))  // Header 4: Tonspuren
        {
          AudioPIDs[0].pid = HumaxHeader.Allgemein.AudioPID;
          AudioPIDs[0].streamType = STREAMTYPE_AUDIO;
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
                  AudioPIDs[k].streamType = STREAMTYPE_AUDIO;
                  AudioPIDs[k].sorted = TRUE;
                  strncpy(AudioPIDs[k].desc, HumaxTonspuren->Items[j].Name, 3);
                }
              }
              AddContinuityPids(HumaxTonspuren->Items[j].PID, FALSE);
            }
          }
          if(!*AudioPIDs[0].desc) strncpy(AudioPIDs[0].desc, "deu", 4);
        }
      }
    }
  }
  RecInf->ServiceInfo.ServiceID = GetSidFromMap(VideoPID, 0 /*GetMinimalAudioPID(AudioPIDs)*/, 0, RecInf->ServiceInfo.ServiceName, &RecInf->ServiceInfo.PMTPID, TRUE);  // erster Versuch - mit Humax Map, Teletext folgt
  if (!RecInf->ServiceInfo.PMTPID)
    RecInf->ServiceInfo.PMTPID = (HumaxHeader.Allgemein.AudioPID != 256) ? 256 : 100;
  AddContinuityPids(TeletextPID, FALSE);

  for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != TeletextPID); k++);
  if (k < MAXCONTINUITYPIDS)
  {
    AudioPIDs[k].pid = TeletextPID;
    AudioPIDs[k].sorted = TRUE;
  }

  CutImportFromBM(NULL, RecInf->BookmarkInfo.Bookmarks, RecInf->BookmarkInfo.NrBookmarks);

//  fseeko64(fIn, FilePos, SEEK_SET);
  if (!ret)
    printf("  Failed to read the Humax header from rec.\n");
  TRACEEXIT;
  return ret;
}
