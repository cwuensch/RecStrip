#ifndef __RECSTRIPH__
#define __RECSTRIPH__

#include "RecHeader.h"

#define VERSION                  "v2.3"

#define NRBOOKMARKS                177   // eigentlich werden nur 48 Bookmarks unterstützt!! (SRP2401)
#define NRSEGMENTMARKER            101
#define MAXCONTINUITYPIDS            8
#define BUFSIZE                  65536
#define FBLIB_DIR_SIZE             512
#define RECBUFFERENTRIES          5000
#define PENDINGBUFSIZE           65536

//audio & video format
#define STREAM_AUDIO_MP3             0x01
#define STREAM_AUDIO_MPEG1           0x03
#define STREAM_AUDIO_MPEG2           0x04
#define STREAM_AUDIO_MPEG4_AC3_PLUS  0x06
#define STREAM_AUDIO_MPEG4_AAC       0x0F
#define STREAM_AUDIO_MPEG4_AAC_PLUS  0x11
#define STREAM_AUDIO_MPEG4_AC3       0x81
#define STREAM_AUDIO_MPEG4_DTS       0x82

#define STREAM_VIDEO_MPEG1           0x01
#define STREAM_VIDEO_MPEG2           0x02
#define STREAM_VIDEO_MPEG4_PART2     0x10
#define STREAM_VIDEO_MPEG4_H263      0x1A
#define STREAM_VIDEO_MPEG4_H264      0x1B
#define STREAM_VIDEO_VC1             0xEA
#define STREAM_VIDEO_VC1SM           0xEB
#define STREAM_UNKNOWN               0xFF


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


// Globale Variablen
extern char             RecFileIn[], RecFileOut[];
extern unsigned long long RecFileSize;
extern SYSTEM_TYPE      SystemType;
extern byte             PACKETSIZE, PACKETOFFSET;
extern word             VideoPID, TeletextPID;
extern word             ContinuityPIDs[MAXCONTINUITYPIDS], NrContinuityPIDs;
extern bool             isHDVideo, AlreadyStripped, HumaxSource;
extern bool             DoStrip, RemoveEPGStream, RemoveTeletext, RebuildNav, RebuildInf;
extern int              DoCut, DoMerge;
extern int              dbg_DelBytesSinceLastVid;

extern TYPE_Bookmark_Info *BookmarkInfo, BookmarkInfo_In;
extern tSegmentMarker2 *SegmentMarker,  *SegmentMarker_In;       //[0]=Start of file, [x]=End of file
extern int              NrSegmentMarker, NrSegmentMarker_In;
extern int              ActiveSegment;
extern dword            InfDuration, NewDurationMS, NewStartTimeOffset;


bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize);
bool isPacketStart(const byte PacketArray[], int ArrayLen);        // braucht 9*192+5 = 1733 / 3*192+5 = 581
int  FindNextPacketStart(const byte PacketArray[], int ArrayLen);  // braucht 20*192+1733 = 5573 / 1185+1733 = 2981
//int  GetPacketSize(FILE *RecFile, int *OutOffset);
void DeleteSegmentMarker(int MarkerIndex, bool FreeCaption);
int  main(int argc, const char* argv[]);

#endif
