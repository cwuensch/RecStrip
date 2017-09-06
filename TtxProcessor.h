#ifndef __TTXPROCESSORH__
#define __TTXPROCESSORH__

#include "RecStrip.h"

extern FILE            *fTtxOut;
extern dword            global_timestamp;
extern dword            last_timestamp;

void SetTeletextBreak(bool NewInputFile);
void TtxProcessor_Init();
bool LoadTeletextOut(const char* AbsOutFile);
void ProcessTtxPacket(tTSPacket *Packet);
bool CloseTeletextOut(void);

#endif
