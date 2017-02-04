#ifndef __INFPROCESSORH__
#define __INFPROCESSORH__

#include "RecHeader.h"
#include "RecStrip.h"

extern TYPE_RecHeader_Info *RecHeaderInfo;

bool InfProcessor_Init(void);
bool LoadInfFromRec(char *AbsRecFileName);
bool LoadInfFile(char *AbsInfName);
void SetInfEventText(const char *pCaption);
bool SetInfCryptFlag(const char *AbsInfFile);
bool SetInfStripFlags(const char *AbsInfFile, bool SetHasBeenScanned, bool ResetToBeStripped);
bool SaveInfFile(const char *AbsDestInf, const char *AbsSourceInf);
void InfProcessor_Free(void);

#endif
