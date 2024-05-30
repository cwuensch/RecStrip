#ifndef __SRTPROCESSORH__
#define __SRTPROCESSORH__

#include "RecStrip.h"

bool LoadSrtFileIn(const char* AbsInSrt);
bool LoadSrtFileOut(const char* AbsOutSrt);
bool SrtProcessCaptions(dword FromTimems, dword ToTimems, int TimeOffset, bool DoOutput);
void CloseSrtFileIn(void);
bool CloseSrtFileOut(void);

#endif
