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
  DESC_AC3               = 0x6A
} DescrTags;

typedef enum
{
  AR_FORBIDDEN           = 0,
  AR_1to1                = 1,  // 1:1
  AR_4to3                = 2,  // 4:3 = 1.333
  AR_16to9               = 3,  // 16:9 = 1.778
  AR_221to100            = 4,  // 2.21:1 = 2.21
  AR_RESERVED            = 5
} AspectRatios;

typedef enum
{
  FR_FORBIDDEN           = 0,
  FR_NTSC_23             = 1,  // 23.976 fps
  FR_CINEMA_24           = 2,  // 24 fps
  FR_PAL_25              = 3,  // 25 fps
  FR_NTSC_29             = 4,  // 29.970 fps
  FR_NTSC_30             = 5,  // 30 fps
  FR_PAL_50              = 6,  // 50 fps
  FR_NTSC_59             = 7,  // 59.940 fps
  FR_NTSC_60             = 8,  // 60 fps
  FR_RESERVED            = 9
} FrameRates;


#pragma pack(push, 1)

typedef struct
{
  byte TableID;
  byte SectionLen1:4;    // first 2 bits are 0
  byte Reserved1:2;      // = 0x03 (all 1)
  byte Private:1;        // = 0
  byte SectionSyntax:1;  // = 1
  byte SectionLen2;
  byte TS_ID1;
  byte TS_ID2;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved2:2;      // = 0x03 (all 1)
  byte SectionNr;
  byte LastSection;

//  for i = 0 to N  {
    word ProgramNr1:8;
    word ProgramNr2:8;
    word PMTPID1:5;  // oder NetworkPID, falls ProgramNr==0
    word Reserved111:3;
    word PMTPID2:8;  // oder NetworkPID, falls ProgramNr==0
//  }
  dword CRC32;
} tTSPAT;

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
  byte DescrTag;
  byte DescrLength;
//  char Name[4];          // without terminating 0
  byte Reserved:4;
  byte asvc_flag:1;
  byte mainid_flag:1;
  byte bsid_flag:1;
  byte component_type_flag:1;
} tTSAC3Desc;            // Ist das richtig??

typedef struct
{
  byte DescrTag;
  byte DescrLength;
  char LanguageCode[3]; // without terminating 0
  byte AudioType;
} tTSAudioDesc;

typedef struct
{
  byte DescrTag;
  byte DescrLength;
  char LanguageCode[3];  // without terminating 0
  byte TtxMagazine:1;    // = 1
  byte Unknown2:2;       // = 0
  byte TtxType:2;        // 1 = initial Teletext page
  byte Unknown:3;        // unknown
  byte FirstPage;
} tTSTtxDesc;


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

// siehe: http://stnsoft.com/DVD/mpeghdrs.html
typedef struct
{
  byte                  StartCode[3];
  byte                  HeaderID;

  byte                  Width1;
  byte                  Height1:4;
  byte                  Width2:4;
  byte                  Height2;

  byte                  FrameRate:4;
  byte                  AspectRatio:4;

  byte                  Bitrate1;
  byte                  Bitrate2;

  byte                  VBV1:5;
  byte                  Marker:1;
  byte                  Bitrate3:2;

  byte                  IntraMatrix1:1;
  byte                  Laden1:1;
  byte                  CPF:1;
  byte                  VBV2:5;

  byte                  IntraMatrix2;
  byte                  IntraMatrix3;
  byte                  Laden2:1;
  byte                  IntraMatrix4:7;
  byte                  NonIntraMatrix:3;
} tSequenceHeader;

// siehe: https://en.wikipedia.org/wiki/MPEG_elementary_stream
typedef struct
{
  byte                  StartCode1;       // StartCode: 0xFFF (12 bits)

  byte                  CRCprotection:1;  // 0=CRC-protected, 1=no-protection
  byte                  Layer:2;          // 3=Layer1, 2=Layer2, 1=Layer3
  byte                  MpegVersion:1;    // 1=MPEG1, 0=MPEG2
  byte                  StartCode2:4;

  byte                  Private:1;
  byte                  Padding:1;
  byte                  SamplingFreq:2;   // 0=44.1 kHz, 1=48 kHz, 2=32 kHz
  byte                  BitrateIndex:4;

  byte                  Emphasis:2;
  byte                  Original:1;       // 0=copy, 1=original
  byte                  Copyright:1;      // 0=none, 1=yes
  byte                  ModeExtension:2;
  byte                  Mode:2;           // 0=Stereo, 1=Joint Stereo, 2=Dual Channel, 3=Single Channel
} __attribute__((packed)) tAudioHeader;

// siehe: http://stnsoft.com/DVD/dtshdr.html
typedef struct
{
  byte                  StartCode[4];       // StartCode: 0x7FFE8001 (32 bits)

  byte                  nblks1:1;
  byte                  cpf:1;
  byte                  shrt:5;
  byte                  ftype:1;

  byte                  fsize1:2;
  byte                  nblks2:6;

  byte                  fsize2;

	byte                  amode1:4;
  byte                  fsize3:4;

  byte                  rate1:2;
  byte                  sfreq:4;
	byte                  amode2:2;

  byte                  hdcd:1;
  byte                  auxf:1;
  byte                  timef:1;
  byte                  dynf:1;
  byte                  mix:1;
  byte                  rate2:3;
} tDTSHeader;
#pragma pack(pop)


#define                 EPGBUFFERSIZE 4097

//extern FILE            *fIn;  // dirty Hack
extern long long        FirstFilePCR, LastFilePCR;
extern dword            FirstFilePTS, LastFilePTS;
extern int              VideoHeight, VideoWidth;
extern double           VideoFPS, VideoDAR;
//extern int              TtxTimeZone;

time_t TF2UnixTime(tPVRTime TFTimeStamp, byte TFTimeSec, bool convertToUTC);
tPVRTime Unix2TFTime(time_t UnixTimeStamp, byte *const outSec, bool convertToLocal);
tPVRTime EPG2TFTime(tPVRTime TFTimeStamp, int *const out_timeoffset);
tPVRTime AddTimeSec(tPVRTime pvrTime, byte pvrTimeSec, byte *const outSec, int addSeconds);
void InitInfStruct(TYPE_RecHeader_TMSS *RecInf);
bool GenerateInfFile(FILE *fIn, TYPE_RecHeader_TMSS *RecInf);
bool AnalysePMT(byte *PSBuffer, int BufSize, TYPE_RecHeader_TMSS *RecInf);

void SortAudioPIDs(tAudioTrack AudioPIDs[]);
void GeneratePatPmt(byte *const PATPMTBuf, word ServiceID, word PMTPID, word VideoPID, word AudioPID, word TtxPID, tAudioTrack AudioPIDs[]);

#endif
