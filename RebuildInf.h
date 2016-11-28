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


typedef struct
{
  byte TableID;
  word SectionLen1:4;
  word Reserved1:2;
  word Reserved0:1;
  word SectionSyntax:1;
  word SectionLen2:8;
  word TS_ID1:8;
  word TS_ID2:8;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved11:2;
  byte SectionNr;
  byte LastSection;
  word OrigNetworkID1:8;
  word OrigNetworkID2:8;
  byte Reserved;
} TTSSDT;

typedef struct
{
  word ServiceID1:8;
  word ServiceID2:8;
  byte EITPresentFollow:1;
  byte EITSchedule:1;
  byte Reserved2:6;
  word DescriptorLen1:4;
  word FreeCAMode:1;
  word RunningStatus:3;
  word DescriptorLen2:8;
} TTSService;

typedef struct
{
  byte DescriptorTag;
  byte DescriptorLen;
  byte ServiceType;
  byte ProviderNameLen;
  char ProviderName;
//  byte ServiceNameLen;
//  char ServiceName[4];
} TTSServiceDesc;


extern FILE            *fIn;  // dirty Hack

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf);

#endif
