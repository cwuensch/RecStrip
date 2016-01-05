#ifndef __RECSTRIPH__
#define __RECSTRIPH__

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



// Globale Variablen
extern char             RecFileIn[], RecFileOut[];
extern word             VideoPID;
extern bool             isHDVideo;
extern byte             PACKETSIZE, PACKETOFFSET;


bool HDD_GetFileSize(const char *AbsFileName, unsigned long long *OutFileSize);
bool isPacketStart(const byte PacketArray[], int ArrayLen);
int  GetPacketSize(char *RecFileName);
int  main(int argc, const char* argv[]);

#endif
