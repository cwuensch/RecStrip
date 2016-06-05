#ifndef __INFPROCESSORH__
#define __INFPROCESSORH__

#include "RecStrip.h"

typedef struct
{
  char                  HeaderMagic[4];
  word                  HeaderVersion;
  byte                  HeaderUnknown2;
  byte                  rbn_HasBeenScanned:1;
  byte                  iqt_UnencryptedRec:1;
  byte                  rs_HasBeenStripped:1;
  byte                  rs_ToBeStripped:1;
  byte                  Reserved:4;
  dword                 HeaderStartTime;
  word                  HeaderDuration;
  word                  HeaderDurationSec;

  word                  CryptFlag:2;  // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  Flags:6;
  word                  Flags2:6;
  word                  TSFlag:1;
  word                  CopyFlag:1;

  byte                  HeaderUnknown4[10];
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
  char                  EventNameDescription[257];
  word                  ServiceID;
  byte                  unknown[14];
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
  byte                  SatIdx;
  word                  Polar:1;              // 0=V, 1=H
  word                  TPMode:3;             // TPMode ist entweder 000 für "normal" oder 001 für "SmaTV" ("SmaTV" kommt in der Realität nicht vor)
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
  dword                 SatIdx:8;      // immer 0 (das niedrigst wertige Byte)
  dword                 Frequency:24;  // nur die 3 hochwertigen Bytes -> muss durch 256 geteilt werden
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
bool SetInfCryptFlag(const char *AbsInfFile);
bool CloseInfFile(const char *AbsDestInf, const char *AbsSourceInf, bool Save);

#endif
