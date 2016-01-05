#ifndef __NALUDUMPH__
#define __NALUDUMPH__

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

typedef struct
{
  char SyncByte;  // = 'G'
  byte PID1:5;
  byte Transport_Prio:1;
  byte Payload_Unit_Start:1;
  byte Transport_Error:1;
  byte PID2:8;
  byte ContinuityCount:4;
  byte Payload_Exists:1;
  byte Adapt_Field_Exists:1;
  byte Scrambling_Ctrl:2;
  byte Adapt_Field_Length;
} tTSPacketHeader;


int TsGetPID(tTSPacketHeader *Packet);
int TsPayloadOffset(tTSPacketHeader *Packet);
bool ProcessTSPacket(unsigned char *Packet, unsigned long long FilePosition);

#endif
