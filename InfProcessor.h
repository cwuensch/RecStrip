#ifndef __INFPROCESSORH__
#define __INFPROCESSORH__

typedef enum
{
  ST_UNKNOWN,
  ST_S,
  ST_T,
  ST_C,
  ST_T5700,
  ST_TMSS,
  ST_TMST,
  ST_TMSC,
  ST_T5800,
  ST_ST,
  ST_CT,
  ST_TF7k7HDPVR,
  ST_NRTYPES
} SYSTEM_TYPE;

typedef struct
{
  char                  HeaderMagic[4];
  word                  HeaderVersion;
  byte                  HeaderUnknown2[2];
  dword                 HeaderStartTime;
  word                  HeaderDuration;
  word                  HeaderDurationSec;

  word                  CryptFlag:2;  // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  Flags:6;
  word                  Flags2:6;
  word                  TSFlag:1;
  word                  CopyFlag:1;

  byte                  HeaderUnknown4[6];
  dword                 RecStripFlag;
} TYPE_RecHeader_Info;

typedef struct
{
  byte                  SatIdx;
  byte                  ServiceType;

  word                  TPIdx:10;   // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  TunerNum:2;
  word                  DelFlag:1;
  word                  CASFlag:1;
  word                  LockFlag:1;
  word                  SkipFlag:1;

  word                  SVCID;
  word                  PMTPID;
  word                  PCRPID;
  word                  VideoPID;
  word                  AudioPID;

  char                  ServiceName[24];

  byte                  VideoStreamType;
  byte                  AudioStreamType;
} TYPE_Service_Info;

typedef struct
{
  byte                  EventUnknown1[2];
  byte                  EventDurationMin;
  byte                  EventDurationHr;
  dword                 EventID;
  dword                 EventStartTime;
  dword                 EventEndTime;
  byte                  EventRunningStatus;
  byte                  EventTextLength;
  byte                  EventParentalRate;
  char                  EventNameDescription[273];
} TYPE_Event_Info;

typedef struct
{
  word                  ExtEventServiceID;
  word                  ExtEventTextLength;
  dword                 ExtEventEventID;
  char                  ExtEventText[1024];
} TYPE_ExtEvent_Info;

typedef struct
{
  dword                 NrBookmarks;
  dword                 Bookmarks[177];
  dword                 Resume;
} TYPE_Bookmark_Info;

typedef struct
{
  byte                  SatIdx;
  word                  Polar:1;              // 0=V, 1=H
  word                  unused1:3;
  word                  ModulationSystem:1;   // 0=DVBS, 1=DVBS2
  word                  ModulationType:2;     // 0=Auto, 1=QPSK, 2=8PSK, 3=16QAM
  word                  FECMode:4;            // 0x0 = AUTO, 0x1 = 1_2, 0x2 = 2_3, 0x3 = 3_4,
                                              // 0x4 = 5_6 , 0x5 = 7_8, 0x6 = 8_9, 0x7 = 3_5,
                                              // 0x8 = 4_5, 0x9 = 9_10, 0xa = reserved, 0xf = NO_CONV
  word                  Pilot:1;
  word                  unused2:4;
  byte                  unused3;
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  AllowTimeSync:1;
  word                  unused4:15;
  word                  OriginalNetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMSS;

typedef struct
{
  byte                  SatIdx;
  byte                  ChannelNr;
  byte                  Bandwidth;
  byte                  unused1;
  dword                 Frequency;
  word                  TSID;
  byte                  LPHP;
  byte                  unused2;
  word                  OriginalNetworkID;
  word                  NetworkID;
}__attribute__((packed)) TYPE_TpInfo_TMST;

typedef struct
{
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  OriginalNetworkID;
  byte                  ModulationType;
  byte                  unused1;
}__attribute__((packed)) TYPE_TpInfo_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSS      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSS;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMSC      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMSC;

typedef struct
{
  TYPE_RecHeader_Info   RecHeaderInfo;
  TYPE_Service_Info     ServiceInfo;
  TYPE_Event_Info       EventInfo;
  TYPE_ExtEvent_Info    ExtEventInfo;
  byte                  TpUnknown1[4];
  TYPE_TpInfo_TMST      TransponderInfo;
  TYPE_Bookmark_Info    BookmarkInfo;
//  byte                  HeaderUnused[8192];
//  word                  NrImages;
//  word                  Unknown1;
} TYPE_RecHeader_TMST;


bool LoadInfFile(const char *AbsInfName);
bool CloseInfFile(const char *AbsDestInf, const char *AbsSourceInf, bool Save);
void ProcessInfFile(const dword CurrentPosition, const dword PositionOffset);

#endif
