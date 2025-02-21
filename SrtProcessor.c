#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#include <stdio.h>
#include <string.h>
#include "type.h"
#include "SrtProcessor.h"
#include "CutProcessor.h"
#include "RecStrip.h"

typedef struct
{
  FILE *fSrtIn;
  FILE *fSrtOut;
  char* Extension;
  int Nr;
  char Number[12];
  dword CaptionStart;
  dword CaptionEnd;
} tSrtStream;


// Globale Variablen
static tSrtStream         srts[4] = {{0}, {0}, {0}, {0}};


// ----------------------------------------------
// *****  PROCESS SRT FILE  *****
// ----------------------------------------------

bool LoadSrtFilesIn(const char* AbsInRec)
{
  char CurFileName[FBLIB_DIR_SIZE];
  int i, j;
  char* Extensions[] = {".srt", ".sup", "_deu.sup", "_fra.sup", "_ger.sup", "_full.sup", "_777.sup", "_150.sup", "_151.sup", "_888.sup", "_160.sup", "_161.sup", "_152.sup", "_149.sup", "_571.sup"};

  CloseSrtFilesIn();

  for (i = j = 0; (i < 8) && (j < 4); i++)
  {
    GetFileNameFromRec(AbsInRec, Extensions[i], CurFileName);
    if (HDD_FileExist(CurFileName))
    {
      srts[j].Number[0] = '\0';
      srts[j].Extension = Extensions[i];
      if ((srts[j].fSrtIn = fopen(CurFileName, "rb")))
      {
        printf("\nSrt file: %s\n", CurFileName);
        j++;
      }
    }
  }
  return (srts[0].fSrtIn != NULL);
}

bool LoadSrtFilesOut(const char* AbsOutRec)
{
  char CurFileName[FBLIB_DIR_SIZE];
  int i;
  CloseSrtFilesOut();

  for (i = 0; i < 4; i++)
  {
    if (srts[i].fSrtIn && *srts[i].Extension)
    {
      GetFileNameFromRec(AbsOutRec, srts[i].Extension, CurFileName);
      srts[i].fSrtOut = fopen(AbsOutRec, (DoMerge == 1) ? "ab" : "wb");
      if (srts[i].fSrtOut)
      {
        printf("\nSrt output: %s\n", CurFileName);
        srts[i].Nr = 1;
        if (DoMerge == 1)
        {
          fseek(srts[i].fSrtOut, 0, SEEK_END);
          fwrite("\r\n", 1, 2, srts[i].fSrtOut);
        }
      }
    }
  }
  return (srts[0].fSrtOut != NULL);
}

bool SrtProcessCaptions(dword FromTimems, dword ToTimems, int TimeDiff, bool DoOutput)
{
  char                  Buffer[256];
  unsigned int          hour1, minute1, second1, millisec1, hour2, minute2, second2, millisec2;
  bool                  inCaption = FALSE, ret[4] = {TRUE, TRUE, TRUE, TRUE};
  int i;

  for (i = 0; i < 4; i++)
  {
    if(!srts[i].fSrtIn) continue;
    if(DoOutput && !srts[i].fSrtOut)
      printf("  Warning! SRT input '...%s' has no open output file!\n", srts[i].Extension);
    Buffer[0] = '\0';

    // Walk through srt file and copy relevant parts to output files
    while (ret[i] && ((!inCaption && srts[i].CaptionEnd) || (fgets(Buffer, sizeof(Buffer), srts[i].fSrtIn))))
    {
      if (inCaption && srts[i].CaptionEnd)
      {
        if (DoOutput && srts[i].fSrtOut)
        {
          if(*srts[i].Number) fprintf(srts[i].fSrtOut, "%s", srts[i].Number);
//          fprintf(fSrtOut, "%d\r\n", srts[i].Nr++);
          hour1 = srts[i].CaptionStart / 3600000;  minute1 = (srts[i].CaptionStart / 60000) % 60;  second1 = (srts[i].CaptionStart / 1000) % 60;  millisec1 = srts[i].CaptionStart % 1000;
          hour2 =  srts[i].CaptionEnd / 3600000;   minute2 =  (srts[i].CaptionEnd / 60000) % 60;   second2 =  (srts[i].CaptionEnd / 1000) % 60;   millisec2 = srts[i].CaptionEnd % 1000;
          fprintf(srts[i].fSrtOut, "%02hu:%02hhu:%02hhu,%03hu --> %02hu:%02hhu:%02hhu,%03hu\r\n", hour1, minute1, second1, millisec1, hour2, minute2, second2, millisec2);
        }
        srts[i].Number[0] = '\0';
        srts[i].CaptionEnd = 0;
      }

      // in Caption -> Zeile ausgeben
      if (inCaption)
      {
        if (DoOutput && srts[i].fSrtOut)
          ret[i] = fwrite(Buffer, 1, strlen(Buffer), srts[i].fSrtOut) && ret[i];

        // leere Zeile -> Caption beendet
        if (!*Buffer || Buffer[0] == '\r' || Buffer[0] == '\n')
        {  
          inCaption = FALSE;
          continue;
        }
      }
      else
      {
        // neue Zeile nur einlesen, wenn keine CaptionEnd mehr hinterlegt
        if (sscanf(Buffer, "%2u:%2u:%2u,%3u --> %2u:%2u:%2u,%3u", &hour1, &minute1, &second1, &millisec1, &hour2, &minute2, &second2, &millisec2) == 8)
        {
          srts[i].CaptionStart = 3600000*hour1 + 60000*minute1 + 1000*second1 + millisec1;
          srts[i].CaptionEnd   = 3600000*hour2 + 60000*minute2 + 1000*second2 + millisec2;
        }

        if (srts[i].CaptionEnd)
        {
          if (srts[i].CaptionStart <= ToTimems && (srts[i].CaptionEnd <= ToTimems || DoOutput))
          {
            // CaptionStart könnte vorm Bereich beginnen
            if (srts[i].CaptionStart >= FromTimems)
              srts[i].CaptionStart -= min(TimeDiff, (int)srts[i].CaptionStart);
            else
              srts[i].CaptionStart = FromTimems-TimeDiff;

            // CaptionEnd könnte ihn überschreiten
            if (srts[i].CaptionEnd <= ToTimems)
              srts[i].CaptionEnd -= TimeDiff;
            else
              srts[i].CaptionEnd = ToTimems-TimeDiff - 1;

            inCaption = TRUE;
          }
          else
            break;
        }
        else if (!*srts[i].Number)
          strncpy(srts[i].Number, Buffer, sizeof(srts[i].Number));
        else
        {
          printf("Error processing srt input file '...%s'!\n", srts[i].Extension);
          ret[i] = FALSE;
        }
      }
    }
  }
  return (ret[0] && ret[1] && ret[2] && ret[3]);
}

void CloseSrtFilesIn(void)
{
  int i;
  for (i = 0; i < 4; i++)
  {
    if(srts[i].fSrtIn) fclose(srts[i].fSrtIn);
    srts[i].fSrtIn = NULL;
  }
}

bool CloseSrtFilesOut(void)
{
  bool ret = TRUE;
  int i;
  for (i = 0; i < 4; i++)
  {
    if(srts[i].fSrtOut) ret = ( fclose(srts[i].fSrtOut) == 0 ) && ret;
    srts[i].fSrtOut = NULL;
  }
  return ret;
}
