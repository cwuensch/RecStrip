#ifndef __REBUILDINFH__
#define __REBUILDINFH__

typedef enum
{
  TABLE_PAT              = 0x00,
  TABLE_PMT              = 0x02,
  TABLE_SDT              = 0x42,
  TABLE_EIT              = 0x4e
} TableIDs;

typedef enum
{
  DESC_Audio             = 0x0A,
  DESC_EITShortEvent     = 'M',  // 0x4d
  DESC_EITExtEvent       = 'N',  // 0x4e
  DESC_Service           = 'H',  // 0x48
  DESC_Teletext          = 'V',  // 0x56
  DESC_Subtitle          = 'Y',  // 0x59
  DESC_AC3               = 0x6A,
} DescrTags;


typedef struct
{
  byte TableID;
  byte SectionLen1:4;    // first 2 bits are 0
  byte Reserved1:2;      // = 0x03 (all 1)
  byte Private:1;        // = 0
  byte SectionSyntax:1;  // = 1
  byte SectionLen2;
  byte ProgramNr1;
  byte ProgramNr2;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved2:2;      // = 0x03 (all 1)
  byte SectionNr;
  byte LastSection;

  word PCRPID1:5;
  word Reserved3:3;      // = 0x07 (all 1)
  word PCRPID2:8;

  word ProgInfoLen1:4;   // first 2 bits are 0
  word Reserved4:4;      // = 0x0F (all 1)
  word ProgInfoLen2:8;
  // ProgInfo (of length ProgInfoLen1*256 + ProgInfoLen2)
  // Elementary Streams (of remaining SectionLen)
} tTSPMT;

typedef struct
{
  byte stream_type;
  byte ESPID1:5;
  byte Reserved1:3;      // = 0x03 (all 1)
  byte ESPID2;
  byte ESInfoLen1:4;
  byte Reserved2:4;      // = 0x07 (all 1)
  byte ESInfoLen2;
} tElemStream;


typedef struct
{
  byte TableID;
  byte SectionLen1:4;    // first 2 bits are 0
  byte Reserved1:2;      // = 0x03 (all 1)
  byte Private:1;        // = 1
  byte SectionSyntax:1;
  byte SectionLen2;
  byte TS_ID1;
  byte TS_ID2;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved2:2;      // = 0x03 (all 1)
  byte SectionNr;
  byte LastSection;
  byte OrigNetworkID1;
  byte OrigNetworkID2;
  byte Reserved;
  // Services
} tTSSDT;

typedef struct
{
  byte ServiceID1;
  byte ServiceID2;
  byte EITPresentFollow:1;
  byte EITSchedule:1;
  byte Reserved2:6;
  byte DescriptorsLen1:4;
  byte FreeCAMode:1;
  byte RunningStatus:3;
  byte DescriptorsLen2;
  // Descriptors
} tTSService;

typedef struct
{
  byte DescrTag;
  byte DescrLen;
  byte ServiceType;
  byte ProviderNameLen;
  char ProviderName;
//  byte ServiceNameLen;
//  char ServiceName[];
} tTSServiceDesc;


typedef struct
{
  byte TableID;
  byte SectionLen1:4;    // first 2 bits are 0
  byte Reserved1:2;      // = 0x03 (all 1)
  byte Private:1;        // = 1
  byte SectionSyntax:1;
  byte SectionLen2;
  byte ServiceID1;
  byte ServiceID2;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved2:2;      // = 0x03 (all 1)
  byte SectionNr;
  byte LastSection;
  byte TS_ID1;
  byte TS_ID2;
  byte OriginalID1;
  byte OriginalID2;
  byte SegLastSection;
  byte LastTable;
  // Events
} tTSEIT;

typedef struct
{
  byte EventID1;
  byte EventID2;
  
  byte StartTime[5];

  byte DurationSec[3];

  byte DescriptorLoopLen1:4;
  byte FreeCAMode:1;
  byte RunningStatus:3;
  byte DescriptorLoopLen2;
  // Descriptors
} tEITEvent;

typedef struct
{
  byte DescrTag;
  byte DescrLength;
  char LanguageCode[3];
  byte EvtNameLen;
//  char EvtName[];
//  byte EvtTextLen;
//  char EvtText[];
} tShortEvtDesc;

typedef struct
{
  byte DescrTag;
  byte DescrLength;
  byte LastDescrNr:4;
  byte DescrNr:4;
  char LanguageCode[3];
  byte ItemsLen;
//  for (i=0; i<N; i++) {
    byte ItemDescLen;
    char ItemDesc; //[];
//    byte ItemLen;
//    char Item[];    
//  }
//  byte TextLen;
//  char Text[];
} tExtEvtDesc;

typedef struct
{
  byte DescrTag;
  byte DescrLength;
  // Data of length DescrLength
} tTSDesc;


//extern FILE            *fIn;  // dirty Hack

bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf);

#endif
