#define _LARGEFILE64_SOURCE
#define __USE_LARGEFILE64  1
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
#include "../../../../../Topfield/API/TMS/include/type.h"
#include "CutProcessor.h"
#include "RecStrip.h"


// Globale Variablen
static tSegmentMarker  *SegmentMarker = NULL;       //[0]=Start of file, [x]=End of file
static int              NrSegmentMarker = 0;
static int              ActiveSegment = 0;


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
    snprintf(OutTimeString, 11, "%lu:%02lu:%02lu", Hour, Min, Sec);
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
    snprintf(OutTimeString, 15, "%lu:%02lu:%02lu,%03lu", Hour, Min, Sec, Millisec);
  }
  TRACEEXIT;
}

static dword TimeStringToMSec(char *const TimeString)
{
  dword                 Hour=0, Min=0, Sec=0, Millisec=0, ret=0;

  TRACEENTER;
  if(TimeString)
  {
    if (sscanf(TimeString, "%lu:%lu:%lu%*1[.,]%lu", &Hour, &Min, &Sec, &Millisec) == 4)
      ret = 1000*(60*(60*Hour + Min) + Sec) + Millisec;
  }
  TRACEEXIT;
  return ret;
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
  memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));

  NrSegmentMarker = 0;
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
          *OutSavedSize = CutHeader.RecFileSize;
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
          *OutSavedSize = CutHeader.RecFileSize;
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
      NrSegmentMarker = fread(SegmentMarker, sizeof(tSegmentMarker), SavedNrSegments, fCut);
      if (NrSegmentMarker < SavedNrSegments)
        printf("CutFileDecodeBin: Unexpected end of file!\n");
    }
  }
  TRACEEXIT;
  return ret;
}

static bool CutFileDecodeTxt(FILE *fCut, unsigned long long *OutSavedSize)
{
  char                  Buffer[512];
  unsigned long long    SavedSize = 0;
  int                   SavedNrSegments = 0;
  bool                  HeaderMode=FALSE, SegmentsMode=FALSE;
  char                  TimeStamp[16];
  char                 *c, Selected;
  int                   p;
  bool                  ret = FALSE;

  TRACEENTER;
  memset(SegmentMarker, 0, NRSEGMENTMARKER * sizeof(tSegmentMarker));

  NrSegmentMarker = 0;
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
        unsigned long long Value;

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
            ActiveSegment = Value;
        }
      }

      // Segmente einlesen
      else if (SegmentsMode)
      {
        //[Segments]
        //#Nr. ; Sel ; StartBlock ; StartTime ; Percent
        if (sscanf(Buffer, "%*i ; %c ; %lu ; %16[^;\r\n] ; %f%%", &Selected, &SegmentMarker[NrSegmentMarker].Block, TimeStamp, &SegmentMarker[NrSegmentMarker].Percent) >= 3)
        {
          SegmentMarker[NrSegmentMarker].Selected = (Selected == '*');
          SegmentMarker[NrSegmentMarker].Timems = (TimeStringToMSec(TimeStamp));
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

bool CutFileLoad(const char *AbsCutName)
{
  FILE                 *fCut = NULL;
  byte                  Version;
  unsigned long long    RecFileSize, SavedSize;
  bool                  ret = FALSE;

  TRACEENTER;

  // Puffer allozieren
  SegmentMarker = (tSegmentMarker*) malloc(NRSEGMENTMARKER * sizeof(tSegmentMarker));
  if (!SegmentMarker)
  {
    printf("CutFileLoad: Failed to allocate memory!\n");
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

    // Check, if size of rec-File has been changed
    if (ret)
    {
      HDD_GetFileSize(RecFileIn, &RecFileSize);
      if (RecFileSize != SavedSize)
      {
        printf("CutFileLoad: .cut file size mismatch!\n");
        TRACEEXIT;
        return FALSE;
      }
    }
    else
      printf("CutFileLoad: Failed to read cut-info from .cut!\n");
  }
  else
    printf("CutFileLoad: Cannot open cut file %s!\n", AbsCutName);

  TRACEEXIT;
  return ret;
}

bool CutFileClose(const char* AbsCutName, bool Save)
{
  FILE                 *fCut = NULL;
  char                  TimeStamp[16];
  unsigned long long    RecFileSize;
  int                   i;
  bool                  ret = TRUE;

  TRACEENTER;

  // neues CutFile speichern
  if (SegmentMarker)
  {
    if (Save)
    {
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
        ret = (fprintf(fCut, "#Nr ; Sel ; StartBlock ;     StartTime ; Percent\r\n") > 0) && ret;
        for (i = 0; i < NrSegmentMarker; i++)
        {
          MSecToTimeString(SegmentMarker[i].Timems, TimeStamp);
          ret = (fprintf(fCut, "%3d ;  %c  ; %10lu ;%14s ;  %5.1f%%\r\n", i, (SegmentMarker[i].Selected ? '*' : '-'), SegmentMarker[i].Block, TimeStamp, SegmentMarker[i].Percent) > 0) && ret;
        }
        ret = (fflush(fCut) == 0) && ret;
        ret = (fclose(fCut) == 0) && ret;
      }
    }
    free(SegmentMarker);
    SegmentMarker = NULL;
  }
  TRACEEXIT;
  return ret;
}

void ProcessCutFile(const dword CurrentPosition, const dword PositionOffset)
{
  static int i = 0;

  TRACEENTER;
  while ((i < NrSegmentMarker) && (SegmentMarker[i].Block < CurrentPosition))
  {
    SegmentMarker[i].Block -= PositionOffset;
    i++;
  }
  TRACEEXIT;
}
