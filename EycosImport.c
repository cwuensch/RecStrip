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
#include <time.h>
#include "type.h"
#include "RecStrip.h"
#include "RecHeader.h"
#include "RebuildInf.h"
#include "TtxProcessor.h"
#include "CutProcessor.h"
#include "EycosHeader.h"
#include "HumaxHeader.h"


char* EycosGetPart(char *const OutEycosPart, const char* AbsTrpName, int NrPart)
{
  TRACEENTER;
  if (OutEycosPart && AbsTrpName && *AbsTrpName)
  {
    strcpy(OutEycosPart, AbsTrpName);
    if (NrPart > 0)
      snprintf(&OutEycosPart[strlen(OutEycosPart)-4], 5, ".%03u", NrPart % 1000);
  }
  return OutEycosPart;
  TRACEEXIT;
}

int EycosGetNrParts(const char* AbsTrpName)
{
  char CurFileName[FBLIB_DIR_SIZE];
  int i = 0;

  TRACEENTER;
  if (AbsTrpName && *AbsTrpName)
  {
    strcpy(CurFileName, AbsTrpName);
    for (i = 1; i < 999; i++)
    {
      snprintf(&CurFileName[strlen(CurFileName)-4], 5, ".%03u", i);
      if(!HDD_FileExist(CurFileName)) break;
    }
  }
  return i;
  TRACEEXIT;
}

bool LoadEycosHeader(char *AbsTrpFileName, TYPE_RecHeader_TMSS *RecInf)
{
  FILE                 *fIfo, *fTxt, *fIdx;
  char                  IfoFile[FBLIB_DIR_SIZE], TxtFile[FBLIB_DIR_SIZE], IdxFile[FBLIB_DIR_SIZE], *p;
  tEycosHeader          EycosHeader;
  tEycosEvent           EycosEvent;
  word                  AudioPID = (word) -1;
  int                   j, k, l;
  bool                  ret = FALSE;

  TRACEENTER;
//  InitInfStruct(RecInf);

  // Zusatz-Dateien von Eycos laden
  strcpy(IfoFile, AbsTrpFileName);
  if((p = strrchr(IfoFile, '.'))) *p = '\0';  // ".trp" entfernen
  strcpy(TxtFile, IfoFile);
  strcpy(IdxFile, IfoFile);
  strcat(IfoFile, ".ifo");
  strcat(TxtFile, ".txt");
  strcat(IdxFile, ".idx");

  // Ifo-Datei laden
  if ((fIfo = fopen(IfoFile, "rb")))
  {
    ret = TRUE;
    if ((ret = (ret && fread(&EycosHeader, sizeof(tEycosHeader), 1, fIfo))))
    {
      printf("  Importing Eycos header\n");
      VideoPID              = EycosHeader.VideoPid;
      AudioPID              = EycosHeader.AudioPid;
      AudioPIDs[0].pid      = AudioPID;
      TeletextPID           = (word) -1;
      isHDVideo             = FALSE;

      RecInf->TransponderInfo.Frequency = EycosHeader.Frequency;
      RecInf->ServiceInfo.ServiceType     = 0;  // SVC_TYPE_Tv
      RecInf->ServiceInfo.ServiceID       = EycosHeader.ServiceID;
      RecInf->ServiceInfo.PMTPID          = EycosHeader.PMTPid;
      RecInf->ServiceInfo.VideoPID        = VideoPID;
      RecInf->ServiceInfo.PCRPID          = VideoPID;
      RecInf->ServiceInfo.AudioPID        = AudioPID;
      RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG1;
      RecInf->ServiceInfo.AudioTypeFlag   = 0;
      RecInf->RecHeaderInfo.StartTime     = 0;  // TODO
//      RecInf->RecHeaderInfo.DurationMin   = 0;  // TODO
//      RecInf->RecHeaderInfo.DurationSec   = 0;  // TODO
      ContinuityPIDs[0] = VideoPID;
      printf("    PMTPID=%hd, SID=%hu, PCRPID=%hd, Stream=0x%hhx, VPID=%hd, APID=%hd, TtxPID=%hd\n", RecInf->ServiceInfo.PMTPID, RecInf->ServiceInfo.ServiceID, RecInf->ServiceInfo.PCRPID, RecInf->ServiceInfo.VideoStreamType, VideoPID, AudioPID, TeletextPID);

      StrToUTF8(RecInf->ServiceInfo.ServiceName, EycosHeader.SenderName, sizeof(RecInf->ServiceInfo.ServiceName), 0);
      printf("    ServiceName=%s\n", RecInf->ServiceInfo.ServiceName);

      // Bookmarks importieren
      ResetSegmentMarkers();
      SegmentMarker[NrSegmentMarker++].Timems = 0;
      for (j = 0; j < min(EycosHeader.NrBookmarks, NRSEGMENTMARKER); j++)
      {
        if (EycosHeader.Bookmarks[j] == 0) break;
        if (EycosHeader.Bookmarks[j] > SegmentMarker[NrSegmentMarker].Timems)
          SegmentMarker[NrSegmentMarker++].Timems = EycosHeader.Bookmarks[j];
        else
          printf("  Eycos-Import: Invalid Bookmark %d: %u is less than %u.\n", j, EycosHeader.Bookmarks[j], SegmentMarker[NrSegmentMarker].Timems);
      }
      SegmentMarker[NrSegmentMarker++].Position = RecFileSize;

      AudioPIDs[0].pid = EycosHeader.AudioPid;
      AudioPIDs[0].sorted = TRUE;

      // PIDs durchgehen
      for (j = 0; j < (int)EycosHeader.NrPids; j++)
      {
        switch (EycosHeader.Pids[j].Type)
        {
          // Video
          case 0x0b:
            if (EycosHeader.Pids[j].PID == VideoPID)  // fall-through
            {
              isHDVideo = TRUE;  // fortsetzen...
              RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG4_H264;
            }
            // (fall-through!)

          case STREAM_VIDEO_MPEG1:
          case STREAM_VIDEO_MPEG2:
          {
            if (RecInf->ServiceInfo.VideoStreamType == 0xff)
            {
              if (EycosHeader.Pids[j].PID == VideoPID)
                RecInf->ServiceInfo.VideoStreamType = STREAM_VIDEO_MPEG2;
            }
            printf("    Video Stream: PID=%hd, Type=0x%hhx, HD=%d\n", VideoPID, RecInf->ServiceInfo.VideoStreamType, isHDVideo);
            if (EycosHeader.Pids[j].PID != VideoPID)
              printf("  Eycos-Import: Video stream (PID=%hd, Type=0x%hx) differs from Video PID %hd.\n", EycosHeader.Pids[j].PID, EycosHeader.Pids[j].Type, VideoPID);
            break;
          }

          // Audio normal / AC3
          case STREAM_AUDIO_MPEG1:
          case STREAM_AUDIO_MPEG2:
          case 0x05:
          case 0x0a:
          {
            int NrAudio = 0, start;  // (EycosHeader.NrPids > 2) ? (int)EycosHeader.NrPids - 2 : 0;  // (int) strlen((char*)EycosHeader.AudioNames) / 2;
            
            for (k = 0; k < (int)EycosHeader.NrPids; k++)
              if (EycosHeader.Pids[k].Type==STREAM_AUDIO_MPEG1 || EycosHeader.Pids[k].Type==STREAM_AUDIO_MPEG2 || EycosHeader.Pids[k].Type==0x05 || EycosHeader.Pids[k].Type==0x0a)
                NrAudio++;

            if ((EycosHeader.Pids[j].PID == AudioPID) && (EycosHeader.Pids[j].Type == 0x0a))
            {
              RecInf->ServiceInfo.AudioStreamType = STREAM_AUDIO_MPEG4_AC3_PLUS;
              RecInf->ServiceInfo.AudioTypeFlag = 1;
            }

            for (k = 0; (k < MAXCONTINUITYPIDS) && (AudioPIDs[k].pid != 0) && (AudioPIDs[k].pid != EycosHeader.Pids[j].PID); k++);
            if (k < MAXCONTINUITYPIDS)
            {
              AudioPIDs[k].pid = EycosHeader.Pids[j].PID;
//              AudioPIDs[k].streamType = STREAMTYPE_AUDIO;
              AudioPIDs[k].type = (EycosHeader.Pids[j].Type == 0x0a) ? STREAM_AUDIO_MPEG4_AC3_PLUS : STREAM_AUDIO_MPEG1;
              AudioPIDs[k].sorted = TRUE;
            }

            start = max(NrAudio*2, 6);
            while(EycosHeader.AudioNames[start] < 65) start++;
            for (l = 0; l < NrAudio; l++)
            {
              if (*((word*) &EycosHeader.AudioNames[2*l]) == EycosHeader.Pids[j].PID)
              {
                strncpy(AudioPIDs[k].desc, &EycosHeader.AudioNames[start + l*4], 3);
                break;
              }
            }
            printf("    Audio Track %d: PID=%d, Type=0x%x [%s]\n", k+1, AudioPIDs[k].pid, AudioPIDs[k].type, AudioPIDs[k].desc);
            
            AddContinuityPids(EycosHeader.Pids[j].PID, FALSE);
            break;
          }

          // Ignorieren
          case 0x09: break;

          // Anderes
          default:
          {
            printf("  Eycos-Import: Unknown elementary stream type 0x%hx (PID=%hd)\n", EycosHeader.Pids[j].Type, EycosHeader.Pids[j].PID);
            break;
          }
        }
      }
    }
    fclose(fIfo);
  }

  // Txt-Datei laden
  if ((fTxt = fopen(TxtFile, "rb")))
  {
    if ((ret = (ret && fread(&EycosEvent, sizeof(tEycosEvent), 1, fIfo))))
    {
      time_t EvtStartUnix, EvtEndUnix;
      int TextLen;
      RecInf->EventInfo.ServiceID = RecInf->ServiceInfo.ServiceID;

//      memset(RecInf->EventInfo.EventNameDescription, 0, sizeof(RecInf->EventInfo.EventNameDescription));
//      memset(RecInf->ExtEventInfo.Text, 0, sizeof(RecInf->ExtEventInfo.Text));
      EycosEvent.Title[sizeof(EycosEvent.Title) - 1] = '\0';
      StrToUTF8(RecInf->EventInfo.EventNameDescription, EycosEvent.Title, sizeof(RecInf->EventInfo.EventNameDescription), 0);
//      RecInf->EventInfo.EventNameDescription[NameLen] = '\0';
      printf("    EventName = %s\n", RecInf->EventInfo.EventNameDescription);

      TextLen = (int)strlen(RecInf->EventInfo.EventNameDescription);
      RecInf->EventInfo.EventNameLength = TextLen;
      EycosEvent.ShortDesc[sizeof(EycosEvent.ShortDesc) - 1] = '\0';
      StrToUTF8(&RecInf->EventInfo.EventNameDescription[TextLen], EycosEvent.ShortDesc, sizeof(RecInf->EventInfo.EventNameDescription) - TextLen, 0);
//      RecInf->EventInfo.EventNameDescription[sizeof(RecInf->EventInfo.EventNameDescription) - 1] = '\0';
      printf("    EventDesc = %s\n", &RecInf->EventInfo.EventNameDescription[TextLen]);

      if(ExtEPGText) free(ExtEPGText);
      if ((ExtEPGText = (char*) malloc(2561)))
      {
        TextLen = 0;
        ExtEPGText[0] = '\0';
        RecInf->ExtEventInfo.ServiceID = RecInf->ServiceInfo.ServiceID;
        for (k = 0; k < 8; k++)
        {
          EycosEvent.LongDesc[k].DescBlock[sizeof(((tEycosDescBlock*)NULL)->DescBlock) - 1] = '\0';
          StrToUTF8(&ExtEPGText[TextLen], EycosEvent.LongDesc[k].DescBlock, 2560 - TextLen, 0);
          TextLen += (int)strlen(&ExtEPGText[TextLen]);
          if(EycosEvent.LongDesc[k].DescBlock[248] == '\0') break;
        }
#ifdef _DEBUG
if (strlen(ExtEPGText) != TextLen)
  printf("ASSERT: ExtEventTextLength (%d) != length of ExtEventText (%d)!\n", TextLen, strlen(ExtEPGText));
#endif
        ExtEPGText[TextLen] = '\0';
        RecInf->ExtEventInfo.TextLength = min(TextLen, (int)sizeof(RecInf->ExtEventInfo.Text) - 1);
        strncpy(RecInf->ExtEventInfo.Text, ExtEPGText, RecInf->ExtEventInfo.TextLength + 1);
        if (RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] != 0)
          snprintf(&RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 4], 4, "...");
        RecInf->ExtEventInfo.Text[sizeof(RecInf->ExtEventInfo.Text) - 1] = '\0';
        printf("    EPGExtEvt = %s\n", ExtEPGText);
      }
      else
      {
        printf("Could not allocate memory for ExtEPGText.\n");
        ret = FALSE;
      }

      EvtStartUnix = MakeUnixTime(EycosEvent.EvtStartYear, EycosEvent.EvtStartMonth, EycosEvent.EvtStartDay, EycosEvent.EvtStartHour, EycosEvent.EvtStartMin, 0, NULL);
      EvtEndUnix = MakeUnixTime(EycosEvent.EvtEndYear, EycosEvent.EvtEndMonth, EycosEvent.EvtEndDay, EycosEvent.EvtEndHour, EycosEvent.EvtEndMin, 0, NULL);
      RecInf->EventInfo.StartTime       = Unix2TFTime(EvtStartUnix, NULL, FALSE);  // DATE(UnixToMJD(EvtStartUnix), EycosEvent.EvtStartHour, EycosEvent.EvtStartMin);  // kein Convert, da ins EPG UTC geschrieben wird
      RecInf->EventInfo.EndTime         = Unix2TFTime(EvtEndUnix, NULL, FALSE);    // DATE(UnixToMJD(EvtEndUnix), EycosEvent.EvtEndHour, EycosEvent.EvtEndMin);        // "
//      RecInf->RecHeaderInfo.StartTime   = Unix2TFTime(EvtStartUnix, NULL, TRUE);   // Convert, da EvtStartUnix als UTC-Timestamp geparsed wurde
#ifdef _DEBUG
/*if (HOUR(RecInf->RecHeaderInfo.StartTime) != EycosEvent.EvtStartHour || MINUTE(RecInf->RecHeaderInfo.StartTime) != EycosEvent.EvtStartMin)
  printf("ASSERT: Eycos Header StartTime (%u:%u) differs from converted time (%u:%u)!\n", EycosEvent.EvtStartHour, EycosEvent.EvtStartMin, HOUR(RecInf->RecHeaderInfo.StartTime), MINUTE(RecInf->RecHeaderInfo.StartTime)); */
//if (HOUR(RecInf->EventInfo.EndTime) != EycosEvent.EvtEndHour || MINUTE(RecInf->EventInfo.EndTime) != EycosEvent.EvtEndMin)
//  printf("ASSERT: Eycos Header EndTime (%u:%u) differs from converted time (%u:%u)!\n", EycosEvent.EvtEndHour, EycosEvent.EvtEndMin, HOUR(RecInf->EventInfo.EndTime), MINUTE(RecInf->EventInfo.EndTime));
#endif
//      RecInf->RecHeaderInfo.DurationMin = (word)((EvtEndUnix - EvtStartUnix) / 60);
      RecInf->EventInfo.DurationHour    = (byte)((EvtEndUnix - EvtStartUnix) / 3600);
      RecInf->EventInfo.DurationMin     = (word)(((EvtEndUnix - EvtStartUnix) / 60) % 60);
      printf("    EvtStart  = %s (local)\n", TimeStrTF(RecInf->EventInfo.StartTime, 0));
      printf("    EvtDuratn = %02d:%02d\n", RecInf->EventInfo.DurationHour, RecInf->EventInfo.DurationMin);
    }
    fclose(fTxt);
  }

  if ((fIdx = fopen(IdxFile, "rb")))
  {
    tEycosIdxEntry EycosIdx;
    size_t ReadOk = fread(&EycosIdx, sizeof(tEycosIdxEntry), 1, fIdx);

    printf("    Bookmarks: %s", (EycosHeader.NrBookmarks == 0) ? "-" : "");
    for (j = 1; j < NrSegmentMarker-1 && ReadOk; j++)
    {
      while ((EycosIdx.Timems + 100 < SegmentMarker[j].Timems) && ReadOk)
        ReadOk = fread(&EycosIdx, sizeof(tEycosIdxEntry), 1, fIdx);
      if (ReadOk)
      {
        SegmentMarker[j].Position = EycosIdx.PacketNr * 188;
        BookmarkInfo->Bookmarks[BookmarkInfo->NrBookmarks++] = (dword)(EycosIdx.PacketNr / 48);
        printf((j > 0) ? ", %llu (%u:%02u:%02u,%03u)" : "%llu (%u:%02u:%02u,%03u)", SegmentMarker[j].Position, SegmentMarker[j].Timems/3600000, SegmentMarker[j].Timems/60000 % 60, SegmentMarker[j].Timems/1000 % 60, SegmentMarker[j].Timems % 1000);
      }
    }
    fseeko64(fIdx, -1 * (int)sizeof(tEycosIdxEntry), SEEK_END);
    if (fread(&EycosIdx, sizeof(tEycosIdxEntry), 1, fIdx))
    {
      RecInf->RecHeaderInfo.DurationMin = (word)(EycosIdx.Timems / 60000);
      RecInf->RecHeaderInfo.DurationSec = (word)(EycosIdx.Timems / 1000) % 60;
      if (NrSegmentMarker > 2)
        SegmentMarker[NrSegmentMarker-1].Timems = EycosIdx.Timems;
    }
    fclose(fIdx);
    printf("\n");
  }

//  fseeko64(fIn, FilePos, SEEK_SET);
  if (ret)
    OutCutVersion = 4;
  else
    printf("  Failed to read the Eycos header from rec.\n");
  TRACEEXIT;
  return ret;
}
