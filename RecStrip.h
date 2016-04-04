#ifndef __RECSTRIPH__
#define __RECSTRIPH__

#define VERSION                   "0.6"

#define NRBOOKMARKS                177   // eigentlich werden nur 48 Bookmarks unterstützt!! (SRP2401)
#define NRSEGMENTMARKER            101
#define BUFSIZE                  65536
#define FBLIB_DIR_SIZE             512
#define RECBUFFERENTRIES          5000

#define STREAM_VIDEO_MPEG1        0x01
#define STREAM_VIDEO_MPEG2        0x02
#define STREAM_VIDEO_MPEG4_PART2  0x10
#define STREAM_VIDEO_MPEG4_H263   0x1A
#define STREAM_VIDEO_MPEG4_H264   0x1B
#define STREAM_VIDEO_VC1          0xEA
#define STREAM_VIDEO_VC1SM        0xEB
#define STREAM_UNKNOWN            0xFF


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
  
  byte PID2:8;
  
  byte ContinuityCount:4;
  byte Payload_Exists:1;
  byte Adapt_Field_Exists:1;
  byte Scrambling_Ctrl:2;
  
  byte Data[184];
} tTSPacket;

/*typedef struct
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
} tPSBuffer; */



// Globale Variablen
extern char             RecFileIn[], RecFileOut[];
extern SYSTEM_TYPE      SystemType;
extern byte             PACKETSIZE, PACKETOFFSET;
extern word             VideoPID;
extern bool             isHDVideo, AlreadyStripped;
extern bool             DoStrip, DoCut;


bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize);
bool isPacketStart(const byte PacketArray[], int ArrayLen);
int  GetPacketSize(char *RecFileName);
int  main(int argc, const char* argv[]);

#endif
