#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64
#ifdef _MSC_VER
  #define inline
  #define __attribute__(a)
#endif

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "type.h"
#include "TtxProcessor.h"
#include "PESProcessor.h"
#include "RecStrip.h"
#include "hamming.h"
#include "teletext.h"

#define NRTTXOUTPUTS  10
#define NRALLTTXPAGES 4096
#define NRSUBPAGES    16

// Globale Variablen
static FILE*            fTtxOut[NRTTXOUTPUTS] = {NULL};
static char             TeletextOut[FBLIB_DIR_SIZE];
static int              TeletextOutLen = 0;
static tPSBuffer        TtxBuffer;
static int              LastBuffer = 0;
static bool             FirstPacketAfterBreak = TRUE, ExtractAllOverwrite = FALSE;
static uint16_t         pages[] = { 0x777, 0x150, 0x151, 0x888, 0x160, 0x161, 0x152, 0x149, 0x571, 0 };

// global TS PCR value
uint32_t global_timestamp = 0;

// last timestamp computed
uint32_t last_timestamp = 0;


/*!
(c) 2011-2014 Forers, s. r. o.: telxcc

telxcc conforms to ETSI 300 706 Presentation Level 1.5: Presentation Level 1 defines the basic Teletext page,
characterised by the use of spacing attributes only and a limited alphanumeric and mosaics repertoire.
Presentation Level 1.5 decoder responds as Level 1 but the character repertoire is extended via packets X/26.
Selection of national option sub-sets related features from Presentation Level 2.5 feature set have been implemented, too.
(X/28/0 Format 1, X/28/4, M/29/0 and M/29/4 packets)

Further documentation:
ETSI TS 101 154 V1.9.1 (2009-09), Technical Specification
  Digital Video Broadcasting (DVB); Specification for the use of Video and Audio Coding in Broadcasting Applications based on the MPEG-2 Transport Stream
ETSI EN 300 231 V1.3.1 (2003-04), European Standard (Telecommunications series)
  Television systems; Specification of the domestic video Programme Delivery Control system (PDC)
ETSI EN 300 472 V1.3.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for conveying ITU-R System B Teletext in DVB bitstreams
ETSI EN 301 775 V1.2.1 (2003-05), European Standard (Telecommunications series)
  Digital Video Broadcasting (DVB); Specification for the carriage of Vertical Blanking Information (VBI) data in DVB bitstreams
ETS 300 706 (May 1997)
  Enhanced Teletext Specification
  https://www.etsi.org/deliver/etsi_i_ets/300700_300799/300706/01_40_57/ets_300706e01o.pdf
ETS 300 708 (March 1997)
  Television systems; Data transmission within Teletext
ISO/IEC STANDARD 13818-1 Second edition (2000-12-01)
  Information technology - Generic coding of moving pictures and associated audio information: Systems
ISO/IEC STANDARD 6937 Third edition (2001-12-15)
  Information technology - Coded graphic character set for text communication - Latin alphabet
Werner Br�ckner -- Teletext in digital television
*/
#define TELXCC_VERSION "2.6.0"

#ifdef __MINGW32__
// switch stdin and all normal files into binary mode -- needed for Windows
  #include <fcntl.h>
  int _CRT_fmode = _O_BINARY;

  #define WIN32_LEAN_AND_MEAN
  #define _WIN32_IE 0x0400
  #define ICC_STANDARD_CLASSES 0x00004000
  #include <windows.h>
//  #include <shellapi.h>
//  #include <commctrl.h>
  #include <wchar.h>
#endif

/*#define uint8_t byte
#define uint16_t word
#define uint32_t dword
#define uint64_t unsigned long long int
#define int8_t char
#define int16_t short
#define int32_t int
#define int64_t long long int*/

typedef enum {
  NO = 0x00,
  YES = 0x01,
  UNDEF = 0xff
} bool_t;

/*typedef enum {
  DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
  DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
  DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0c,
  DATA_UNIT_VPS = 0xc3,
  DATA_UNIT_CLOSED_CAPTIONS = 0xc5
} data_unit_t; */

typedef enum {
  TRANSMISSION_MODE_PARALLEL = 0,
  TRANSMISSION_MODE_SERIAL = 1
} transmission_mode_t;

typedef enum {
  COLOR_BLACK   = 0,
  COLOR_RED     = 1,
  COLOR_GREEN   = 2,
  COLOR_YELLOW  = 3,
  COLOR_BLUE    = 4,
  COLOR_MAGENTA = 5,
  COLOR_CYAN    = 6,
  COLOR_WHITE   = 7
} color_t;

static const char* TTXT_COLOURS[8] = {
  // black,     red,      green,    yellow,     blue,     magenta,    cyan,     white
  "#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff"
};

static const word TTXT_COLORSYMBOLS[2][8] = {
  { 0x25CF, 0x25D0, 0x25D1, 0x25D2, 0x25D3, 0x25D4, 0x25D5, 0x25CB },  // Foreground: ● Black, ◐ Red, ◑ Green, ◒ Yellow, ◓ Blue, ◔ Magenta, ◕ Cyan, ○ White*
  { 0x25A3, 0x25A4, 0x25A5, 0x25A6, 0x25A7, 0x25A8, 0x25A9, 0x25A1 }   // Background: ▣ Black*, ▤ Red, ▥ Green, ▦ Yellow, ▧ Blue, ▨ Magenta, ▩ Cyan, □ White
};

typedef struct {
  uint32_t show_timestamp; // show at timestamp (in ms)
  uint32_t hide_timestamp; // hide at timestamp (in ms)
  uint16_t text[25][40]; // 25 lines x 40 cols (1 screen/page) of wide chars
  uint8_t receiving_data;
  uint8_t tainted; // 1 = text variable contains any data
  uint32_t frames_produced;
  uint32_t last_show_timestamp;
  uint16_t last_text[25][40];
} teletext_page_t;

typedef struct {
  uint16_t text[25][40]; // 25 lines x 40 cols (1 screen/page) of wide chars
} teletext_text_t;

typedef struct {
  int16_t subpages[NRSUBPAGES];
} teletext_pagemap_t;

// application config global variable
static struct {
  uint8_t   verbose;       // should telxcc be verbose?
  uint16_t  page;          // teletext page containing cc we want to filter
  int32_t   offset;        // time offset in milliseconds
  uint8_t   colours;       // output <font...></font> tags
  uint32_t  utc_refvalue;  // UTC referential value
  // FIXME: move SE_MODE to output module
  uint8_t se_mode;
  //char *template; // output format template
} config = { NO, 0, 0, YES, NO, 0 };

/*
formatting template:
  %f -- from timestamp (absolute, UTC)
  %t -- to timestamp (absolute, UTC)
  %F -- from time (SRT)
  %T -- to time (SRT)
  %g -- from timestamp (relative)
  %u -- to timestamp (relative)
  %c -- counter 0-based
  %C -- counter 1-based
  %s -- subtitles
  %l -- subtitles (lines)
  %p -- page number
  %i -- stream ID
*/

// macro -- output only when increased verbosity was turned on
#define VERBOSE_ONLY if (config.verbose == YES)

// application states -- flags for notices that should be printed only once
static struct {
  uint8_t programme_info_processed;
  uint8_t pts_initialized;
} states = { NO, NO };

// SRT frames produced
//static uint32_t frames_produced = 0;

// subtitle type pages bitmap, 2048 bits = 2048 possible pages in teletext (excl. subpages)
static uint8_t cc_map[256] = { 0 };

// working teletext page buffer
static teletext_page_t *page_buffer = NULL;  // [NRTTXOUTPUTS]
static teletext_page_t *page_buffer_in = NULL;  // [8]
static teletext_text_t *page_buffer_all = NULL;   // [NRALLTTXPAGES]
static teletext_pagemap_t *page_map = NULL;  // [NRTTXOUTPUTS]
static int16_t total_pages = 0;

// teletext transmission mode
//static transmission_mode_t transmission_mode = TRANSMISSION_MODE_SERIAL;

// flag indicating if incoming data should be processed or ignored
//static uint8_t receiving_data = NO;

// flag indicating whether G0 (standard) or G1 (graphic) table shall be used
uint8_t graphic_mode = NO;
uint8_t hidden_mode = NO;

// current charset (charset can be -- and always is -- changed during transmission)
static struct {
  uint8_t current;
  uint8_t g0_m29;
  uint8_t g0_x28;
} primary_charset = { 0x00,  UNDEF, UNDEF };

// entities, used in colour mode, to replace unsafe HTML tag chars
static struct {
  uint16_t character;
  char *entity;
} const ENTITIES[] = { {'<', "&lt;"}, {'>', "&gt;"}, {'&', "&amp;"} };
  
// helper, array length function
#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))

// helper, linear searcher for a value
static inline bool_t in_array(uint16_t *array, uint16_t length, uint16_t element)
{
  bool_t r = NO;
  word i;
  for (i = 0; i < length; i++) {
    if (array[i] == element) {
      r = YES;
      break;
    }
  }
  return r;
}

// extracts magazine number from teletext page
#define MAGAZINE(p) ((p >> 8) & 0xf)

// extracts page number from teletext page
#define PAGE(p) (p & 0xff)

// ETS 300 706, chapter 8.2
static uint8_t unham_8_4(uint8_t a)
{
  uint8_t r = UNHAM_8_4[a];
  if (r == 0xff) {
    r = 0;
//    VERBOSE_ONLY printf("  ! Unrecoverable data error; UNHAM8/4(%02x)\n", a);
  }
  return (r & 0x0f);
}

// ETS 300 706, chapter 8.3
static uint32_t unham_24_18(uint32_t a)
{
  uint8_t test = 0, i;

  // Tests A-F correspond to bits 0-6 respectively in 'test'.
  for (i = 0; i < 23; i++) test ^= ((a >> i) & 0x01) * (i + 33);
  // Only parity bit is tested for bit 24
  test ^= ((a >> 23) & 0x01) * 32;

  if ((test & 0x1f) != 0x1f) {
    // Not all tests A-E correct
    if ((test & 0x20) == 0x20) {
      // F correct: Double error
      return 0xffffffff;
    }
    // Test F incorrect: Single error
    a ^= 1 << (30 - test);
  }

  return (a & 0x000004) >> 2 | (a & 0x000070) >> 3 | (a & 0x007f00) >> 4 | (a & 0x7f0000) >> 5;
}

static void remap_g0_charset(uint8_t c)
{
  if (c != primary_charset.current && c < sizeof(G0_LATIN_NATIONAL_SUBSETS_MAP)) {
    uint8_t m = G0_LATIN_NATIONAL_SUBSETS_MAP[c];
    if (m == 0xff) {
      printf("  G0 Latin National Subset ID 0x%1x.%1x is not implemented\n", (c >> 3), (c & 0x7));
    }
    else {
      uint8_t j;
      for (j = 0; j < 13; j++) G0[LATIN][G0_LATIN_NATIONAL_SUBSETS_POSITIONS[j]] = G0_LATIN_NATIONAL_SUBSETS[m].characters[j];
      for (j = 2; j < 6; j++)  G1[LATIN][G0_LATIN_NATIONAL_SUBSETS_POSITIONS[j]] = G0_LATIN_NATIONAL_SUBSETS[m].characters[j];
//      VERBOSE_ONLY printf("  Using G0 Latin National Subset ID 0x%1x.%1x (%s)\n", (c >> 3), (c & 0x7), G0_LATIN_NATIONAL_SUBSETS[m].language);
      primary_charset.current = c;
    }
  }
}

// convert teletext page nr from hex to decimal
static inline uint16_t hex2dec(uint16_t page_number)
{
  int i, pot = 1, digits = sizeof(page_number) * 2;
  uint32_t result = 0;

  for (i = 0; i < digits; i++)
    if (((page_number >> (4*i)) & 0xf) < 10)
    {
      result = result + (pot * ((page_number >> (4*i)) & 0xf));
      pot = pot * 10;
    }
    else
      return 0;
  return result;

/*  byte a = MAGAZINE(page_number);
  byte b = (page_number >> 4) & 0xf;
  byte c = page_number & 0xf;
  if (a < 10 && b < 10 && c < 10)
    return 100*a + 10*b + c;
  else
    return 0; */
}

static void timestamp_to_srttime(uint32_t timestamp, char *buffer)
{
  uint16_t h = (word) (timestamp / 3600000);
  uint8_t  m = (byte) ((timestamp / 60000) % 60);
  uint8_t  s = (byte) ((timestamp / 1000) % 60);
  uint16_t u = (word) (timestamp % 1000);
  sprintf(buffer, "%02hu:%02hhu:%02hhu,%03hu", h, m, s, u);
}

char* TimeStr(time_t UnixTimeStamp)
{
  static char TS[26];
  struct tm timeinfo = {0};
  TS[0] = '\0';

  if (UnixTimeStamp)
  {
    #if defined(LINUX)
      gmtime_r(&UnixTimeStamp, &timeinfo);
    #elif defined(_WIN32)
      localtime_s(&timeinfo, &UnixTimeStamp);
    #else
      localtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    strftime(TS, sizeof(TS), "%a %d %b %Y %H:%M:%S", &timeinfo);
  }
  return TS;
}

#ifdef LINUX
  #define TimeStr_UTC TimeStr
#else
static char* TimeStr_UTC(time_t UnixTimeStamp)
{
  static char TS[26];
  struct tm timeinfo = {0};
  TS[0] = '\0';

  if (UnixTimeStamp)
  {
    #ifdef _WIN32
      gmtime_s(&timeinfo, &UnixTimeStamp);
    #else
      gmtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    strftime(TS, sizeof(TS), "%a %d %b %Y %H:%M:%S", &timeinfo);
  }
  return TS;
}
#endif

char* TimeStrTF(tPVRTime TFTimeStamp, byte TFTimeSec)
{
  static char TS[26];
  struct tm timeinfo = {0};
  TS[0] = '\0';

  if (TFTimeStamp)
  {
    time_t UnixTimeStamp = (MJD(TFTimeStamp) - 0x9e8b) * 86400 + HOUR(TFTimeStamp) * 3600 + MINUTE(TFTimeStamp) * 60 + TFTimeSec;

    #ifdef _WIN32
      gmtime_s(&timeinfo, &UnixTimeStamp);
    #else
      gmtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    strftime(TS, sizeof(TS), "%a %d %b %Y %H:%M:%S", &timeinfo);
  }
  return TS;
}

char* TimeStr_DB(tPVRTime TFTimeStamp, byte TFTimeSec)
{
  static char TS[20];
  struct tm timeinfo = {0};
  TS[0] = '\0';

  if (TFTimeStamp)
  {
    time_t UnixTimeStamp = (MJD(TFTimeStamp) - 0x9e8b) * 86400 + HOUR(TFTimeStamp) * 3600 + MINUTE(TFTimeStamp) * 60 + TFTimeSec;

    #ifdef _WIN32
      gmtime_s(&timeinfo, &UnixTimeStamp);
    #else
      gmtime_r(&UnixTimeStamp, &timeinfo);
    #endif
    strftime(TS, sizeof(TS), "%Y-%m-%d %H:%M:%S", &timeinfo);
  }
  return TS;
}

// UCS-2 (16 bits) to UTF-8 (Unicode Normalization Form C (NFC)) conversion
int ucs2_to_utf8(char *r, uint32_t ch)
{
  if (ch < 0x80) {
    r[0] = ch & 0x7f;
    r[1] = 0;
    r[2] = 0;
    r[3] = 0;
    return 1;
  }
  else if (ch < 0x800) {
    r[0] = (ch >> 6) | 0xc0;
    r[1] = (ch & 0x3f) | 0x80;
    r[2] = 0;
    r[3] = 0;
    return 2;
  }
  else if (ch < 0x10000) {
    r[0] = (ch >> 12) | 0xe0;
    r[1] = ((ch >> 6) & 0x3f) | 0x80;
    r[2] = (ch & 0x3f) | 0x80;
    r[3] = 0;
    return 3;
  }
  else {
    r[0] = (ch >> 18) | 0xf0;
    r[1] = ((ch >> 12) & 0x3f) | 0x80;
    r[2] = ((ch >>  6) & 0x3f) | 0x80;
    r[3] = (ch & 0x3f) | 0x80;
	return 4;
  }
}

// check parity and translate any reasonable teletext character into ucs2
uint16_t telx_to_ucs2(uint8_t c)
{
  uint16_t r = c & 0x7f;

  if (PARITY_8[c] == 0) {
//    VERBOSE_ONLY printf("  ! Unrecoverable data error; PARITY(%02x)\n", c);
    return 0x2370;
  }

  if (r <= 0x07)
  {
    graphic_mode = NO;
    hidden_mode = NO;
  }
  else if (r >= 0x10 && r <= 0x17)
  {
    graphic_mode = YES;
    hidden_mode = NO;
  }
  else if (/*r == 0x18 ||*/ r == 0x1d)
    hidden_mode = YES;

  if (r >= 0x20)
  {
    if(hidden_mode /*|| (graphic_mode && !with_graphic)*/)  r = ' ';
    else if(graphic_mode)  r = G1[LATIN][r - 0x20];
    else                   r = G0[LATIN][r - 0x20];
  }
  return r;
}

static bool GetTeletextOut(uint16_t page_number, bool AddNewPage)  // Page mit Magazin!
{
  uint8_t page = PAGE(page_number);
  int i;
  for (i = 0; i < NRTTXOUTPUTS; i++)
    if(PAGE(pages[i]) == page) return i;  // Magazin wird aber nicht ausgewertet

  if (AddNewPage)
  {
    if (pages[i-1] == 0)
    {
      printf("  TTX: Additional subtitle page: %03x\n\n", page_number);
      pages[i-1] = page_number;
      return i-1;
    }
  }
  return -1;
}

// FIXME: implement output modules (to support different formats, printf formatting etc)
static void process_page(teletext_page_t *page, uint16_t page_number, int out_nr, bool check_duplicate)
{
  FILE* fOut = NULL;
  uint8_t row, col;
  uint8_t first_line_written = NO;
  uint8_t page_is_empty, page_is_duplicate;

//static int dbg = 0;

#ifdef DEBUG
  for (row = 1; row < 25; row++) {
    fprintf(fTtxOut, "# DEBUG[%02u]: ", row);
    for (col = 0; col < 40; col++) fprintf(fTtxOut, "%3x ", page->text[row][col]);
    fprintf(fTtxOut, "\n");
  }
  fprintf(fTtxOut, "\n");
#endif

  // optimization: slicing column by column -- higher probability we could find boxed area start mark sooner
  page_is_empty = YES;
  for (col = 0; col < 40; col++) {
    for (row = 1; row < 25; row++) {
      if (page->text[row][col] == 0x0b) {
        page_is_empty = NO;
        goto empty_finish;
      }
    }
  }
empty_finish:

  if (page_is_empty)
    for (row = 1; row < 25; row++)
      page->text[row][0] = 0x00;
  else
  {
    // Check all lines of NEW text for validity
    for (row = 1; row < 25; row++) {
      // anchors for string trimming purpose
      uint8_t nr_missing = 0;
      uint8_t col_start = 40;
      uint8_t col_stop = 40, col_stop2 = 40;

      // detect start of text
      for (col = 39; col > 0; col--) {
        if (page->text[row][col] == 0xb) {
          col_start = col;
          break;
        }
      }
      // line is empty
      if (col_start > 39)
        { page->text[row][0] = 0x00; continue; }

      // detect end of text
      for (col = col_start + 1; col <= 39; col++) {
        if ((page->text[row][col] >= 0x20) && (page->text[row][col] < 0x2370)) {
          if (col_stop > 39) col_start = col;
          if ((page->text[row][col] > 0x20))
            col_stop = col;
          col_stop2 = col;
        }
        else if ((col_stop <= 39) && (page->text[row][col] == 0x2370))
          nr_missing++;
        if (page->text[row][col] == 0xa) break;
      }
      // line is empty
      if (col_stop > 39)
        { page->text[row][0] = 0x00; continue; }

      // CW: line has no boxed area stop code
      if ((page->text[row][col] != 0xa) && (col_stop2 < 38 || col_stop - col_start <= 2*nr_missing))
        { page->text[row][0] = 0x00; continue; }

      // line is corrupted
      for (col = col_start; col <= col_stop; col++)
        if(page->text[row][col] == 0x2370)
          nr_missing++;
      if (nr_missing >= 3)
        { page->text[row][0] = 0x00; continue; }
    }
  }

  // Compare NEW text with last_text whether all lines are duplicate
  page_is_duplicate = NO;
  if (check_duplicate)
  {
    page_is_duplicate = YES;
    for (row = 1; row < 25; row++)
    {
      if (((page->text[row][0] != 0x00) || (page->last_text[row][0] != 0x00)) && (memcmp(page->text[row], page->last_text[row], 40*sizeof(uint16_t)) != 0))
        { page_is_duplicate = NO; break; }
    }
    if(page_is_duplicate) return;
  }

  // Try to combine last_text and text, if last_page is displayed only 2 frames
  if ((page->show_timestamp <= page->last_show_timestamp + 100) && (page->show_timestamp >= page->last_show_timestamp))
  {
    uint8_t append_page = YES;
    for (row = 1; row < 25; row++)
      if ((page->last_text[row][0] != 0x00) && (page->text[row][0] != 0x00))
        { append_page = NO; break; }

    if (append_page)
    {
      for (row = 1; row < 25; row++)
        if (page->text[row][0] != 0x00)
          memcpy(page->last_text[row], page->text[row], 40*sizeof(uint16_t));
      return;
    }
  }


  // HIER DIE CACHED SEITE VERARBEITEN UND AUSGEBEN
//  if (!page_is_duplicate && (page->show_timestamp <= page->last_show_timestamp + 100))
  {
    // Erst hier das Output-File �ffnen
    if (!fTtxOut[out_nr])
    {
      printf("  TTX: Trying to extract subtitles from page %03x\n\n", page_number);
      sprintf(&TeletextOut[TeletextOutLen], "_%03hx.srt", page_number);
      fTtxOut[out_nr] = fopen(TeletextOut, ((DoMerge==1) ? "ab" : "wb"));
    }
    fOut = fTtxOut[out_nr];

    for (row = 1; row < 25; row++)
    {
      uint8_t foreground_color = 0x7;
      uint8_t font_tag_opened = NO;
      uint8_t col_start = 40;
      uint8_t col_stop = 40;

      if (page->last_text[row][0] != 0x00)
      {
        // detect start of text
        for (col = 39; col > 0; col--)
          if (page->last_text[row][col] == 0xb)
            { col_start = col; break; }

        // detect end of text
        for (col = col_start + 1; col <= 39; col++) {
          if ((page->last_text[row][col] > 0x20) && (page->last_text[row][col] < 0x2370)) {
            if (col_stop > 39) col_start = col;
            col_stop = col;
          }
          if (page->last_text[row][col] == 0xa) break;
        }

        // print timestamps, (only) if a non-empty line exists
        if (!first_line_written)
        {
          if (page->last_show_timestamp > page->hide_timestamp)  page->hide_timestamp = page->last_show_timestamp;

          if (config.se_mode == YES) {
            ++page->frames_produced;
            fprintf(fTtxOut[out_nr], "%.3f|", (double)page->last_show_timestamp / 1000.0);
          }
          else {
            char timecode_hide[24] = { 0 };
            char timecode_show[24] = { 0 };

            timestamp_to_srttime(page->last_show_timestamp, timecode_show);
            timecode_show[12] = 0;
            timestamp_to_srttime(page->hide_timestamp, timecode_hide);
            timecode_hide[12] = 0;

            fprintf(fTtxOut[out_nr], "%u\r\n%s --> %s\r\n", ++page->frames_produced, timecode_show, timecode_hide);
          }
          first_line_written = YES;
//dbg++;
        }

/*if(dbg >= 31 && page_number==0x150)
{
  char dbgstr[44]; int k;
  if (page->last_text[row][0] != 0xff)
  {
    for (k = 0; k < 40; k++)
      if(page->last_text[row][k] >= 20) ucs2_to_utf8(&dbgstr[k], page->last_text[row][k]);
      else dbgstr[k] = ' ';
//      dbgstr[k] = (char)page->last_text[row][k];
    dbgstr[40] = '\0';
    printf("      new line: %s\n", dbgstr);
  }
} */

        // ETS 300 706, chapter 12.2: Alpha White ("Set-After") - Start-of-row default condition.
        // used for colour changes _before_ start box mark
        // white is default as stated in ETS 300 706, chapter 12.2
        // black(0), red(1), green(2), yellow(3), blue(4), magenta(5), cyan(6), white(7)

        for (col = 0; col <= col_stop; col++) {
          // v is just a shortcut
          uint16_t v = page->last_text[row][col];

          if (col < col_start) {
            if (v <= 0x7) foreground_color = (byte)v;
          }

          if (col == col_start) {
            if ((foreground_color != 0x7) && (config.colours == YES)) {
              fprintf(fOut, "<font color=\"%s\">", TTXT_COLOURS[foreground_color]);
              font_tag_opened = YES;
            }
          }

          if (col >= col_start) {
            if (v <= 0x7) {
              // ETS 300 706, chapter 12.2: Unless operating in "Hold Mosaics" mode,
              // each character space occupied by a spacing attribute is displayed as a SPACE.
              if (config.colours == YES) {
                if (font_tag_opened == YES) {
                  fprintf(fOut, "</font> ");
                  font_tag_opened = NO;
                }

                // black is considered as white for telxcc purpose
                // telxcc writes <font/> tags only when needed
                if ((v > 0x0) && (v < 0x7)) {
                  fprintf(fOut, "<font color=\"%s\">", TTXT_COLOURS[v]);
                  font_tag_opened = YES;
                }
              }
              else v = 0x20;
            }

            if (v >= 0x20) {
              // translate some chars into entities, if in colour mode
              if (config.colours == YES) {
                uint8_t i;
                for (i = 0; i < ARRAY_LENGTH(ENTITIES); i++) {
                  if (v == ENTITIES[i].character) {
                    fprintf(fOut, "%s", ENTITIES[i].entity);
                    // v < 0x20 won't be printed in next block
                    v = 0;
                    break;
                  }
                }
              }
            }

            if (v >= 0x20) {
              char u[4] = { 0, 0, 0, 0 };
              if(v >= 0x2370) v = 0x20;
              ucs2_to_utf8(u, v);
              fprintf(fOut, "%s", u);
            }
          }
        }

        // no tag will left opened!
        if ((config.colours == YES) && (font_tag_opened == YES)) {
          fprintf(fOut, "</font>");
          font_tag_opened = NO;
        }

        // line delimiter
        fprintf(fOut, "%s", (config.se_mode == YES) ? " " : "\r\n");
      }
    }
    if (first_line_written) {
      fprintf(fOut, "\r\n");
      fflush(fOut);
    }

    memcpy(page->last_text, page->text, sizeof(teletext_text_t));
    page->last_show_timestamp = page->show_timestamp;
//    memset(page->text, 0, sizeof(teletext_text_t));
//    page->show_timestamp = 0;
  }
}

static void process_page2(uint16_t page_number)
{
  int p = 0, s, i, j;
  bool page_empty = TRUE;
  int page_nr = hex2dec(page_number);
  teletext_page_t *page = (teletext_page_t*) &page_buffer_in[MAGAZINE(page_number)-1];
  if (MAGAZINE(page_number) <= 0) return;

  // Check for empty
  for (i = 1; i < 25 && page_empty; i++)
    for (j = 0; j < 40; j++)
      if ((page->text[i][j] > 0x20) && (page->text[i][j] != 0x2370))
        { page_empty = FALSE; break; }

  if(!page_nr || page_nr < 100 || page_nr > 899 || page_empty) return;

  // Find subpage from subcode
  // (If subcode specified, use subcode as place - if not, use '1' or first free place)
  if (page->text[0][3] == '|')
  {
    if ((page->text[0][4] == '0'))
      p = 10 * (page->text[0][5] - '0') + page->text[0][6] - '0';
  }

  if (page_map[page_nr-100].subpages[p] >= 0)
  {
    // Find first (almost) matching page or page with subset, otherwise first empty page
    for (s = p; s < NRSUBPAGES; s++)
    {
      teletext_text_t *ref = &page_buffer_all[page_map[page_nr-100].subpages[s]];
      bool ref_empty = TRUE;
      int nr_diff = 0, nr_content_same = 0, nr_unique_ref = 0, nr_unique_new = 0, nr_missing_ref = 0, nr_missing_new = 0;

      if (page_map[page_nr-100].subpages[s] < 0)
        { p = s; break; }

      // Check if reference page is empty
      for (i = 1; i < 25 && ref_empty; i++)
        for (j = 0; j < 40; j++)
          if (ref->text[i][j] > 0x20)
            { ref_empty = FALSE; break; }
      if(ref_empty)
        { p = s; break; }

      // Check for grade of matching
      for (i = 0; i < 25; i++)
      {
        if (page->text[i][0] == 0 || ref->text[i][0] == 0)
        {
          if (page->text[i][0] == 0 &&  ref->text[i][j] > 0)  nr_unique_ref += 10;
          if ( ref->text[i][0] == 0 && page->text[i][j] > 0)  nr_unique_new += 10;
          continue;
        }
        for (j = 0; j < 40; j++)
        {
          if (page->text[i][j] == ref->text[i][j])  nr_content_same++;  // v�llige �bereinstimmung
          else
          {
            if (i>=1 && ref->text[i][j] > ' ' && page->text[i][j] > ' ' /*&& ref->text[i][j] != 0 && page->text[i][j] != 0*/)  nr_diff++;  // echte Differenzen, ohne Leerzeichen
            if      ( ref->text[i][j] == 0x2370)                           nr_missing_ref++;
            else if ( ref->text[i][j]  > ' '  && page->text[i][j] <= ' ')  nr_unique_ref++;  // zus�tzliches Zeichen in ref (wenn alles auf Leerzeichen ist Z.658, kann erste Bedingung weg)
            if      (page->text[i][j] == 0x2370)                           nr_missing_new++;
            else if (page->text[i][j]  > ' '  &&  ref->text[i][j] <= ' ')  nr_unique_new++;  // zus�tzliches Zeichen in new (wenn alles auf Leerzeichen ist Z.658, kann erste Bedingung weg)
          }
        }
      }

      // Wenn ref = new oder ref Teilmenge von new oder umgekehrt
      if((nr_content_same > 40) && (nr_diff < 40) && !(nr_unique_ref > 20 && nr_unique_new > 20))
      {
        // Fix missing chars if page is duplicate
        if ((nr_missing_ref || nr_missing_new) && nr_diff)
        {
          for (i = 0; i < 25; i++)
            for (j = 0; j < 40; j++)
            {
              if (     (ref->text[i][j] == 0x2370)  && ((j==0) || (ref->text[i][j-1] == page->text[i][j-1])) && ((j==39) || (ref->text[i][j+1] == page->text[i][j+1])) )
                ref->text[i][j] = page->text[i][j];
              else if( (page->text[i][j] == 0x2370) && ((j==0) || (ref->text[i][j-1] == page->text[i][j-1])) && ((j==39) || (ref->text[i][j+1] == page->text[i][j+1])) )
                page->text[i][j] = ref->text[i][j];
            }
        }

        // ref und new stimmen �berein
        if ((nr_unique_new > nr_unique_ref + 40) || ((nr_missing_new < nr_missing_ref) && (nr_unique_new + 10 >= nr_unique_ref)) || ((nr_unique_new > nr_unique_ref) && (nr_missing_new == nr_missing_ref)) || ExtractAllOverwrite)
          p = s;
        else  p = -1;
        break;
      }
      if(p >= 0) break;
    }
  }

  // Copy the page to the desired place
  if (p >= 0 && p < NRSUBPAGES)
  {
    int16_t place = page_map[page_nr-100].subpages[p];
    if (place < 0)
    {
      if(total_pages < NRALLTTXPAGES)
      {
        place = total_pages++;
        page_map[page_nr-100].subpages[p] = place;
      }
      else printf("  TTX: Warning! Teletext page buffer (4096 pages) is full!\n");
    }
    if (place >= 0)
      memcpy(&page_buffer_all[place], page->text, sizeof(teletext_text_t));
  }
//  memset(&page_buffer_in[MAGAZINE(page_number)-1], 0, sizeof(teletext_page_t));
}

static void process_telx_packet(data_unit_t data_unit_id, teletext_packet_payload_t *packet, uint32_t timestamp)
{
  // variable names conform to ETS 300 706, chapter 7.1.2
  uint8_t address = (unham_8_4(packet->address[1]) << 4) | unham_8_4(packet->address[0]);
  uint8_t y = (address >> 3) & 0x1f;  // Zeile
  uint8_t m = (address & 0x7) ? address & 0x7 : 8;  // Magazin
  uint8_t designation_code = (y > 25) ? unham_8_4(packet->data[0]) : 0x00;
  uint8_t c;  // Charset

  static uint16_t page_number = 0;   // (m << 8) | p;  // page_number = m|pp (magazin|page)
  static transmission_mode_t transmission_mode = TRANSMISSION_MODE_SERIAL;
  static int out_nr = -1;            // GetTeletextOut(page_number, FALSE);
  teletext_page_t *cur_page_buffer = NULL;

  graphic_mode = NO;
  hidden_mode = NO;

//printf("y=%d\n", y);

  if (y == 0)
  {
    // CC map
    uint8_t  p   = (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);  // Page (2-stellig)
    uint16_t sub = ((unham_8_4(packet->data[5]) & 0x3) << 12) | ((unham_8_4(packet->data[4]) & 0xf) << 8) | ((unham_8_4(packet->data[3]) & 0x7) << 4) | (unham_8_4(packet->data[2]) & 0xf);  // Page Subcode
    uint8_t flag_subtitle = (unham_8_4(packet->data[5]) & 0x08) >> 3;
    uint8_t flag_suppress_header;
    uint8_t charset;

    cc_map[p] |= flag_subtitle << (m - 1);

    if(m != MAGAZINE(page_number)) primary_charset.g0_m29 = UNDEF;  // CW

    charset = unham_8_4(packet->data[7]);
    charset = ((charset & 0x08) | (charset & 0x04) | (charset & 0x02)) >> 1;
//printf("new page: m=%hx, p=%02hx, trans=%d, charset=%u\n", m, p, (unham_8_4(packet->data[7]) & 0x01), charset);

    flag_suppress_header = (unham_8_4(packet->data[6]) & 0x01) || flag_subtitle;
    //uint8_t flag_inhibit_display = (unham_8_4(packet->data[6]) & 0x08) >> 3;

    // ETS 300 706, chapter 9.3.1.3:
    // When set to '1' the service is designated to be in Serial mode and the transmission of a page is terminated
    // by the next page header with a different page number.
    // When set to '0' the service is designated to be in Parallel mode and the transmission of a page is terminated
    // by the next page header with a different page number but the same magazine number.
    // The same setting shall be used for all page headers in the service.
    // ETS 300 706, chapter 7.2.1: Page is terminated by and excludes the next page header packet
    // having the same magazine address in parallel transmission mode, or any magazine address in serial transmission mode.
//    transmission_mode = unham_8_4(packet->data[7]) & 0x01;

    if ( (p != PAGE(page_number)) || (transmission_mode == TRANSMISSION_MODE_SERIAL && m != MAGAZINE(page_number)) || (transmission_mode == TRANSMISSION_MODE_PARALLEL && m == MAGAZINE(page_number)) )
    {
      if (ExtractAllTeletext)
      {
        // ExtractAllTeletext is processed as soon as any new page arrives
        uint16_t i;
        if (page_number)
        {
          if (transmission_mode == TRANSMISSION_MODE_PARALLEL)
            for (i = 0; i < 8; i++)
            {
//              if (page_buffer_in[i].receiving_data = YES)
                process_page2((i+1) << 8 | PAGE(page_number));
              page_buffer_in[i].receiving_data = NO;
            }
          else
            process_page2(page_number);
        }
        if ((unham_8_4(packet->data[7]) & 0x01) == TRANSMISSION_MODE_PARALLEL)
          for (i = 0; i < 8; i++)
          {
            page_buffer_in[i].receiving_data = NO;
            memset(page_buffer_in[i].text, 0, sizeof(teletext_text_t));
          }
        else
          memset(page_buffer_in[m-1].text, 0, sizeof(teletext_text_t));
      }

      // FIXME: Well, this is not ETS 300 706 kosher, however we are interested in DATA_UNIT_EBU_TELETEXT_SUBTITLE only
      if (/*!ExtractAllTeletext &&*/ ((unham_8_4(packet->data[7]) & 0x01) == TRANSMISSION_MODE_PARALLEL) && (data_unit_id != DATA_UNIT_EBU_TELETEXT_SUBTITLE)) return;  // !! sch�tzt vor [m=8, p=a0, t=par] Paketen bei arte HD, die ggf. Pages zu fr�h beenden (leider nicht bei Kombination -tt und -tx)

      // Subtitle Page transmission is terminated, however now we are waiting for our new page...
      if (out_nr >= 0)
        page_buffer[out_nr].receiving_data = NO;
    }

    page_number = (m << 8) | p;
    transmission_mode = (transmission_mode_t) (unham_8_4(packet->data[7]) & 0x01);

    if (((p & 0xf0) > 0x90) || ((p & 0x0f) > 0x09)) return;

    // Open a new srt output file
    if (ExtractTeletext && flag_subtitle)
    {
      out_nr = GetTeletextOut(page_number, TRUE);
      if((out_nr >= 0) && ((transmission_mode == TRANSMISSION_MODE_PARALLEL) || (MAGAZINE(pages[out_nr]) == m)))
      {
        cur_page_buffer = &page_buffer[out_nr];
        page_number = pages[out_nr];
      }
      else out_nr = -1;
    }
    else out_nr = -1;

    if (((out_nr >= 0) && (m == MAGAZINE(page_number))) || ExtractAllTeletext)
    {
      // Charset mapping
      primary_charset.g0_x28 = UNDEF;
      c = (primary_charset.g0_m29 != UNDEF) ? primary_charset.g0_m29 : charset;   // ToDo: Eigentlich m�sste das Charset im PageBuffer gespeichert werden (f�r parallel mode)
//printf("A Remap charset nr. %hhu\n", c);
      remap_g0_charset(c);

      if (out_nr >= 0)
        cur_page_buffer = &page_buffer[out_nr];
      else if (ExtractAllTeletext && page_number)
        cur_page_buffer = &page_buffer_in[m-1];
      cur_page_buffer->receiving_data = YES;

      // ... Here starts the new page!
      if (out_nr >= 0)
      {
        // Now we have the begining of page transmission; if there is page_buffer pending, process it
        if (cur_page_buffer->tainted == YES)
        {
          process_page(cur_page_buffer, pages[out_nr], out_nr, TRUE);  // hier den last_text und last_show_timestamp verwenden!

          // it would be nice, if subtitle hides on previous video frame, so we subtract 40 ms (1 frame @25 fps)
          cur_page_buffer->hide_timestamp = timestamp - 40;
        }

        memset(cur_page_buffer->text, 0, sizeof(teletext_text_t));
        cur_page_buffer->show_timestamp = timestamp;
        cur_page_buffer->tainted = NO;
//        cur_page_buffer->receiving_data = YES;
      }

      // I know -- not needed; in subtitles we will never need disturbing teletext page status bar
      // displaying tv station name, current time etc.
      if (ExtractAllTeletext || flag_suppress_header == NO) {
        char page_str[15];
        uint8_t i;
        snprintf(page_str, 15, (sub > 0 && sub <= 0x79 && (sub & 0x0f) <= 9) ? "%03hx|%03hx:" : "%03hx:    ", page_number, sub);
//printf("%s\n", page_str);
        for (i =  0; i < 8; i++) cur_page_buffer->text[0][i] = page_str[i];
        for (i =  8; i < 40; i++) cur_page_buffer->text[0][i] = telx_to_ucs2(packet->data[i]);
        //cur_page_buffer->tainted = YES;
      }
    }
    return;
  }

  if (((page_number & 0xf0) > 0x90) || ((page_number & 0x0f) > 0x09)) return;
  if (out_nr >= 0)
    cur_page_buffer = &page_buffer[out_nr];
  else if (ExtractAllTeletext)
    cur_page_buffer = &page_buffer_in[m-1];
  else
    return;

  if (m == MAGAZINE(page_number) || (ExtractAllTeletext && transmission_mode == TRANSMISSION_MODE_PARALLEL))
  {
    if (cur_page_buffer->receiving_data == YES)
    {
      if ((y >= 1) && (y <= 23)) {
        // ETS 300 706, chapter 9.4.1: Packets X/26 at presentation Levels 1.5, 2.5, 3.5 are used for addressing
        // a character location and overwriting the existing character defined on the Level 1 page
        // ETS 300 706, annex B.2.2: Packets with Y = 26 shall be transmitted before any packets with Y = 1 to Y = 25;
        // so page_buffer.text[y][i] may already contain any character received
        // in frame number 26, skip original G0 character
        uint8_t i;
        bool line_empty = TRUE;
/*char test[41];
memset(test, 0, sizeof(test));
for(i = 0; i < 40; i++) {
  test[i] = telx_to_ucs2(packet->data[i]) & 0xff;
  if(test[i] < 0x20 && test[i] != '\n' && test[i] != '\t') test[i] = 0x20;
}
graphic_mode = NO;
hidden_mode = NO;
//if(page_number == 0x888)
  printf("[%1hx%02hx] %03hhu: %s\n", m, PAGE(page_number), y, test); */

        for (i = 0; i < 40; i++)
        {
          if (cur_page_buffer->text[y][i] == 0x00)
            cur_page_buffer->text[y][i] = telx_to_ucs2(packet->data[i]);
          if ((cur_page_buffer->text[y][i] > ' ' || cur_page_buffer->text[y][i] == 0x1d) && (cur_page_buffer->text[y][i] != 0x2370))
            line_empty = FALSE;

//packet->data[i] = packet->data[i] & 0x7f;
        }
        if(line_empty)  // CW: line_empty eher unn�tig - eingef�hrt, um Wiederholung von Untertiteln vermeiden zu k�nnen
//          for (i = 0; i < 40; i++)
            cur_page_buffer->text[y][0] = 0x0;
        else
        {
          if (cur_page_buffer->text[y][0] == 0x0)
            cur_page_buffer->text[y][0] = ' ';
          cur_page_buffer->tainted = YES;
        }
      }
      else if (y == 26) {
        // ETS 300 706, chapter 12.3.2: X/26 definition
        uint8_t x26_row = 0;
        uint8_t x26_col = 0;

        uint32_t triplets[13] = { 0 };
        uint8_t i, j;
        for (i = 1, j = 0; i < 40; i += 3, j++) triplets[j] = unham_24_18((packet->data[i + 2] << 16) | (packet->data[i + 1] << 8) | packet->data[i]);

        for (j = 0; j < 13; j++) {
          uint8_t data, mode, address, row_address_group;

          if (triplets[j] == 0xffffffff) {
            // invalid data (HAM24/18 uncorrectable error detected), skip group
//            VERBOSE_ONLY printf("  ! Unrecoverable data error; UNHAM24/18()=%04x\n", triplets[j]);
            continue;
          }

          data = (triplets[j] & 0x3f800) >> 11;
          mode = (triplets[j] & 0x7c0) >> 6;
          address = triplets[j] & 0x3f;
          row_address_group = (address >= 40) && (address <= 63);

          // ETS 300 706, chapter 12.3.1, table 27: set active position
          if ((mode == 0x04) && (row_address_group == YES)) {
            x26_row = address - 40;
            if (x26_row == 0) x26_row = 24;
            x26_col = 0;
          }

          // ETS 300 706, chapter 12.3.1, table 27: termination marker
          if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == YES)) break;

          // ETS 300 706, chapter 12.3.1, table 27: character from G2 set
          if ((mode == 0x0f) && (row_address_group == NO)) {
            x26_col = address;
            if (data > 31) cur_page_buffer->text[x26_row][x26_col] = G2[0][data - 0x20];
          }

          // ETS 300 706, chapter 12.3.1, table 27: G0 character with diacritical mark
          if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == NO)) {
            x26_col = address;

            // A - Z
            if ((data >= 65) && (data <= 90)) cur_page_buffer->text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 65];
            // a - z
            else if ((data >= 97) && (data <= 122)) cur_page_buffer->text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 71];
            // other
            else cur_page_buffer->text[x26_row][x26_col] = telx_to_ucs2(data);
          }
        }
      }
      else if (y == 28) {
        // TODO:
        //   ETS 300 706, chapter 9.4.7: Packet X/28/4
        //   Where packets 28/0 and 28/4 are both transmitted as part of a page, packet 28/0 takes precedence over 28/4 for all but the colour map entry coding.
        if ((designation_code == 0) || (designation_code == 4)) {
          // ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1
          // ETS 300 706, chapter 9.4.7: Packet X/28/4
          uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);

          if (triplet0 == 0xffffffff) {
            // invalid data (HAM24/18 uncorrectable error detected), skip group
//            VERBOSE_ONLY printf("  ! Unrecoverable data error; UNHAM24/18()=%04x\n", triplet0);
          }
          else {
            // ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1 only
            if ((triplet0 & 0x0f) == 0x00)
            {
              primary_charset.g0_x28 = (triplet0 & 0x3f80) >> 7;
//printf("NO Remap charset nr. %hhu\n", primary_charset.g0_x28);
//CW              remap_g0_charset(primary_charset.g0_x28);
            }
          }
        }
      }
    }
    if (y == 29)
    {
      // TODO:
      //   ETS 300 706, chapter 9.5.1 Packet M/29/0
      //   Where M/29/0 and M/29/4 are transmitted for the same magazine, M/29/0 takes precedence over M/29/4.
      if ((designation_code == 0) || (designation_code == 4)) {
        // ETS 300 706, chapter 9.5.1: Packet M/29/0
        // ETS 300 706, chapter 9.5.3: Packet M/29/4
        uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);

        if (triplet0 == 0xffffffff) {
          // invalid data (HAM24/18 uncorrectable error detected), skip group
//          VERBOSE_ONLY printf("  ! Unrecoverable data error; UNHAM24/18()=%04x\n", triplet0);
        }
        else {
          // ETS 300 706, table 11: Coding of Packet M/29/0
          // ETS 300 706, table 13: Coding of Packet M/29/4
          if ((triplet0 & 0xff) == 0x00) {
            primary_charset.g0_m29 = (triplet0 & 0x3f80) >> 7;
            // X/28 takes precedence over M/29
            if (primary_charset.g0_x28 == UNDEF) {
//printf("B Remap charset nr. %hhu\n", primary_charset.g0_m29);
              remap_g0_charset(primary_charset.g0_m29);
            }
          }
        }
      }
    }
  }
  if ((m == 8) && (y == 30))
  {
    // ETS 300 706, chapter 9.8: Broadcast Service Data Packets
    if (states.programme_info_processed == NO) {
      // ETS 300 706, chapter 9.8.1: Packet 8/30 Format 1
      if (unham_8_4(packet->data[0]) < 2) {
        uint32_t t;
        time_t t0;

/*        printf("  TTX: Programme Identification Data = ");
        for (i = 20; i < 40; i++) {
          char u[4] = { 0, 0, 0, 0 };
          uint16_t c = telx_to_ucs2(packet->data[i]);
          // strip any control codes from PID, eg. TVP station
          if (c < 0x20) continue;

          ucs2_to_utf8(u, c);
          printf("%s", u);
        }
        printf("\n"); */

        // OMG! ETS 300 706 stores timestamp in 7 bytes in Modified Julian Day in BCD format + HH:MM:SS in BCD format
        // + timezone as 5-bit count of half-hours from GMT with 1-bit sign
        // In addition all decimals are incremented by 1 before transmission.

        // 1st step: BCD to Modified Julian Day
        t = 0;
        t += (packet->data[10] & 0x0f) * 10000;
        t += ((packet->data[11] & 0xf0) >> 4) * 1000;
        t += (packet->data[11] & 0x0f) * 100;
        t += ((packet->data[12] & 0xf0) >> 4) * 10;
        t += (packet->data[12] & 0x0f);
        t -= 11111;
        // 2nd step: conversion Modified Julian Day to unix timestamp
        t = (t - 40587) * 86400;
        // 3rd step: add time
        t += 3600 * ( ((packet->data[13] & 0xf0) >> 4) * 10 + (packet->data[13] & 0x0f) );
        t +=   60 * ( ((packet->data[14] & 0xf0) >> 4) * 10 + (packet->data[14] & 0x0f) );
        t +=    ( ((packet->data[15] & 0xf0) >> 4) * 10 + (packet->data[15] & 0x0f) );
        t -= 40271;
        // 4th step: conversion to time_t
        t0 = (time_t)t;
        // ctime output itself is \n-ended
//        printf("  TTX: Programme Timestamp (UTC) = %s\n", TimeStr_UTC(t0));

//        VERBOSE_ONLY printf("  Transmission mode = %s\n", (transmission_mode == TRANSMISSION_MODE_SERIAL ? "serial" : "parallel"));

        if (config.se_mode == YES) {
          printf("  Broadcast Service Data Packet received, resetting UTC referential value to %s\n", TimeStr_UTC(t0));
          config.utc_refvalue = t;
          states.pts_initialized = NO;
        }

        states.programme_info_processed = YES;
      }
    }
  }
}

uint16_t process_pes_packet(uint8_t *buffer, uint16_t size)
{
  static bool_t         using_pts = UNDEF;
  static uint32_t       delta = 0;
//  static uint32_t       t0 = 0;
  uint32_t              t = 0, new_timestamp;
  uint64_t              pes_prefix;
  uint8_t               pes_stream_id;
  uint16_t              pes_packet_length;
  uint8_t               optional_pes_header_included = NO;
  uint16_t              optional_pes_header_length = 0;
  uint16_t              i;

  if (size < 6) return 0;

  // Packetized Elementary Stream (PES) 32-bit start code
  pes_prefix = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
  pes_stream_id = buffer[3];

  // check for PES header
  if (pes_prefix != 0x000001) return 0;

  // stream_id is not "Private Stream 1" (0xbd)
  if (pes_stream_id != 0xbd) return 0;

  // PES packet length
  // ETSI EN 301 775 V1.2.1 (2003-05) chapter 4.3: (N x 184) - 6 + 6 B header
  pes_packet_length = 6 + ((buffer[4] << 8) | buffer[5]);
  // Can be zero. If the "PES packet length" is set to zero, the PES packet can be of any length.
  // A value of zero for the PES packet length can be used only when the PES packet payload is a video elementary stream.
  if (pes_packet_length == 6) return 6;

  // truncate incomplete PES packets
  if (pes_packet_length > size) pes_packet_length = size;

  // optional PES header marker bits (10.. ....)
  if ((buffer[6] & 0xc0) == 0x80) {
    optional_pes_header_included = YES;
    optional_pes_header_length = buffer[8];
  }

  // should we use PTS or PCR?
  if (using_pts == UNDEF) {
    if ((optional_pes_header_included == YES) && ((buffer[7] & 0x80) > 0)) {
      using_pts = YES;
//      VERBOSE_ONLY printf("  PID 0xbd PTS available\n");
    } else {
      using_pts = NO;
//      VERBOSE_ONLY printf("  PID 0xbd PTS unavailable, using TS PCR\n");
    }
  }

  // If there is no PTS available, use global PCR
  if (using_pts == NO) {
    t = global_timestamp;
  }
  else {
    // PTS is 33 bits wide, however, timestamp in ms fits into 32 bits nicely (PTS/90)
    // presentation and decoder timestamps use the 90 KHz clock, hence PTS/90 = [ms]
    uint64_t pts = 0;
    // __MUST__ assign value to uint64_t and __THEN__ rotate left by 29 bits
    // << is defined for signed int (as in "C" spec.) and overflow occures
    pts = (buffer[9] & 0x0e);
    pts <<= 29;
    pts |= (buffer[10] << 22);
    pts |= ((buffer[11] & 0xfe) << 14);
    pts |= (buffer[12] << 7);
    pts |= ((buffer[13] & 0xfe) >> 1);
    t = (uint32_t) (pts / 90);
  }

  if (states.pts_initialized == NO) {
    delta = t - 1000 * (config.utc_refvalue + config.offset);
    states.pts_initialized = YES;

    if ((using_pts == NO) && (global_timestamp == 0)) {
      // We are using global PCR, nevertheless we still have not received valid PCR timestamp yet
      states.pts_initialized = NO;
    }
  }

//CW  if (t < t0) delta = last_timestamp;
  new_timestamp = t - delta;
  if (FirstPacketAfterBreak || labs(new_timestamp - last_timestamp) > 30000)
  {
    delta += new_timestamp - last_timestamp;
    new_timestamp = t - delta;
    FirstPacketAfterBreak = FALSE;
  }
  last_timestamp = new_timestamp;

  // skip optional PES header and process each 46 bytes long teletext packet
  i = 7;
  if (optional_pes_header_included == YES) i += 2 + optional_pes_header_length;
  i++;

//printf ("new packet\n");

  while (i <= pes_packet_length - 6) {
    uint8_t data_unit_id = buffer[i++];
    uint8_t data_unit_len = buffer[i++];

    if ((data_unit_id == DATA_UNIT_EBU_TELETEXT_NONSUBTITLE) || (data_unit_id == DATA_UNIT_EBU_TELETEXT_SUBTITLE)) {
      // teletext payload has always size 44 bytes
      if (data_unit_len == 44) {
        // reverse endianess (via lookup table), ETS 300 706, chapter 7.1
        uint8_t j;
        for (j = 0; j < data_unit_len; j++) buffer[i + j] = REVERSE_8[buffer[i + j]];

//printf("  new payload\n");

        // FIXME: This explicit type conversion could be a problem some day -- do not need to be platform independant
        process_telx_packet((data_unit_t)data_unit_id, (teletext_packet_payload_t *)&buffer[i], last_timestamp);
      }
    }

    i += data_unit_len;
  }
  return pes_packet_length;
}


// ------------------------------------------------------------------------------------------------

void SetTeletextBreak(bool NewInputFile, bool NewOutputFile, word SubtitlePage)
{
  int k;
  FirstPacketAfterBreak = TRUE;
//  PSBuffer_DropCurBuffer(&TtxBuffer);
  PSBuffer_StartNewBuffer(&TtxBuffer, FALSE);

  for (k = 0; k < NRTTXOUTPUTS; k++)
  {
    if (fTtxOut[k])
    {
      // output any pending close caption
      process_page(&page_buffer[k], pages[k], k, TRUE);  // hier den last_text und last_show_timestamp verwenden!
      memset(page_buffer[k].text, 0, sizeof(teletext_text_t));
      page_buffer[k].show_timestamp = 0;

      // this time we do not subtract any frames, there will be no more frames
      page_buffer[k].hide_timestamp = last_timestamp;
      if (page_buffer[k].tainted == YES)
        process_page(&page_buffer[k], pages[k], k, FALSE);  // hier wird text und show_timestamp genutzt
      memset(page_buffer[k].last_text, 0, sizeof(teletext_text_t));
      page_buffer[k].last_show_timestamp = 0;

      page_buffer[k].receiving_data = NO;
    }
  }
  if (ExtractAllTeletext)
    memset(page_buffer_in, 0, 8 * sizeof(teletext_page_t));

  if (NewOutputFile)
  {
    states.pts_initialized = NO;
    last_timestamp = 0;

    if (ExtractTeletext)
      for (k = 0; k < NRTTXOUTPUTS; k++)
        page_buffer[k].frames_produced = 0;
  }
  if (NewInputFile)
  {
    states.programme_info_processed = NO;
    states.pts_initialized = NO;
    pages[NRTTXOUTPUTS-1] = 0;
    if (SubtitlePage != config.page)
    {
      config.page = SubtitlePage;
      pages[NRTTXOUTPUTS-1] = SubtitlePage;
      printf("  TTX: Trying to extract subtitles from user page %03x\n\n", config.page);
      for (k = 0; k < NRTTXOUTPUTS-1; k++)
        if (PAGE(pages[k]) == PAGE(SubtitlePage))
          printf("  TTX: Warning! User page %03x conflicts with default page %03x.\n\n", config.page, pages[k]);  // only relevant for transmission_mode=PARALLEL
    }
  }
}

void TtxProcessor_SetOverwrite(bool DoOverwrite)
{
  ExtractAllOverwrite = DoOverwrite;
}

void TtxProcessor_Init(word SubtitlePage)
{
  int k;
  if(page_buffer) free(page_buffer);
  if(page_buffer_in) free(page_buffer_in);
  if(page_buffer_all) free(page_buffer_all);
  if(page_map) free(page_map);
  if(ExtractTeletext)
  {
    page_buffer = (teletext_page_t*) malloc(NRTTXOUTPUTS * sizeof(teletext_page_t));
    memset(page_buffer, 0, NRTTXOUTPUTS * sizeof(teletext_page_t));
  }
  if(ExtractAllTeletext)
  {
    page_buffer_in = (teletext_page_t*) malloc(8 * sizeof(teletext_page_t));
    page_buffer_all = (teletext_text_t*) malloc(NRALLTTXPAGES * sizeof(teletext_text_t));
    page_map = (teletext_pagemap_t*) malloc(800 * sizeof(teletext_pagemap_t));
    memset(page_buffer_in, 0, 8 * sizeof(teletext_page_t));
    memset(page_buffer_all, 0, NRALLTTXPAGES * sizeof(teletext_text_t));
    memset(page_map, -1, 800 * sizeof(teletext_pagemap_t));
  }
  PSBuffer_Init(&TtxBuffer, TeletextPID, 4096, FALSE, FALSE);  // eigentlich: 1288 / 1472
  LastBuffer = 0;
  config.page = SubtitlePage;
  pages[NRTTXOUTPUTS-1] = SubtitlePage;
  if (SubtitlePage)
    printf("  TTX: Trying to extract subtitles from user page %03x\n\n", config.page);
  for (k = 0; k < NRTTXOUTPUTS-1; k++)
    if (PAGE(pages[k]) == PAGE(SubtitlePage))
      printf("  TTX: Warning! User page %03x conflicts with default page %03x.\n\n", config.page, pages[k]);  // only relevant for transmission_mode=PARALLEL
  FirstPacketAfterBreak = TRUE;
  global_timestamp = 0;
  last_timestamp = 0;
}

bool LoadTeletextOut(const char* AbsOutFile)
{
  char *p;
  TRACEENTER;

  snprintf(TeletextOut, sizeof(TeletextOut), "%s", AbsOutFile);
  if ((p = strrchr(AbsOutFile, '.')) != NULL)
    TeletextOutLen = (int)(p - AbsOutFile);
  else
    TeletextOutLen = (int)strlen(TeletextOut);

  if (ExtractTeletext)
    PSBuffer_Init(&TtxBuffer, TeletextPID, 4096, FALSE, FALSE);  // eigentlich: 1288 / 1472

  TRACEEXIT;
  return TRUE;
}

void ProcessTtxPacket(tTSPacket *Packet)
{
  TRACEENTER;

  PSBuffer_ProcessTSPacket(&TtxBuffer, (tTSPacket*)Packet);
  if(TtxBuffer.ValidDiscontinue) SetTeletextBreak(FALSE, FALSE, TeletextPage);
  if(TtxBuffer.ValidBuffer != LastBuffer && TtxBuffer.ValidBufLen > 0)
  {
    byte *pBuffer = (TtxBuffer.ValidBuffer==2) ? TtxBuffer.Buffer2 : TtxBuffer.Buffer1;
    process_pes_packet(pBuffer, TtxBuffer.ValidBufLen);
    LastBuffer = TtxBuffer.ValidBuffer;
  }

  TRACEEXIT;
}

bool WriteAllTeletext(char *AbsOutFile)
{
  uint16_t line[41];
  int p, s, i, j, col_stop;
  uint16_t *c, *last_coltag;
  color_t foreground_color, background_color;
  bool hold_mosaic = FALSE, ret = TRUE;
  FILE *f = fopen(AbsOutFile, "wb");
  if(!f) return FALSE;

  memset(line, 0, sizeof(line));
  for (p = 0; p < 800; p++)
  {
    // Anzahl Subpages ermitteln
    int nr_subpages = 0;
    for (s = 0; s < NRSUBPAGES; s++)
    {
      teletext_text_t *page = &page_buffer_all[page_map[p].subpages[s]];
      bool empty_page = TRUE;
      if (page_map[p].subpages[s] >= 0)
        for (i = 1; i < 25 && empty_page; i++)
          for (j = 0; j < 40; j++)
            if((page->text[i][j] > 0x20) && (page->text[i][j] != 0x2370))  { empty_page = FALSE; break; }
      if(!empty_page) nr_subpages++;
    }

    // alle Subpages ausgeben
    for (s = 0; s < NRSUBPAGES; s++)
    {
      teletext_text_t *page = &page_buffer_all[page_map[p].subpages[s]];
      bool empty_page = TRUE;
      if (page_map[p].subpages[s] >= 0)
        for (i = 0; i < 25 && empty_page; i++)
          for (j = 0; j < 40; j++)
            if((page->text[i][j] > 0x20) && (page->text[i][j] != 0x2370))  { empty_page = FALSE; break; }
      if(empty_page) continue;

      fprintf(f, "----------------------------------------\r\n");
      fprintf(f, ((nr_subpages <= 1) ? "[%03hu]\r\n" : "[%03hu] (%d/%d)\r\n"), p+100, s, nr_subpages);

      for (i = 0; i < 24; i++)
      {
        foreground_color = COLOR_WHITE;
        background_color = COLOR_BLACK;
        hold_mosaic = FALSE;
        last_coltag = NULL;

        // Steuerzeichen entfernen  ToDo: Humax-3/LOEWENZAHN_0511101625.ttx (S.100)
        for (j = 0; j < 40; j++)
        {
          c = &page->text[i][j];
          if (*c <= 0x20)
          {
            if (*c == 0 && j == 0)
            {
              foreground_color = COLOR_BLACK;
              *c = ' ';
              last_coltag = c;
            }
            else if ((*c <= 0x07) && (hold_mosaic || (foreground_color != (color_t) *c)))
            {
              foreground_color = (color_t) *c;
              *c = (uint16_t) TTXT_COLORSYMBOLS[0][foreground_color];
              if(last_coltag && !hold_mosaic)  *last_coltag = ' ';
              last_coltag = c;
            }
            else if ((*c >= 0x10 && *c <= 0x17) && (hold_mosaic || (foreground_color != (color_t) (*c - 0x10))))
            {
              foreground_color = (color_t) (*c - 0x10);
              *c = (uint16_t) TTXT_COLORSYMBOLS[0][foreground_color];
              if(last_coltag && !hold_mosaic)  *last_coltag = ' ';
              last_coltag = c;
            }
            else if (*c == 0x1c && (background_color != COLOR_BLACK))
            {
              background_color = COLOR_BLACK;
              *c = (uint16_t) TTXT_COLORSYMBOLS[1][COLOR_BLACK];
            }
            else if (*c == 0x1d && (background_color != foreground_color))
            {
              background_color = foreground_color;
              *c = (uint16_t) TTXT_COLORSYMBOLS[1][foreground_color];
            }
            else if (*c == 0x1e)
            {
              *c = 0x25c6;  // hold mosaic
              hold_mosaic = TRUE;
              last_coltag = NULL;
            }
            else if (*c == 0x1f)
            {
              *c = 0x25c7;  // release mosaic
              hold_mosaic = FALSE;
              last_coltag = NULL;
            }
            else
              *c = ' ';
          }
          else
            last_coltag = NULL;
        }
        if(last_coltag && !hold_mosaic) *last_coltag = ' ';

        // Ende der Zeile ermitteln
        col_stop = 0;
        for (j = 39; j >= 0; j--)
          if(page->text[i][j] > 0x20)
          {
            col_stop = j + 1;
            break;
          }
        if(col_stop > 0) col_stop = 40;

        // Zeile ausgeben
        for (j = 0; j < col_stop; j++)
        {
          char u[5] = { 0, 0, 0, 0, 0 };

          if((page->text[i][j] & 0xff00) == 0xfb00)
            ucs2_to_utf8(u, 0x10000 + page->text[i][j]);
          else if(page->text[i][j] >= 0x20)
            ucs2_to_utf8(u, page->text[i][j]);
          else
            { u[0] = ' '; u[1] = '\0'; }
          ret = ( fprintf(f, "%s", u) ) && ret;
        }
        ret = ( fwrite("\r\n", 1, 2, f) == 2 ) && ret;
      }
    }
  }
  fclose(f);
  return ret;
}

bool CloseTeletextOut(void)
{
  unsigned long long    OutFileSize = 0;
  int                   k;
  bool                  first = TRUE, ret = TRUE;

  TRACEENTER;

  for (k = 0; k < NRTTXOUTPUTS; k++)
  {
    if (fTtxOut[k])
    {
      // output any pending close caption
      if (ExtractTeletext)
      {
        process_page(&page_buffer[k], pages[k], k, TRUE);  // hier den last_text und last_show_timestamp verwenden!
        memset(page_buffer[k].text, 0, sizeof(teletext_text_t));
        page_buffer[k].show_timestamp = 0;

        // this time we do not subtract any frames, there will be no more frames
        page_buffer[k].hide_timestamp = last_timestamp;
        if (page_buffer[k].tainted == YES)
          process_page(&page_buffer[k], pages[k], k, FALSE);  // hier wird text und show_timestamp genutzt
        memset(page_buffer[k].last_text, 0, sizeof(teletext_text_t));
        page_buffer[k].last_show_timestamp = 0;
      }

      ret = (/*fflush(fTtxOut[i]) == 0 &&*/ fclose(fTtxOut[k]) == 0) && ret;
 
      sprintf (&TeletextOut[TeletextOutLen], "_%03hx.srt", pages[k]);
      if (HDD_GetFileSize(TeletextOut, &OutFileSize) && (OutFileSize == 0))
        remove(TeletextOut);
      else if((config.page == pages[k]) || (!config.page && first)) 
      {
        char NewName[FBLIB_DIR_SIZE];
        snprintf(NewName, TeletextOutLen + 1, "%s", TeletextOut);
        sprintf (&NewName[TeletextOutLen], ".srt");
        rename(TeletextOut, NewName);
      }
      first = FALSE;
    }
    fTtxOut[k] = NULL;
  }

  PSBuffer_Reset(&TtxBuffer);
  TRACEEXIT;
  return ret;
}

void TtxProcessor_Free(void)
{
  if(page_buffer) { free(page_buffer); page_buffer = NULL; }
  if(page_buffer_in) { free(page_buffer_in); page_buffer_in = NULL; }
  if(page_buffer_all) { free(page_buffer_all); page_buffer_all = NULL; }
  if(page_map) { free(page_map); page_map = NULL; }
}
