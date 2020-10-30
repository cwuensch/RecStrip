/*
  RecStrip Inf / Log Extractor v1.0
  Extracts stream details from inf or continutiy errors from a RecStrip logfile
  (c) 2020 Christian Wünsch
*/
  
#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64
#ifdef _MSC_VER
  #define inline
#endif

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <io.h>
  #include <sys/utime.h>
#else
  #include <unistd.h>
  #include <utime.h>
#endif

#include <time.h>
#include "type.h"
#include "RecHeader.h"
#include "LogExtractor.h"


static FILE            *fIn, *FOut;
static char            *Buffer;
static char             TextBuffer[4097];
static char             RecFileIn[FBLIB_DIR_SIZE], RecFileOut[FBLIB_DIR_SIZE];
static char             StartTimeStr[25];
static long long        RecSize;
static tContinuityError FileDefect[MAXPIDS];
static word             PidMap[MAXPIDS];
static int              NrFiles, NrPIDs, NrErrorsInFile;
static long long        FirstPCR, LastPCR;


static bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize)
{
  struct stat64         statbuf;
  bool                  ret = FALSE;

  if(AbsFileName)
  {
    ret = (stat64(AbsFileName, &statbuf) == 0);
    if (ret && OutFileSize)
      *OutFileSize = statbuf.st_size;
  }
  return ret;
}

static SYSTEM_TYPE DetermineInfType(const byte *const InfBuffer, const unsigned long long InfFileSize)
{
  int                   AntiS = 0, AntiT = 0, AntiC = 0;
  SYSTEM_TYPE           SizeType = ST_UNKNOWN, Result = ST_UNKNOWN;

  TYPE_RecHeader_TMSS  *Inf_TMSS = (TYPE_RecHeader_TMSS*)InfBuffer;
  TYPE_RecHeader_TMSC  *Inf_TMSC = (TYPE_RecHeader_TMSC*)InfBuffer;
  TYPE_RecHeader_TMST  *Inf_TMST = (TYPE_RecHeader_TMST*)InfBuffer;

  // Dateigröße beurteilen
  if (((InfFileSize % 122312) % 1024 == 84) || ((InfFileSize % 122312) % 1024 == 248))
  {
//    PointsS++;
//    PointsT++;
    SizeType = ST_TMSS;
  }
  else if (((InfFileSize % 122312) % 1024 == 80) || ((InfFileSize % 122312) % 1024 == 244))
  {
//    PointsC++;
    SizeType = ST_TMSC;
  }

  // Frequenzdaten

  //ST_TMSS: Frequency = dword @ 0x0578 (10000...13000)
  //         SymbolRate = word @ 0x057c (2000...30000)
  //         Flags1 = word     @ 0x0575 (& 0xf000 == 0)
  //
  //ST_TMST: Frequency = dword @ 0x0578 (47000...862000)
  //         Bandwidth = byte  @ 0x0576 (6..8)
  //         LPHP = byte       @ 0x057e (& 0xfe == 0)
  //
  //ST_TMSC: Frequency = dword @ 0x0574 (174000...230000 || 470000...862000)
  //         SymbolRate = word @ 0x0578 (2000...30000)
  //         Modulation = byte @ 0x057e (<= 4)

  if (Inf_TMSS->TransponderInfo.Frequency    &&  !((Inf_TMSS->TransponderInfo.Frequency      >=     10700)  &&  (Inf_TMSS->TransponderInfo.Frequency  <=     12750)))  AntiS++;
  if (Inf_TMSS->TransponderInfo.SymbolRate   &&  !((Inf_TMSS->TransponderInfo.SymbolRate     >=     10000)  &&  (Inf_TMSS->TransponderInfo.SymbolRate <=     30000)))  AntiS++;
  if (Inf_TMSS->TransponderInfo.UnusedFlags1  ||  Inf_TMSS->TransponderInfo.Unknown2)  AntiS++;
  if (Inf_TMSS->TransponderInfo.TPMode > 1)  AntiS++;
  if ((dword)Inf_TMSS->BookmarkInfo.NrBookmarks > 64)  AntiS++;

  if (Inf_TMST->TransponderInfo.Frequency    &&  !((Inf_TMST->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMST->TransponderInfo.Frequency  <=    862000)))  AntiT++;
  if (Inf_TMST->TransponderInfo.Bandwidth    &&  !((Inf_TMST->TransponderInfo.Bandwidth      >=         6)  &&  (Inf_TMST->TransponderInfo.Bandwidth  <=         8)))  AntiT++;
  if (Inf_TMST->TransponderInfo.LPHP > 1  ||  Inf_TMST->TransponderInfo.unused2)  AntiT++;
  if (Inf_TMST->TransponderInfo.SatIdx != 0)  AntiT++;
  if ((dword)Inf_TMST->BookmarkInfo.NrBookmarks > 64)  AntiT++;

  if (Inf_TMSC->TransponderInfo.Frequency    &&  !((Inf_TMSC->TransponderInfo.Frequency      >=     47000)  &&  (Inf_TMSC->TransponderInfo.Frequency  <=    862000)))  AntiC++;
  if (Inf_TMSC->TransponderInfo.SymbolRate   &&  !((Inf_TMSC->TransponderInfo.SymbolRate     >=      6111)  &&  (Inf_TMSC->TransponderInfo.SymbolRate <=      6900)))  AntiC++;
  if (Inf_TMSC->TransponderInfo.ModulationType > 4)  AntiC++;
  if (Inf_TMSC->TransponderInfo.SatIdx != 0)  AntiC++;
  if ((dword)Inf_TMSC->BookmarkInfo.NrBookmarks > 64)  AntiC++;

//  printf("  Determine SystemType: DVBs = %d, DVBt = %d, DVBc = %d Points\n", -AntiS, -AntiT, -AntiC);

  if ((AntiC == 0 && AntiS > 0 && AntiT > 0) || (AntiC == 1 && AntiS > 1 && AntiT > 1))
    Result = ST_TMSC;
  else
    if ((AntiS == 0 && AntiC > 0 && AntiT > 0) || (AntiS == 1 && AntiC > 1 && AntiT > 1))
      Result = ST_TMSS;
    else if ((AntiT == 0 && AntiS > 0 && AntiC > 0) || (AntiT == 1 && AntiS > 1 && AntiC > 1))
      Result = ST_TMST;
    else if (/*(Inf_TMSS->BookmarkInfo.NrBookmarks == 0) &&*/ (Inf_TMSC->BookmarkInfo.NrBookmarks == 0))
      Result = ST_TMSS;

//  printf("   -> SystemType=ST_TMS%c\n", (Result==ST_TMSS ? 's' : ((Result==ST_TMSC) ? 'c' : ((Result==ST_TMST) ? 't' : '?'))));
  if (Result != SizeType && SizeType && !(Result == ST_TMST && SizeType == ST_TMSS))
    fprintf(stderr, "   -> DEBUG! Assertion error: SystemType in inf (%u) not consistent to filesize (%u)!\n", Result, SizeType);

  return Result;
}

static char* RemoveItemizedText(TYPE_RecHeader_TMSS *RecHeader, char *const NewEventText, int NewTextLen)
{
  // ggf. Itemized Items in ExtEventText entfernen
  memset(NewEventText, 0, sizeof(NewEventText));
  {
    int j = 0, k = 0, p = 0;
    char* c;
    while ((j < 2*RecHeader->ExtEventInfo.NrItemizedPairs) && (p < RecHeader->ExtEventInfo.TextLength))
      if (RecHeader->ExtEventInfo.Text[p++] == '\0')  j++;

    if (j == 2*RecHeader->ExtEventInfo.NrItemizedPairs)
    {
      strncpy(NewEventText, &RecHeader->ExtEventInfo.Text[p], min(RecHeader->ExtEventInfo.TextLength - p, NewTextLen-1));

      p = 0;
      for (k = 0; k < j; k++)
      {
        if(RecHeader->ExtEventInfo.Text[p] < 0x20)  p++;
        sprintf(&NewEventText[strlen(NewEventText)], ((k % 2 == 0) ? ((NewEventText[0]>=0x15) ? "\xC2\x8A%s: " : "\x8A%s: ") : "%s"), &RecHeader->ExtEventInfo.Text[p]);
        p += (int)strlen(&RecHeader->ExtEventInfo.Text[p]) + 1;
      }
    }
    else
      strncpy(NewEventText, RecHeader->ExtEventInfo.Text, min(RecHeader->ExtEventInfo.TextLength, NewTextLen-1));

    // Ersetze eventuelles '\n' im Output
    for (c = NewEventText; *c != '\0'; c++)
      if (*c == '\n') *c = 0x8A;
  }
  return NewEventText;
}

static time_t TF2UnixTime(tPVRTime TFTimeStamp, byte TFTimeSec, bool isUTC)
{
  time_t Result = (MJD(TFTimeStamp) - 0x9e8b) * 86400 + HOUR(TFTimeStamp) * 3600 + MINUTE(TFTimeStamp) * 60 + TFTimeSec;
#ifndef LINUX
  if (!isUTC)
  {
    Result += timezone;
    if (localtime(&Result)->tm_isdst)
      Result -= 3600;
  }
#endif
  return Result;
}

static char* TimeStr2(tPVRTime TFTimeStamp, byte TFTimeSec, bool isUTC)
{
  static char TS[26];
  time_t DisplayTime = TF2UnixTime(TFTimeStamp, TFTimeSec, isUTC);
  if (TFTimeStamp)
    strftime(TS, sizeof(TS), "%a %d %b %Y %H:%M:%S", localtime(&DisplayTime));
  else
  {
    TS[0] = '-'; TS[1] = '\0';
  }
  return TS;
}


static int getPidId(word PID)
{
  int i;
  for (i = 0; i < NrPIDs; i++)
  {
    if (PidMap[i] == PID)
      return i;
  }
  if (NrPIDs < MAXPIDS - 1)
  {
    PidMap[NrPIDs] = PID;
    NrPIDs++;
    return NrPIDs - 1;
  }
  return -1;
}

static void printFileDefect(tContinuityError FileDefect[])
{
  int i;
  if (FileDefect[0].PID != 0)
  {
    // FileID;  Input-Rec;  konvertierte Rec;  Größe Original-Rec;  FirstPCR;  LastPCR;  { PID;  Position }
    printf("%d.\t%s\t%s\t%lld\t%lld\t%lld", NrFiles, RecFileIn, RecFileOut, RecSize, FirstPCR, LastPCR);
    for (i = 0; i < NrPIDs; i++)
      printf("\t%hu\t%.2f%%\t%lld", FileDefect[i].PID, (double)FileDefect[i].Position*100/RecSize, FileDefect[i].Position);
    printf("\n");
  }
}


int main(int argc, char* argv[])
{
  fprintf(stderr, "RecStrip Inf / Log Extractor v1.0\n");
  fprintf(stderr, "Extracts stream details from inf or continutiy errors from a RecStrip logfile.\n");
  fprintf(stderr, "(c) 2020 Christian Wuensch\n\n");
  tzset();

  if (argc <= 1)
  {
    printf("Usage: %s <RecName.rec.inf>\n\n", argv[0]);
    printf("  or:  %s <LogFile.txt>\n\n", argv[0]);
    return 1;
  }

  if (!(Buffer = (char*) malloc(BUFSIZE)))
  {
    fprintf(stderr, "Insufficient memory.\n\n");
    return 2;
  }
  RecFileIn[0] = '\0';
  RecFileOut[0] = '\0';
  StartTimeStr[0] = '\0';


  if (strncmp(&(argv[1][strlen(argv[1])-4]), ".inf", 4) == 0)
  {
    // DO INF PROCESSING

    // Open inf file
    if ((fIn = fopen(argv[1], "rb")))
    {
      SYSTEM_TYPE            InfType = ST_UNKNOWN;
      int                    InfSize = 0;
      TYPE_RecHeader_TMSS   *Inf_TMSS = (TYPE_RecHeader_TMSS*)Buffer;
      TYPE_RecHeader_TMSC   *Inf_TMSC = (TYPE_RecHeader_TMSC*)Buffer;
      TYPE_RecHeader_TMST   *Inf_TMST = (TYPE_RecHeader_TMST*)Buffer;

      InfSize = fread(Buffer, 1, BUFSIZE, fIn);
      if (InfSize >= sizeof(TYPE_RecHeader_TMSC))
      {
        // Detection of inf type
        InfType = DetermineInfType((byte*)Buffer, InfSize);
        memset(TextBuffer, 0, sizeof(TextBuffer));
        strncpy(TextBuffer, Inf_TMSS->EventInfo.EventNameDescription, min(Inf_TMSS->EventInfo.EventNameLength, sizeof(Inf_TMSS->EventInfo.EventNameDescription)));

        // Print out inf details
        // Rec-Name;  StartTime (DateTime);  Duration (mm:ss);  ServiceName;  ServiceID; PMTPID; VideoPID; AudioPID; VideoType; AudioType;  EvtName; EvtShortDesc; EvtStart (DateTime); EvtEnd (DateTime); EvtDuration (hh:mm); ExtEventDesc (inkl. ItemizedItems)
        printf("%s\tST_TMS%c\t",                 argv[1],  (InfType==ST_TMSS ? 's' : ((InfType==ST_TMSC) ? 'c' : ((InfType==ST_TMST) ? 't' : '?'))));
        printf("%s\t%02hu:%02hu:%02hu\t",                   TimeStr2(Inf_TMSS->RecHeaderInfo.StartTime, Inf_TMSS->RecHeaderInfo.StartTimeSec, FALSE), Inf_TMSS->RecHeaderInfo.DurationMin/60,   Inf_TMSS->RecHeaderInfo.DurationMin%60, Inf_TMSS->RecHeaderInfo.DurationSec);
        printf("%s\t%hu\t%hu\t%hu\t%hu\t0x%hx\t0x%hx\t",    Inf_TMSS->ServiceInfo.ServiceName,        Inf_TMSS->ServiceInfo.ServiceID,                Inf_TMSS->ServiceInfo.PMTPID,             Inf_TMSS->ServiceInfo.VideoPID,  Inf_TMSS->ServiceInfo.AudioPID,  Inf_TMSS->ServiceInfo.VideoStreamType,  Inf_TMSS->ServiceInfo.AudioStreamType);
        printf("%s\t%s\t%s\t",                              TextBuffer,                               &Inf_TMSS->EventInfo.EventNameDescription[Inf_TMSS->EventInfo.EventNameLength],           TimeStr2(Inf_TMSS->EventInfo.StartTime, 0, TRUE));
        printf("%s\t%02hhu:%02hhu\t%s\n",                   TimeStr2(Inf_TMSS->EventInfo.EndTime, 0, TRUE), Inf_TMSS->EventInfo.DurationHour,         Inf_TMSS->EventInfo.DurationMin,          RemoveItemizedText(Inf_TMSS, TextBuffer, 1024));
      }
      else
      {
        fprintf(stderr, "Unexpected end of inf file: %s\n", argv[1]);
        return 4;
      }
      fclose(fIn);
    }
    else
    {
      fprintf(stderr, "File not found: %s\n", argv[1]);
      return 3;
    }
  }
  else
  {
    // DO LOG PROCESSING

    // Open logfile
    if ((fIn = fopen(argv[1], "r")))
    {
      int               NrLines = 0, KeptLines = 0;
      int               NrSuccess = 0, NrErrors = 0, NrContErrors = 0;
      long long int     LastPosition;
      bool              isEPGText = FALSE;

      // Read line by line
      while (fgets(Buffer, BUFSIZE, fIn) != NULL)
      {
        word            CurPID;
        long long int   CurrentPosition;
        word            CountIs, CountShould;
        int             Offset, PidID;

//        printf("%s", Buffer);
        if (isEPGText)
        {
          if (strncmp(Buffer, "  TS: ExtEvent  =", 17) != 0)
          {
            int len = strlen(TextBuffer);
            if(TextBuffer[len-1] != 0x8A)  TextBuffer[len++] = 0x8A;
            strncpy(&TextBuffer[len], Buffer, 4096-len);
            len = strlen(TextBuffer);
            if(TextBuffer[len-1] == '\n')  TextBuffer[--len] = 0;
            if(TextBuffer[len-1] == '\r')  TextBuffer[--len] = 0;
          }
          else
            isEPGText = FALSE;
        }

        if (sscanf(Buffer, "Input file: %511[^\n\r]", &RecFileIn) == 1)
        {
          RecFileOut[0] = '\0';
          NrFiles++;
          NrErrorsInFile = 0;
          NrPIDs = 0;
          RecSize = 0;
          FirstPCR = 0; LastPCR = 0;
          LastPosition = 0;
          memset(PidMap, 0, MAXPIDS * sizeof(word));
          memset(FileDefect, 0, MAXPIDS * sizeof(tContinuityError));
          memset(TextBuffer, 0, sizeof(TextBuffer));
          isEPGText = FALSE;
        }
        else if (sscanf(Buffer, "Output rec: %511[^\n\r]", &RecFileOut) == 1)
        {
//          printf(Buffer);
          // ok
        }
        else if (sscanf(Buffer, " File size: %lld, packet size: %*d", &RecSize) == 1)
        {
//          printf(Buffer);
          // ok
        }
        else if (sscanf(Buffer, " TS: FirstPCR  = %llu (%*d:%*d:%*d,%*d), Last: %llu (%*d:%*d:%*d,%*d)", &FirstPCR, &LastPCR) == 2)
        {
//          printf(Buffer);
          // ok
        }
        else if (sscanf(Buffer, " TS: StartTime = %24[^\n\r]", &StartTimeStr) == 1)
        {
//          printf(StartTimeStr);
          // ok
        }
        else if (sscanf(Buffer, " TS: EPGExtEvt = %4096[^\n\r]", &TextBuffer) == 1)
        {
          isEPGText = TRUE;
        }
        else if ( (sscanf(Buffer,      "TS check: TS continuity mismatch (PID=%hu, pos=%lld, expect=%hu, found=%hu, missing=%*hd)", &CurPID, &CurrentPosition, &CountShould, &CountIs) == 4)  // %hhu wird von MSVC wohl nicht unterstützt -> schreibt mehr als ein Byte
               || (sscanf(Buffer,   "cNaluDumper: TS continuity mismatch (PID=%hu, pos=%lld, expect=%hu, found=%hu, Offset=%d)",    &CurPID, &CurrentPosition, &CountShould, &CountIs, &Offset) == 5)
               || (sscanf(Buffer, " PESProcessor: TS continuity mismatch (PID=%hu, pos=%lld, expect=%hu, found=%hu)",               &CurPID, &CurrentPosition, &CountShould, &CountIs) == 4) )
        {
          // add error to array
          if ((PidID = getPidId(CurPID)) >= 0)
          {
            if (FileDefect[PidID].PID || (LastPosition == 0) || (CurrentPosition - LastPosition > MAXDIST))
            {
              // neuer Fehler
              printFileDefect(FileDefect);
              if (NrErrorsInFile == 0)
                NrContErrors++;
              NrErrorsInFile++;
              memset(FileDefect, 0, MAXPIDS * sizeof(tContinuityError));
            }
            FileDefect[PidID].PID         = CurPID;
            FileDefect[PidID].Position    = CurrentPosition;
            FileDefect[PidID].CountIs     = (byte) CountIs;
            FileDefect[PidID].CountShould = (byte) CountShould;
            LastPosition = CurrentPosition;
          }
          else
            fprintf(stderr, "ERROR: Too many PIDs!\n");
        }
        else if (strncmp(Buffer, "Elapsed time:", 13) == 0)
        {
          printFileDefect(FileDefect);

          fgets(Buffer, BUFSIZE, fIn);
          if ((strncmp(Buffer, "ERFOLG:", 7) == 0) || (strncmp(Buffer, "FEHLER", 6) == 0))
          {
            Buffer[6] = '\0';
            if(Buffer[0] == 'E')  { NrSuccess++; } else { NrErrors++; }
          }
          else
            Buffer[0] = '\0';
          fprintf(stderr, "%3d.\t%s\t%s\t%lld\t%s\t%lld\t%lld\t%d\t%s\t%s\n", NrFiles, RecFileIn, RecFileOut, RecSize, StartTimeStr, FirstPCR, LastPCR, NrErrorsInFile, Buffer, TextBuffer);

          RecFileIn[0] = '\0';
          RecFileOut[0] = '\0';
          memset(FileDefect, 0, MAXPIDS * sizeof(tContinuityError));
        }
        NrLines++;
      }
      fclose(fIn);
      fprintf(stderr, "\nLog extraction finished. %d / %d files successfully processed (%d files with cont. errors).\n", NrSuccess, NrFiles, NrContErrors);
    }
    else
    {
      fprintf(stderr, "File not found: %s\n", argv[1]);
      return 3;
    }
  }

  free(Buffer);
  return 0;
}
