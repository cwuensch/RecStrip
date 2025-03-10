#ifndef __PESPROCESSORH__
#define __PESPROCESSORH__

typedef struct
{
  byte TableID;          // TABLE_PAT = 0x00
  byte SectionLen1:4;    // first 2 bits are 0
  byte Reserved1:2;      // = 0x03 (all 1)
  byte Private:1;        // = 0
  byte SectionSyntax:1;  // = 1
  byte SectionLen2;
} tTSTableHeader;

typedef struct
{
  word                  PID;
  bool                  TablePacket;
  int                   ValidBuffer;    //0: no data yet, Buffer1 gets filled
                                        //1: Buffer1 is valid, Buffer2 gets filled
                                        //2: Buffer2 is valid, Buffer1 gets filled
  int                   BufferSize;
  int                   BufferPtr;
  bool                  IgnoreContErrors;
  bool                  ValidDiscontinue;
  bool                  NewDiscontinue;
  int                   ValidBufLen;
  byte                 *pBuffer;
  byte                  LastCCCounter;
  byte                 *Buffer1, *Buffer2;
  bool                  ValidPayloadStart;
  bool                  NewPayloadStart;
  int                   curSectionLen;
  bool                  ErrorFlag;
#ifdef _DEBUG
  int                   maxPESLen;
#endif
} tPSBuffer;

bool PSBuffer_Init(tPSBuffer *PSBuffer, word PID, int BufferSize, bool TablePacket, bool DropBufferOnErr);
void PSBuffer_Reset(tPSBuffer *PSBuffer);
void PSBuffer_DropCurBuffer(tPSBuffer *PSBuffer);
void PSBuffer_StartNewBuffer(tPSBuffer *PSBuffer, bool ResetContinuity);
void PSBuffer_ProcessTSPacket(tPSBuffer *PSBuffer, tTSPacket *Packet);

#endif
