#ifndef __RECHEADERH__
#define __RECHEADERH__

//#include                "../../../../../Topfield/FireBirdLib/flash/FBLib_flash.h"

typedef struct
{
  char                  Magic[4];               // "TFrc"
  word                  Version;                // 0x8000
  byte                  Unknown1;
  byte                  rbn_HasBeenScanned:1;
  byte                  iqt_UnencryptedRec:1;
  byte                  rs_HasBeenStripped:1;
  byte                  rs_ToBeStripped:1;
  byte                  Reserved:4;
  dword                 StartTime;
  word                  DurationMin;
  word                  DurationSec;

  word                  CryptFlag:2;            // 0=FTA, 1=scrambled, 2=descrambled, 3=partly scrambled
  word                  Flags:6;
  word                  FlagsUnused:5;
  word                  FlagUnknown:1;
  word                  FlagTimeShift:1;
  word                  FlagCopy:1;

  byte                  Unknown2[10];
} TYPE_RecHeader_Info;

typedef struct
{
  byte                  SatIndex;
  byte                  ServiceType;

  word                  TPIndex:10;   // Reihenfolge?? - stimmt mit DecodeRecHeader() überein!
  word                  FlagTuner:2;
  word                  FlagSkip:1;  // Reihenfolge der Flags geändert
  word                  FlagLock:1;
  word                  FlagCAS:1;
  word                  FlagDel:1;

  word                  ServiceID;
  word                  PMTPID;
  word                  PCRPID;
  word                  VideoPID;
  word                  AudioPID;

  char                  ServiceName[24];

  byte                  VideoStreamType;    //see tap.h for possible video and audio stream types
  byte                  AudioStreamType;
} TYPE_Service_Info;

typedef struct
{
  byte                  Unknown1[2];
  byte                  DurationMin;
  byte                  DurationHour;
  dword                 EventID;
  dword                 StartTime;
  dword                 EndTime;
  byte                  RunningStatus;
  byte                  EventNameLength;
  byte                  ParentalRate;
  char                  EventNameDescription[257];
  word                  ServiceID;
  byte                  Unknown2[14];
} TYPE_Event_Info;

typedef struct
{
  word                  ServiceID;
  word                  TextLength;
  dword                 EventID;
  char                  Text[1024];
} TYPE_ExtEvent_Info;

/* typedef struct
{
  word                  NrOfImages;         // Check if the inf file size is at least 10322 bytes to
                                            // make sure that NrOfImages is valid
  word                  unknown1;             
  dword                 unknown2;
  dword                 unknown3;
  dword                 PixelData1[196*156];  //ARGB
  dword                 unknown4;
  dword                 unknown5;
  dword                 PixelData2[196*156];  //ARGB
  dword                 unknown6;
  dword                 unknown7;
  dword                 PixelData3[196*156];  //ARGB
  dword                 unknown8;
  dword                 unknown9;
  dword                 PixelData4[196*156];  //ARGB
} tPreviewImages; */

typedef struct
{
  dword                 SatIndex:8;
  dword                 Polarization:1;       // 0=V, 1=H
  dword                 TPMode:3;             // TPMode ist entweder 000 für "normal" oder 001 für "SmaTV" ("SmaTV" kommt in der Realität nicht vor)
  dword                 ModulationSystem:1;   // 0=DVBS, 1=DVBS2
  dword                 ModulationType:2;     // 0=Auto, 1=QPSK, 2=8PSK, 3=16QAM
  dword                 FECMode:4;            // 0x0 = AUTO, 0x1 = 1_2, 0x2 = 2_3, 0x3 = 3_4,
                                              // 0x4 = 5_6 , 0x5 = 7_8, 0x6 = 8_9, 0x7 = 3_5,
                                              // 0x8 = 4_5, 0x9 = 9_10, 0xa = reserved, 0xf = NO_CONV
  dword                 Pilot:1;
  dword                 UnusedFlags1:4;
  dword                 Unknown2:8;
  dword                 Frequency;
  word                  SymbolRate;
  word                  TSID;
  word                  ClockSync:1;
  word                  UnusedFlags2:15;
  word                  OriginalNetworkID;
} TYPE_TpInfo_TMSS;

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
} TYPE_TpInfo_TMST;

typedef struct
{
  dword                 SatIdx:8;      // immer 0 (das niedrigst wertige Byte)
  dword                 Frequency:24;  // nur die 3 hochwertigen Bytes -> muss durch 256 geteilt werden
  word                  SymbolRate;
  word                  TSID;
  word                  OriginalNetworkID;
  byte                  ModulationType;
  byte                  unused1;
} TYPE_TpInfo_TMSC;

typedef struct
{
  dword                 NrBookmarks;
  dword                 Bookmarks[177];
  dword                 Resume;
} TYPE_Bookmark_Info;

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
//  tPreviewImages        PreviewImages;
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
//  tPreviewImages        PreviewImages;
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
//  tPreviewImages        PreviewImages;
} TYPE_RecHeader_TMST;

#endif
