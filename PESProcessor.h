#ifndef __PESPROCESSORH__
#define __PESPROCESSORH__

#define PCRTOPTSOFFSET 40000000

typedef struct
{
  word                  PID;
  bool                  TablePacket;
  int                   ValidBuffer;    //0: no data yet, Buffer1 gets filled
                                        //1: Buffer1 is valid, Buffer2 gets filled
                                        //2: Buffer2 is valid, Buffer1 gets filled
  int                   BufferSize;
  int                   BufferPtr;
  bool                  StartImmediate;
  bool                  IgnoreContErrors;
  bool                  ValidDiscontinue;
  bool                  NewDiscontinue;
  int                   ValidBufLen;
  byte                 *pBuffer;
  byte                  LastCCCounter;
  byte                 *Buffer1, *Buffer2;
//  int                   PSFileCtr;
//  long long             ValidPCR;
//  long long             NewPCR;
  bool                  ValidPayloadStart;
  bool                  NewPayloadStart;
  bool                  ErrorFlag;
#ifdef _DEBUG
  int                   maxPESLen;
#endif
} tPSBuffer;


bool PSBuffer_Init(tPSBuffer *PSBuffer, word PID, int BufferSize, bool TablePacket, bool DropBufferOnErr, bool SkipFirstIncomplete);
void PSBuffer_Reset(tPSBuffer *PSBuffer);
void PSBuffer_StartNewBuffer(tPSBuffer *PSBuffer, bool SkipFirstIncomplete, bool ResetContinuity);
void PSBuffer_ProcessTSPacket(tPSBuffer *PSBuffer, tTSPacket *Packet, long long FilePosition);
void PSBuffer_DropCurBuffer(tPSBuffer *PSBuffer);


void PESMuxer_Init(byte *PESBuffer, word PID, bool pPayloadStart, bool pDiscontinuity);
void PESMuxer_StartNewFile(void);
bool PESMuxer_NextTSPacket(tTSPacket *const outPacket, int *const PESBufLen);

#endif
