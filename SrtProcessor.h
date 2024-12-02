#ifndef __SRTPROCESSORH__
#define __SRTPROCESSORH__

#include "RecStrip.h"

bool LoadSrtFilesIn(const char* AbsInRec);
bool LoadSrtFilesOut(const char* AbsOutRec);
bool SrtProcessCaptions(dword FromTimems, dword ToTimems, int TimeOffset, bool DoOutput);
void CloseSrtFilesIn(void);
bool CloseSrtFilesOut(void);

#endif
