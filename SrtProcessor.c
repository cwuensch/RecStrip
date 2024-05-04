#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#include <stdio.h>
#include <string.h>
#include "type.h"
#include "SrtProcessor.h"
#include "RecStrip.h"


// Globale Variablen
FILE                   *fSrtIn = NULL, *fSrtOut = NULL;
char                    Number[12];
dword                   CaptionStart = 0, CaptionEnd = 0;
bool                    inCaption = FALSE;


// ----------------------------------------------
// *****  PROCESS SRT FILE  *****
// ----------------------------------------------

void SrtProcessor_Init(void)
{
}

bool LoadSrtFileIn(const char* AbsInSrt)
{
}

bool LoadSrtFileOut(const char* AbsOutSrt)
{
}

bool SrtProcessNextCaption(Out, NewStartTimeOffset, SegmentMarker[CurSeg].Timems)
{
}

void CloseSrtFileIn(void)
{
}

bool CloseSrtFileOut(void)
{
}
