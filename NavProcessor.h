#ifndef __NAVPROCESSORH__
#define __NAVPROCESSORH__

#include "RecStrip.h"

typedef struct
{
  dword                 SHOffset:24; // = (FrameType shl 24) or SHOffset
  dword                 FrameType:8;
  byte                  MPEGType;
  byte                  FrameIndex;
  word                  iFrameSeqOffset;
//  byte                  Zero1;
  dword                 PHOffsetHigh;
  dword                 PHOffset;

  dword                 PTS2;
  dword                 NextPH;
  dword                 Timems;
  dword                 Zero5;
} tnavSD;

typedef struct
{
  dword                 SEIPPS:24;
  dword                 FrameType:8;
  byte                  MPEGType;
  byte                  FrameIndex;
  word                  PPSLen;
//  byte                  Zero1;
  dword                 SEIOffsetHigh;
  dword                 SEIOffsetLow;

  dword                 SEIPTS;
  dword                 NextAUD;
  dword                 Timems;
  dword                 Zero2;

  dword                 SEISPS;
  dword                 SPSLen;
  dword                 IFrame;
  dword                 Zero4;

  dword                 Zero5;
  dword                 Zero6;
  dword                 Zero7;
  dword                 Zero8;
} tnavHD;

typedef enum
{
  NAL_UNKNOWN         = 0,
  NAL_SLICE           = 1,
  NAL_SLICE_DPA       = 2,
  NAL_SLICE_DPB       = 3,
  NAL_SLICE_DPC       = 4,
  NAL_SLICE_IDR       = 5,
  NAL_SEI             = 6,
  NAL_SPS             = 7,
  NAL_PPS             = 8,
  NAL_AU_DELIMITER    = 9,
  NAL_END_SEQUENCE    = 10,
  NAL_END_STREAM      = 11,
  NAL_FILLER_DATA     = 12,
  NAL_SPS_EXT         = 13,
  NAL_AUXILIARY_SLICE = 19
} NAL_unit_type;

typedef enum
{
  SLICE_P,
  SLICE_B,
  SLICE_I,
  SLICE_SP,
  SLICE_SI
} tSLICE_Type;


//HDNAV
typedef struct
{
  unsigned long long    Offset;
  byte                  ID;
  word                  Len;
}tPPS;

typedef struct
{
  dword                 PTS;       // PTS des zuletzt gefundenen I-Frames
  int                   FrameCtr;  // Anzahl Frames, die nach diesem I-Frame kamen (bis ein weiterer Counter eingefügt wurde)
}tFrameCtr;


extern dword            LastTimems, TimeOffset;
extern dword           *pOutNextTimeStamp;
extern FILE            *fNavIn;


bool GetPCR(byte *pBuffer, long long *pPCR);
bool GetPCRms(byte *pBuffer, dword *pPCR);
dword DeltaPCR(dword FirstPCR, dword SecondPCR);

void NavProcessor_Init(void);
void HDNAV_ParsePacket(tTSPacket *Packet, long long FilePositionOfPacket);
void SDNAV_ParsePacket(tTSPacket *Packet, long long FilePositionOfPacket);
bool LoadNavFileIn(const char* AbsInNav);
bool LoadNavFileOut(const char* AbsOutNav);
bool CloseNavFiles(void);
void ProcessNavFile(const long long CurrentPosition, const long long PositionOffset, tTSPacket* Packet);
void QuickNavProcess(const long long CurrentPosition, const long long PositionOffset);
void SetFirstPacketAfterBreak(void);

#endif
