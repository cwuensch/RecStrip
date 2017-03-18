#ifndef __REBUILDINFH__
#define __REBUILDINFH__

typedef struct
{
  word                  PID;
  bool                  TablePacket;
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
  byte SectionLen1:4;
  byte Reserved1:2;
  byte Reserved0:1;
  byte SectionSyntax:1;
  byte SectionLen2;
  byte TS_ID1;
  byte TS_ID2;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved11:2;
  byte SectionNr;
  byte LastSection;
  byte OrigNetworkID1;
  byte OrigNetworkID2;
  byte Reserved;
} TTSSDT;

typedef struct
{
  byte ServiceID1;
  byte ServiceID2;
  byte EITPresentFollow:1;
  byte EITSchedule:1;
  byte Reserved2:6;
  byte DescriptorLen1:4;
  byte FreeCAMode:1;
  byte RunningStatus:3;
  byte DescriptorLen2;
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
