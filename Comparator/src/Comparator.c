/*
  RecFile Comparer v1.0
  Compares two 188/192-packet TS files and identifies matching parts
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
#include "Comparator.h"


static FILE            *fL, *fR;
static char             RefPackL[192], RefPackR[192];
static char             CurPackL[192], CurPackR[192];
static long long int    RefPosL = 0, RefPosR = 0;
static int              PacketSizeL = 0, PacketSizeR = 0, OffsetL = 0, OffsetR = 0;
static bool             isHumaxL = FALSE, isHumaxR = FALSE, EofL = FALSE, EofR = FALSE;
static bool             Synched = FALSE;
static unsigned int     PacksSinceLastSync = (unsigned int) -1;


static bool isPacketStart(const byte PacketArray[], int ArrayLen, int PacketSize, int PacketOffset)  // braucht 9*192+5 = 1733 / 3*192+5 = 581
{
  int                   i;
  bool                  ret = TRUE;

  for (i = 0; i < 10; i++)
  {
    if (PacketOffset + (i * PacketSize) >= ArrayLen)
    {
      if (i < 3) ret = FALSE;
      break;
    }
    if (PacketArray[PacketOffset + (i * PacketSize)] != 'G')
    {
      ret = FALSE;
      break;
    }
  }
  return ret;
}

static int FindNextPacketStart(const byte PacketArray[], int ArrayLen, int PacketSize, int PacketOffset)  // braucht 20*192+1733 = 5573 / 1185+1733 = 2981
{
  int ret = -1;
  int i;

  for (i = 0; i <= 20; i++)
  {
    if (PacketOffset + (i * PacketSize) >= ArrayLen)
      break;

    if (PacketArray[PacketOffset + (i * PacketSize)] == 'G')
    {
      if (isPacketStart(&PacketArray[i * PacketSize], ArrayLen - i*PacketSize, PacketSize, PacketOffset))
      {
        ret = i * PacketSize;
        break;
      }
    }
  }

  if (ret < 0)
  {
    for (i = 0; i <= 1184; i++)
    {
      if (i + PacketOffset >= ArrayLen)
        break;

      if (PacketArray[i + PacketOffset] == 'G')
      {
        if (isPacketStart(&PacketArray[i], ArrayLen - i, PacketSize, PacketOffset))
        {
          ret = i;
          break;
        }
      }
    }
  }
  return ret;
}

static int GetPacketSize(FILE *RecFile, char *FileName, int *OutPacketOffset, long long int *OutOffset)
{
  byte                 *Buffer = NULL;
  long long int         Offset = -1;
  int                   PacketSize = 188, PacketOffset = 0;

  Buffer = (byte*) malloc(5573);
  if (Buffer)
  {
    fseeko64(RecFile, 9024, SEEK_SET);
    if (fread(Buffer, 1, 5573, RecFile) == 5573)
    {
      char *p = strrchr(FileName, '.');
//      if (p && strcmp(p, ".vid") == 0)
//        HumaxSource = TRUE;

      Offset = FindNextPacketStart(Buffer, 5573, PacketSize, PacketOffset);

      if (Offset < 0)
      {
        PacketSize = 192;
        PacketOffset = 4;
        Offset = FindNextPacketStart(Buffer, 5573, PacketSize, PacketOffset);
      }
    }
    free(Buffer);
  }
  if(OutPacketOffset) *OutPacketOffset = PacketOffset;
  if(OutOffset) *OutOffset = (Offset + PacketOffset);

  return ((Offset >= 0) ? PacketSize : 0);
}


int main(int argc, char* argv[])
{
  fprintf(stderr, "RecFile Comparator v1.0\n");
  fprintf(stderr, "Compares two 188/192-packet TS files and identifies matching parts.\n");
  fprintf(stderr, "(c) 2020 Christian Wuensch\n\n");

  if (argc <= 2)
  {
    printf("Usage: %s <FirstFile.rec> <SecondFile.rec>\n\n", argv[0]);
    return 1;
  }
  setvbuf(stdout, NULL, _IONBF, 0);

  // Open rec files
  printf("Left file:  %s\n", argv[1]);
  if (!(fL = fopen(argv[1], "rb")))
  {
    fprintf(stderr, "File not found: %s\n", argv[1]);
    return 1;
  }
  printf("Right file: %s\n\n", argv[2]);
  if (!(fR = fopen(argv[2], "rb")))
  {
    fprintf(stderr, "File not found: %s\n", argv[2]);
    return 1;
  }

  // Detect packet size
  PacketSizeL = GetPacketSize(fL, argv[1], &OffsetL, &RefPosL);
  PacketSizeR = GetPacketSize(fR, argv[2], &OffsetR, &RefPosR);

  // Read reference pack
  fseeko64(fL, RefPosL, SEEK_SET);
  fseeko64(fR, RefPosR, SEEK_SET);

  // erste einlesen
  if (!PacketSizeL || !fread(CurPackL, PacketSizeL, 1, fL))  { printf("Unsupported file L\n", ftello64(fL)); fclose(fL); fL = NULL; }
  if (!PacketSizeR || !fread(CurPackR, PacketSizeR, 1, fR))  { printf("Unsupported file R\n", ftello64(fR)); fclose(fR); fR = NULL; }
  memcpy(RefPackL, CurPackL, 188);
  memcpy(RefPackR, CurPackR, 188);

  while (fL && fR)
  {
    // prüfe auf TS (ggf. Humax-Anpassung)
    if (!EofL && CurPackL[0] != 'G')
    {
      if (*(int*)CurPackL == 0xFD04417F)
      {
        fseeko64(fL, 996, SEEK_CUR);  // 1184 - 188
        fread(CurPackL, PacketSizeL, 1, fL);
      }
      if (CurPackL[0] != 'G')
      {
        printf("SyncErr L at %lld\n", ftello64(fL));
        memset(CurPackL, 0, sizeof(CurPackL));
        EofL = TRUE;
      }
      break;
    }
    if (!EofR && CurPackR[0] != 'G')
    {
      if (*(int*)CurPackR == 0xFD04417F)
      {
        fseeko64(fR, 996, SEEK_CUR);  // 1184 - 188
        fread(CurPackR, PacketSizeR, 1, fR);
      }
      if (CurPackR[0] != 'G')
      {
        printf("SyncErr R at %lld\n", ftello64(fR));
        memset(CurPackR, 0, sizeof(CurPackR));
        EofR = TRUE;
      }
      break;
    }

    if (Synched == FALSE)
    {
      // State: nicht eingerastet
      if (!EofL && (memcmp(CurPackL, RefPackR, 188) == 0))
      {
        // linkes File entspricht der Referenz
        printf("Synced from: L=%11lld  R=%11lld   ", ftello64(fL) - PacketSizeL, RefPosR);
        fseeko64(fR, RefPosR + PacketSizeR, SEEK_SET);
        Synched = TRUE;
      }
      else if (!EofR && (memcmp(CurPackR, RefPackL, 188) == 0))
      {
        // rechtes File entspricht der Referenz
        printf("Synced from: L=%11lld  R=%11lld   ", RefPosL, ftello64(fR) - PacketSizeR);
        fseeko64(fL, RefPosL + PacketSizeL, SEEK_SET);
        Synched = TRUE;
      }
      else if (PacksSinceLastSync < 10)
      {
        if (memcmp(CurPackL, CurPackR, 188) == 0)
        {
          // letzte Synchronisation wird forgesetzt (lediglich 1-10 Pakete mit Bitfehlern)
          printf(" - contined: L=%11lld  R=%11lld   ", ftello64(fL) - PacketSizeL, ftello64(fR) - PacketSizeR);
          Synched = TRUE;
        }
        PacksSinceLastSync++;
      }
    }
    else
    {
      // State: eingerastet
      if (memcmp(CurPackL, CurPackR, 188) != 0)
      {
        // nicht mehr eingerastet
        memcpy(RefPackL, CurPackL, 188);
        memcpy(RefPackR, CurPackR, 188);
        RefPosL = ftello64(fL) - PacketSizeL;
        RefPosR = ftello64(fR) - PacketSizeR;
        Synched = FALSE;
        PacksSinceLastSync = 0;
        printf("to: L=%11lld  R=%11lld\n", RefPosL, RefPosR);
      }
    }

    // nächste einlesen
    if (!fread(CurPackL, PacketSizeL, 1, fL))  { EofL = TRUE; memset(CurPackL, 0, sizeof(CurPackL)); }
    if (!fread(CurPackR, PacketSizeR, 1, fR))  { EofR = TRUE; memset(CurPackR, 0, sizeof(CurPackR)); }

    if (EofL && EofR)
    {
      if(Synched)  printf("to: L=%11lld  R=%11lld\n-> Synchronized end of both files!\n", ftello64(fL), ftello64(fR));
      else         printf("-> Unsynched - both files at end: L=%11lld  R=%11lld\n", ftello64(fL), ftello64(fR));
      fclose(fL); fL = NULL;
      fclose(fR); fR = NULL;
    }
  }

  if(fL) fclose(fL);
  if(fR) fclose(fR);
  return 0;
}
