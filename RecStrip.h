#ifndef __RECSTRIPH__
#define __RECSTRIPH__

#include "RecHeader.h"
#include <time.h>

#define VERSION                  "v2.9"

#define NRBOOKMARKS                177   // eigentlich werden nur 48 Bookmarks unterstützt!! (SRP2401)
#define NRSEGMENTMARKER            101
#define MAXCONTINUITYPIDS            8
#define BUFSIZE                  65536
#define FBLIB_DIR_SIZE             512
//#define RECBUFFERENTRIES          5000
#define PENDINGBUFSIZE           65536
#define VIDEOBUFSIZE           2097152
#define CONT_MAXDIST             38400

//audio & video format
typedef enum
{
  STREAM_AUDIO_MP3              = 0x01,
  STREAM_AUDIO_MPEG1            = 0x03,
  STREAM_AUDIO_MPEG2            = 0x04,
  STREAM_AUDIO_MPEG4_AC3_PLUS   = 0x06,
  STREAM_AUDIO_MPEG4_AAC        = 0x0F,
  STREAM_AUDIO_MPEG4_AAC_PLUS   = 0x11,
  STREAM_AUDIO_MPEG4_AC3        = 0x81,
  STREAM_AUDIO_MPEG4_DTS        = 0x82
} tAudioStreamFmt;
typedef enum
{
  STREAM_VIDEO_MPEG1            = 0x01,
  STREAM_VIDEO_MPEG2            = 0x02,
  STREAM_VIDEO_MPEG4_PART2      = 0x10,
  STREAM_VIDEO_MPEG4_H263       = 0x1A,
  STREAM_VIDEO_MPEG4_H264       = 0x1B,
  STREAM_VIDEO_VC1              = 0xEA,
  STREAM_VIDEO_VC1SM            = 0xEB,
  STREAM_UNKNOWN                = 0xFF
} tVideoStreamFmt;

typedef enum
{
  STREAMTYPE_UNKNOWN            = 0,
  STREAMTYPE_AUDIO              = 1,
  STREAMTYPE_TELETEXT           = 2,
  STREAMTYPE_SUBTITLE           = 3
} tStreamType;


#if defined(WIN32) || defined(_WIN32) 
  #define PATH_SEPARATOR '\\' 
  #define fstat64 _fstat64
  #define stat64 _stat64
  #define fseeko64 _fseeki64
  #define ftello64 _ftelli64
//  #define fopen fopen_s
//  #define strncpy strncpy_s
//  #define sprintf sprintf_s
#else 
  #define PATH_SEPARATOR '/'
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
  extern int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap);
  extern int c99_snprintf(char *outBuf, size_t size, const char *format, ...);

  #define snprintf c99_snprintf
  #define vsnprintf c99_vsnprintf
#endif


#define TRACEENTER // printf("Start %s \n", (char*)__FUNCTION__)
#define TRACEEXIT  // printf("End %s \n", (char*)__FUNCTION__)

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
  char SyncByte;  // = 'G'
  byte PID1:5;
  byte Transport_Prio:1;
  byte Payload_Unit_Start:1;
  byte Transport_Error:1;
  
  byte PID2;
  
  byte ContinuityCount:4;
  byte Payload_Exists:1;
  byte Adapt_Field_Exists:1;
  byte Scrambling_Ctrl:2;
  
  byte Data[184];
} tTSPacket;

typedef struct
{
  long long             Position;
  dword                 Timems; //Time in ms
  float                 Percent;
  int                   Selected;
  char                 *pCaption;
} tSegmentMarker2;

typedef struct
{
  word PID;
  long long int Position;
  byte CountIs;
  byte CountShould;
} tContinuityError;

typedef struct
{
  word pid;
  byte type;          // 1=mpeg1, 0=mpeg2
  byte layer: 2;      // 3=Layer1, 2=Layer2, 1=Layer3
  byte mode: 2;       // 0=Stereo, 1=Joint stereo, 2=Dual channel, 3=Single channel
  byte bitrate: 4;
  byte scanned: 1;    // Flags: 0=toScan, 1=scanned, 2=noAudio
  byte sorted: 1;
  byte streamType: 2; // Flags: 0=Audio, 1=Teletext, 2=Subtitles, 3=unknown
  byte streamId: 4;   // neu: StreamID aus der Original-PMT eintragen
  char desc[4];
  byte desc_flag;
} tAudioTrack;


// Globale Variablen
extern char             RecFileIn[], RecFileOut[];
extern byte            *PATPMTBuf, *EPGPacks;
extern const char      *ExePath;
extern unsigned long long RecFileSize;
extern time_t           RecFileTimeStamp;
extern SYSTEM_TYPE      SystemType;
extern byte             PACKETSIZE, PACKETOFFSET, OutPacketSize;
extern word             VideoPID, TeletextPID, SubtitlesPID, TeletextPage;
extern word             TransportStreamID;
extern tAudioTrack      AudioPIDs[];
extern word             ContinuityPIDs[MAXCONTINUITYPIDS], NrContinuityPIDs;
extern bool             isHDVideo, AlreadyStripped, HumaxSource, EycosSource, DVBViewerSrc;
extern bool             DoStrip, DoSkip, RemoveEPGStream, ExtractTeletext, ExtractAllTeletext, RemoveTeletext, RebuildNav, RebuildInf, RebuildSrt, DoInfoOnly, DoFixPMT, MedionMode, MedionStrip, WriteDescPackets, PMTatStart;
extern int              DoCut, DoMerge, DoInfFix, DemuxAudio;
extern int              NrEPGPacks;
extern int              dbg_DelBytesSinceLastVid;

extern TYPE_Bookmark_Info *BookmarkInfo, BookmarkInfo_In;
extern tSegmentMarker2 *SegmentMarker;       //[0]=Start of file, [x]=End of file
extern int              NrSegmentMarker;
extern long long        NrDroppedZeroStuffing;
extern int              ActiveSegment;
extern dword            InfDuration, NewDurationMS, NavFrames;
extern int              NavDurationMS;
extern int              NewStartTimeOffset;
extern dword            TtxPTSOffset;
extern long long        CurrentPosition;
extern char            *ExtEPGText;


bool StrToUTF8(char *destination, const char *source, size_t num, byte DefaultISO8859CharSet);

dword CalcBlockSize(long long Size);
bool HDD_FileExist(const char *AbsFileName);
bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize);
void AddContinuityPids(word newPID, bool first);
void AddContinuityError(word CurPID, long long CurrentPosition, byte CountShould, byte CountIs);
bool isPacketStart(const byte PacketArray[], int ArrayLen);        // braucht 9*192+5 = 1733 / 3*192+5 = 581
int  FindNextPacketStart(const byte PacketArray[], int ArrayLen);  // braucht [ 20*192 = 3840 / 10*188 + 1184 = 3064 ] + 1733
int  FindPrevPacketStart(const byte PacketArray[], int ArrayLen);  // braucht [ 20*192 = 3840 / 10*188 + 1184 = 3064 ] + 1733
//int  GetPacketSize(FILE *RecFile, int *OutOffset);
void DeleteSegmentMarker(int MarkerIndex, bool FreeCaption);
int  main(int argc, const char* argv[]);

#endif
