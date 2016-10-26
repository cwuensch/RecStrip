#ifndef __NALUDUMPH__
#define __NALUDUMPH__

#include "RecStrip.h"

#define TS_SIZE               188

// ----------------------------------------------
// *****  NALU Dump  *****
// ----------------------------------------------

typedef enum
{
  NALU_NONE=0,    // currently not NALU fill stream
  NALU_FILL,      // Within NALU fill stream, 0xff bytes and NALU start code in byte 0
  NALU_TERM,      // Within NALU fill stream, read 0x80 terminating byte
  NALU_END        // Beyond end of NALU fill stream, expecting 0x00 0x00 0x01 now
} eNaluFillState;

typedef struct
{
  int DropPayloadStartBytes;
  int DropPayloadEndBytes;
  bool DropAllPayloadBytes;
  bool ZerosOnly;
} sPayloadInfo;


int TsGetPID(tTSPacket *Packet);
int TsPayloadOffset(tTSPacket *Packet);
int ProcessTSPacket(unsigned char *Packet, long long FilePosition);

#endif
