#ifndef __SRTPROCESSORH__
#define __SRTPROCESSORH__

#include "RecStrip.h"

extern FILE            *fSrtIn, *fSrtOut;
extern char             Number[12];
extern dword            CaptionStart, CaptionEnd;
extern bool             inCaption;

void SrtProcessor_Init(void);
bool LoadSrtFileIn(const char* AbsInSrt);
bool LoadSrtFileOut(const char* AbsOutSrt);
void CloseSrtFileIn(void);
bool CloseSrtFileOut(void);

#endif
