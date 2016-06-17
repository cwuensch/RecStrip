#ifndef __REBUILDINFH__
#define __REBUILDINFH__

typedef struct
{
  word                  PID;
  int                   ValidBuffer;    //0: no data yet, Buffer1 gets filled
                                        //1: Buffer1 is valid, Buffer2 gets filled
                                        //2: Buffer2 is valid, Buffer1 gets filled
  int                   BufferSize;
  int                   BufferPtr;
  byte                 *pBuffer;
  byte                  LastCCCounter;
  byte                 *Buffer1, *Buffer2;
  int                   PSFileCtr;
  byte                  ErrorFlag;
} tPSBuffer;


extern FILE            *fIn;  // dirty Hack

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf);

#endif
