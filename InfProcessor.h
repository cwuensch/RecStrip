#ifndef __INFPROCESSORH__
#define __INFPROCESSORH__

#include "RecHeader.h"
#include "RecStrip.h"

bool LoadInfFile(char *AbsInfName);
bool SetInfCryptFlag(const char *AbsInfFile);
bool CloseInfFile(const char *AbsDestInf, const char *AbsSourceInf, bool Save);

#endif
