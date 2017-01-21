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
#include "CutProcessor.h"
#include "RecStrip.h"


// Globale Variablen
static bool             WriteCutFile = TRUE, WriteCutInf = FALSE;


// ----------------------------------------------
// *****  READ AND WRITE CUT FILE  *****
// ----------------------------------------------

static void SecToTimeString(dword Time, char *const OutTimeString)  // needs max. 4 + 1 + 2 + 1 + 2 + 1 = 11 chars
{
  dword                 Hour, Min, Sec;

  TRACEENTER;
  if(OutTimeString)
  {
    Hour = (Time / 3600);
    Min  = (Time / 60) % 60;
    Sec  = Time % 60;
    if (Hour >= 10000) Hour = 9999;
    snprintf(OutTimeString, 11, "%u:%02u:%02u", Hour, Min, Sec);
  }
  TRACEEXIT;
}

static void MSecToTimeString(dword Timems, char *const OutTimeString)  // needs max. 4 + 1 + 2 + 1 + 2 + 1 + 3 + 1 = 15 chars
{
  dword                 Hour, Min, Sec, Millisec;

  TRACEENTER;
  if(OutTimeString)
  {
    Hour = (Timems / 3600000);
    Min  = (Timems / 60000) % 60;
    Sec  = (Timems / 1000) % 60;
    Millisec = Timems % 1000;
    snprintf(OutTimeString, 15, "%u:%02u:%02u,%03u", Hour, Min, Sec, Millisec);
  }
  TRACEEXIT;
}

static dword TimeStringToMSec(char *const TimeString)
{
  dword                 Hour=0, Min=0, Sec=0, Millisec=0, ret=0;

  TRACEENTER;
  if(TimeString)
  {
    if (sscanf(TimeString, "%u:%u:%u%*1[.,]%u", &Hour, &Min, &Sec, &Millisec) == 4)
      ret = 1000*(60*(60*Hour + Min) + Sec) + Millisec;
  }
  TRACEEXIT;
  return ret;
}

static void ResetSegmentMarkers()
{
  int i;
  TRACEENTER;

  for (i = 0; i < NrSegmentMarker; i++)
    if (SegmentMarker[i].pCaption)
      free(SegmentMarker[i].pCaption);
  memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));
  NrSegmentMarker = 0;

  TRACEEXIT;
}

void GetCutNameFromRec(const char *RecFileName, char *const OutCutFileName)
{
  char *p = NULL;

  TRACEENTER;
  if (RecFileName && OutCutFileName)
  {
    snprintf(OutCutFileName, FBLIB_DIR_SIZE, "%s", RecFileName);
    if ((p = strrchr(OutCutFileName, '.')) == NULL)
      p = &OutCutFileName[strlen(OutCutFileName)];
    snprintf(p, 5, ".cut");
  }
  TRACEEXIT;
}

static bool CutFileDecodeBin(FILE *fCut, unsigned long long *OutSavedSize)
{
  byte                  Version;
  int                   SavedNrSegments = 0;
  bool                  ret = FALSE;

  TRACEENTER;

  ResetSegmentMarkers();
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
        tCutHeader1 CutHeader;
        rewind(fCut);
        ret = (fread(&CutHeader, sizeof(CutHeader), 1, fCut) == 1);
        if (ret)
        {
          if (OutSavedSize) *OutSavedSize = CutHeader.RecFileSize;
          SavedNrSegments = CutHeader.NrSegmentMarker;
          ActiveSegment = CutHeader.ActiveSegment;
        }
        break;
      }
      case 2:
      {
        tCutHeader2 CutHeader;
        rewind(fCut);
        ret = (fread(&CutHeader, sizeof(CutHeader), 1, fCut) == 1);
        if (ret)
        {
          if (OutSavedSize) *OutSavedSize = CutHeader.RecFileSize;
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
      while (fread(&SegmentMarker[NrSegmentMarker], sizeof(tSegmentMarker)-4, 1, fCut))
      {
        SegmentMarker[NrSegmentMarker].pCaption = NULL;
        NrSegmentMarker++;
      }
      if (NrSegmentMarker < SavedNrSegments)
        printf("CutFileDecodeBin: Unexpected end of file!\n");
    }
  }
  TRACEEXIT;
  return ret;
}

static bool CutFileDecodeTxt(FILE *fCut, unsigned long long *OutSavedSize)
{
  char                  Buffer[4096];
  unsigned long long    SavedSize = 0;
  int                   SavedNrSegments = 0;
  bool                  HeaderMode=FALSE, SegmentsMode=FALSE;
  char                  TimeStamp[16];
  char                 *c, Selected;
  int                   p, ReadBytes;
  bool                  ret = FALSE;

  TRACEENTER;

  ResetSegmentMarkers();
  ActiveSegment = 0;
  if (OutSavedSize) *OutSavedSize = 0;

  if (fCut)
  {
    // Check the first line
    if (fgets(Buffer, sizeof(Buffer), fCut))
    {
      if (strncmp(Buffer, "[MCCut3]", 8) == 0)
      {
        HeaderMode = TRUE;
        ret = TRUE;
      }
    }

    while (ret && (fgets(Buffer, sizeof(Buffer), fCut)))
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
        long long       Value;

        if (sscanf(Buffer, "%49[^= ] = %lld", Name, &Value) == 2)
        {
          if (strcmp(Name, "RecFileSize") == 0)
          {
            SavedSize = Value;
            if (OutSavedSize) *OutSavedSize = SavedSize;
          }
          else if (strcmp(Name, "NrSegmentMarker") == 0)
            SavedNrSegments = (int)Value;
          else if (strcmp(Name, "ActiveSegment") == 0)
            ActiveSegment = (int)Value;
        }
      }

      // Segmente einlesen
      else if (SegmentsMode)
      {
        //[Segments]
        //#Nr. ; Sel ; StartBlock ; StartTime ; Percent
        if (sscanf(Buffer, "%*i ; %c ; %u ; %15[^;\r\n] ; %f%%%n", &Selected, &SegmentMarker[NrSegmentMarker].Block, TimeStamp, &SegmentMarker[NrSegmentMarker].Percent, &ReadBytes) >= 3)
        {
          SegmentMarker[NrSegmentMarker].Selected = (Selected == '*');
          SegmentMarker[NrSegmentMarker].Timems = (TimeStringToMSec(TimeStamp));
          SegmentMarker[NrSegmentMarker].pCaption = NULL;
          while (Buffer[ReadBytes] && (Buffer[ReadBytes] == ' ' || Buffer[ReadBytes] == ';'))  ReadBytes++;
          if (Buffer[ReadBytes])
          {
            SegmentMarker[NrSegmentMarker].pCaption = (char*)malloc(strlen(&Buffer[ReadBytes]) + 1);
            strcpy(SegmentMarker[NrSegmentMarker].pCaption, &Buffer[ReadBytes]);
          }
          NrSegmentMarker++;
        }
      }
    }

    if (ret)
    {
      if (NrSegmentMarker != SavedNrSegments)
        printf("CutFileDecodeTxt: Invalid number of segments read (%d of %d)!\n", NrSegmentMarker, SavedNrSegments);
    }
    else
      printf("CutFileDecodeTxt: Invalid cut file format!\n");
  }
  TRACEEXIT;
  return ret;
}

static bool CutDecodeFromBM(dword Bookmarks[])
{
  int                   End = 0, Start, i;
  bool                  ret = FALSE;

  TRACEENTER;

  ResetSegmentMarkers();
  ActiveSegment = 0;
  if (Bookmarks[NRBOOKMARKS - 2] == 0x8E0A4247)       // ID im vorletzen Bookmark-Dword (-> neues SRP-Format und CRP-Format auf SRP)
    End = NRBOOKMARKS - 2;
  else if (Bookmarks[NRBOOKMARKS - 1] == 0x8E0A4247)  // ID im letzten Bookmark-Dword (-> CRP- und altes SRP-Format)
    End = NRBOOKMARKS - 1;

  if(End)
  {
    ret = TRUE;
    NrSegmentMarker = Bookmarks[End - 1];
    if (NrSegmentMarker > NRSEGMENTMARKER) NrSegmentMarker = NRSEGMENTMARKER;

    Start = End - NrSegmentMarker - 5;
    for (i = 0; i < NrSegmentMarker; i++)
    {
      SegmentMarker[i].Block = Bookmarks[Start + i];
      SegmentMarker[i].Selected = ((Bookmarks[End-5+(i/32)] & (1 << (i%32))) != 0);
      SegmentMarker[i].Timems = 0;  // NavGetBlockTimeStamp(SegmentMarker[i].Block);
      SegmentMarker[i].Percent = 0;
      SegmentMarker[i].pCaption = NULL;
    }
  }

  TRACEEXIT;
  return ret;
}

bool CutProcessor_Init(void)
{
  TRACEENTER;

  // Puffer allozieren
  SegmentMarker = (tSegmentMarker*) malloc(NRSEGMENTMARKER * sizeof(tSegmentMarker));
  if (SegmentMarker)
  {
    memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));
    TRACEEXIT;
    return TRUE;
  }
  else
  {
    free(SegmentMarker); SegmentMarker = NULL;
    printf("CutFileLoad: Failed to allocate memory!\n");
    TRACEEXIT;
    return FALSE;
  }
}

bool CutFileLoad(const char *AbsCutName)
{
  FILE                 *fCut = NULL;
  byte                  Version;
  unsigned long long    RecFileSize, SavedSize;
  bool                  ret = FALSE;

  TRACEENTER;

  if (!SegmentMarker)
  {
    TRACEEXIT;
    return FALSE;
  }

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
    if (!ret)
      printf("CutFileLoad: Failed to read cut-info from .cut!\n");

    // Check, if size of rec-File has been changed
    if (ret)
    {
      HDD_GetFileSize(RecFileIn, &RecFileSize);
      if (RecFileSize != SavedSize)
      {
        printf("CutFileLoad: .cut file size mismatch!\n");
        ResetSegmentMarkers();
        ret = FALSE;
      }
    }
  }

  // sonst schaue in der inf
  if (!ret && BookmarkInfo)
  {
    ret = CutDecodeFromBM(BookmarkInfo->Bookmarks);
    if (ret)
    {
      WriteCutInf = TRUE;
      printf("CutFileLoad: Imported segments from Bookmark-area.\n");
    }
  }
  else if (BookmarkInfo)
    WriteCutInf = ((BookmarkInfo->Bookmarks[NRBOOKMARKS-2] == 0x8E0A4247) || (BookmarkInfo->Bookmarks[NRBOOKMARKS-1] == 0x8E0A4247));

  // Wenn zu wenig Segmente -> auf Standard zurücksetzen
  if (NrSegmentMarker < 2)
  {
    if(ret) printf("CutFileLoad: Less than two timestamps imported -> resetting!\n"); 
    ResetSegmentMarkers();
    free(SegmentMarker);
    SegmentMarker = NULL;
    ret = FALSE;
  }

  TRACEEXIT;
  return ret;
}

static bool CutEncodeToBM(dword Bookmarks[], int NrBookmarks)
{
  int                   End, Start, i;
  bool                  ret = TRUE;

  TRACEENTER;
  if (!Bookmarks)
  {
    TRACEEXIT;
    return FALSE;
  }
  memset(&Bookmarks[NrBookmarks], 0, (NRBOOKMARKS - min(NrBookmarks, NRBOOKMARKS)) * sizeof(dword));

  if (SegmentMarker && (NrSegmentMarker > 2))
  {
    End   = (SystemType == ST_TMSC) ? NRBOOKMARKS-1 : NRBOOKMARKS-2;  // neu: auf dem SRP das vorletzte Dword nehmen -> CRP-kompatibel
    Start = End - NrSegmentMarker - 5;
    if (Start >= NrBookmarks)
    {
      Bookmarks[End]   = 0x8E0A4247;  // Magic
      Bookmarks[End-1] = NrSegmentMarker;
      for (i = 0; i < NrSegmentMarker; i++)
      {
        Bookmarks[Start+i] = SegmentMarker[i].Block;
        Bookmarks[End-5+(i/32)] = (Bookmarks[End-5+(i/32)] & ~(1 << (i%32))) | (SegmentMarker[i].Selected ? 1 << (i%32) : 0);
      }
    }
    else
    {
      printf("CutEncodeToBM: Error! Not enough space to store segment markers. (NrSegmentMarker=%d, NrBookmarks=%d)\n", NrSegmentMarker, NrBookmarks);
      ret = FALSE;
    }
  }

  TRACEEXIT;
  return ret;
}

bool CutFileSave(const char* AbsCutName)
{
  FILE                 *fCut = NULL;
  char                  TimeStamp[16];
  unsigned long long    RecFileSize;
  int                   i;
  bool                  ret = TRUE;

  TRACEENTER;

  if (SegmentMarker)
  {
    if (WriteCutFile && (NrSegmentMarker > 2 || SegmentMarker[0].pCaption))
    {
      // neues CutFile speichern
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
        ret = (fprintf(fCut, "#Nr ; Sel ; StartBlock ;     StartTime ; Percent ; Caption\r\n") > 0) && ret;
        for (i = 0; i < NrSegmentMarker; i++)
        {
          MSecToTimeString(SegmentMarker[i].Timems, TimeStamp);
          ret = (fprintf(fCut, "%3d ;  %c  ; %10u ;%14s ;  %5.1f%% ; %s\r\n", i, (SegmentMarker[i].Selected ? '*' : '-'), SegmentMarker[i].Block, TimeStamp, SegmentMarker[i].Percent, (SegmentMarker[i].pCaption ? SegmentMarker[i].pCaption : "")) > 0) && ret;
        }
        ret = (fclose(fCut) == 0) && ret;
//        HDD_SetFileDateTime(&AbsCutName[1], "", 0);
      }
      else
      {
        printf("CutFileSave: failed to open .cut!\n");
        ret = FALSE;
      }
    }

    if (WriteCutInf && BookmarkInfo)
      CutEncodeToBM(BookmarkInfo->Bookmarks, BookmarkInfo->NrBookmarks);
  }
  TRACEEXIT;
  return ret;
}

void CutProcessor_Free(void)
{
  TRACEEXIT;
  if (SegmentMarker)
  {
    ResetSegmentMarkers();
    free(SegmentMarker);  SegmentMarker = NULL;
  }
  TRACEEXIT;
}