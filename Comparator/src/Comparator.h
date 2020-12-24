#ifndef __COMPARATORH__
#define __COMPARATORH__

#ifdef _MSC_VER
  #define __attribute__(a)
#endif

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

#define FBLIB_DIR_SIZE      512
#define BUFSIZE         1048576
#define MAXPIDS              10
#define MAXDIST           38400

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
  word PID;
  long long int Position;
  byte CountIs;
  byte CountShould;
} tContinuityError;

#endif
