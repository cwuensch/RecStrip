#ifndef __TTXPROCESSORH__
#define __TTXPROCESSORH__

#include "RecStrip.h"

extern FILE            *fTtxOut;
extern dword            global_timestamp;
extern dword            last_timestamp;

char* TimeStr(time_t UnixTimeStamp);
//char* TimeStr_UTC(time_t UnixTimeStamp);
char* TimeStrTF(tPVRTime TFTimeStamp, byte TFTimeSec);
char* TimeStr_DB(tPVRTime TFTimeStamp, byte TFTimeSec);
void SetTeletextBreak(bool NewInputFile, word SubtitlePage);
void TtxProcessor_Init(word SubtitlePage);
bool LoadTeletextOut(const char* AbsOutFile);
void ProcessTtxPacket(tTSPacket *Packet, long long FilePos);
word telx_to_ucs2(byte c);
void ucs2_to_utf8(char *r, word ch);
void process_pes_packet(byte *buffer, word size);
bool CloseTeletextOut(const char* AbsOutFile);

#endif
