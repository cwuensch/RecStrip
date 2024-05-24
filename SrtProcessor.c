#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#include <stdio.h>
#include <string.h>
#include "type.h"
#include "SrtProcessor.h"
#include "RecStrip.h"


// Globale Variablen
static FILE            *fSrtIn = NULL, *fSrtOut = NULL;
static char             Number[12];
static int              Nr;
static dword            CaptionStart = 0, CaptionEnd = 0;
static bool             inCaption = FALSE;


// ----------------------------------------------
// *****  PROCESS SRT FILE  *****
// ----------------------------------------------

void SrtProcessor_Init(void)
{
}

bool LoadSrtFileIn(const char* AbsInSrt)
{
  if(fSrtIn) fclose(fSrtIn);
  fSrtIn = fopen(AbsInSrt, "rb");
  return (fSrtIn != NULL);
}

bool LoadSrtFileOut(const char* AbsOutSrt)
{
  if(fSrtOut) fclose(fSrtOut);
  fSrtOut = fopen(AbsOutSrt, (DoMerge == 1) ? "ab" : "wb");
  Nr = 1;
  return (fSrtOut != NULL);
}

bool SrtProcessCaptions(dword FromTimems, dword ToTimems, int TimeDiff, bool DoOutput)
{
  char                  Buffer[256], Number[12];
  unsigned int          hour1, minute1, second1, millisec1, hour2, minute2, second2, millisec2;
  bool                  inCaption = FALSE, ret = TRUE;

  Buffer[0] = '\0'; Number[0] = '\0';
  
  // Walk through srt file and copy relevant parts to output files
  while (ret && ((!inCaption && CaptionEnd) || (fgets(Buffer, sizeof(Buffer), fSrtIn))))
  {
    // in Caption -> Zeile ausgeben
    if (inCaption)
    {
      ret = fwrite(Buffer, 1, strlen(Buffer), fSrtOut) && ret;
      fwrite("\n", 1, 1, fSrtOut);

      // leere Zeile -> Caption beendet
      if (!*Buffer || Buffer[0] == '\r' || Buffer[0] == '\n')
      {  
        inCaption = FALSE;
        continue;
      }
    }

    if (!inCaption || CaptionEnd)
    {
      // neue Zeile nur einlesen, wenn keine CaptionEnd mehr hinterlegt
      if (CaptionEnd || sscanf(Buffer, "%2u:%2u:%2u,%3u --> %2u:%2u:%2u,%3u", &hour1, &minute1, &second1, &millisec1, &hour2, &minute2, &second2, &millisec2) == 8)
      {
        CaptionStart = 3600000*hour1 + 60000*minute1 + 1000*second1 + millisec1;
        CaptionEnd   = 3600000*hour2 + 60000*minute2 + 1000*second2 + millisec2;

        if (CaptionStart <= ToTimems && (CaptionEnd <= ToTimems || DoOutput))
        {
          // CaptionStart könnte vorm Bereich beginnen
          if (CaptionStart >= FromTimems)
            CaptionStart -= min(TimeDiff, (int)CaptionStart);
          else
            CaptionStart = FromTimems;

          // CaptionEnd könnte ihn überschreiten
          if (CaptionEnd <= ToTimems)
            CaptionEnd -= TimeDiff;
          else
            CaptionEnd = ToTimems;
        }
        else
          break;

        hour1 = CaptionStart / 3600000;  minute1 = (CaptionStart / 60000) % 60;  second1 = (CaptionStart / 1000) % 60;  millisec1 = CaptionStart % 1000;
        hour2 =  CaptionEnd / 3600000;   minute2 =  (CaptionEnd / 60000) % 60;   second2 =  (CaptionEnd / 1000) % 60;   millisec2 = CaptionEnd % 1000;
        CaptionEnd = 0;

//        fprintf(fOut, "%d\r\n", Nr++);
        if(*Number) fprintf(fSrtOut, "%s\r\n", Number);
        fprintf(fSrtOut, "%02hu:%02hhu:%02hhu,%03hu --> %02hu:%02hhu:%02hhu,%03hu\r\n", hour1, minute1, second1, millisec1, hour2, minute2, second2, millisec2);
        Number[0] = '\0';
        inCaption = TRUE;
      }
      else if (!*Number)
        strncpy(Number, Buffer, sizeof(Number));
      else
      {
        printf("Error processing srt input file!\n");
        ret = FALSE;
      }
    }
  }
  return ret;
}

void CloseSrtFileIn(void)
{
  if(fSrtIn) fclose(fSrtIn);
  fSrtIn = NULL;
}

bool CloseSrtFileOut(void)
{
  bool ret = TRUE;
  if(fSrtOut) ret = fclose(fSrtOut);
  fSrtOut = NULL;
  return ret;
}
