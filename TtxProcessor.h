#ifndef __TTXPROCESSORH__
#define __TTXPROCESSORH__

#include "stdint.h"
#include "RecStrip.h"

typedef enum {
  DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
  DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
  DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0c,
  DATA_UNIT_VPS = 0xc3,
  DATA_UNIT_CLOSED_CAPTIONS = 0xc5
} data_unit_t;

// 1-byte alignment; just to be sure, this struct is being used for explicit type conversion
// FIXME: remove explicit type conversion from buffer to structs
#pragma pack(push, 1)
typedef struct {
  uint8_t _clock_in; // clock run in
  uint8_t _framing_code; // framing code, not needed, ETSI 300 706: const 0xe4
  uint8_t address[2];
  uint8_t data[40];
}__attribute__((packed)) teletext_packet_payload_t;
#pragma pack(pop)

extern dword            global_timestamp;
extern dword            last_timestamp;

char* TimeStr(time_t UnixTimeStamp);
//char* TimeStr_UTC(time_t UnixTimeStamp);
char* TimeStrTF(tPVRTime TFTimeStamp, byte TFTimeSec);
char* TimeStr_DB(tPVRTime TFTimeStamp, byte TFTimeSec);
void SetTeletextBreak(bool NewInputFile, bool NewOutputFile, word SubtitlePage);
void TtxProcessor_SetOverwrite(bool DoOverwrite);
void TtxProcessor_Init(word SubtitlePage);
bool LoadTeletextOut(const char* AbsOutFile);
void ProcessTtxPacket(tTSPacket *Packet, long long FilePos);
word telx_to_ucs2(byte c);
void ucs2_to_utf8(char *r, word ch);
//void process_telx_packet(data_unit_t data_unit_id, teletext_packet_payload_t *packet, uint32_t timestamp);
uint16_t process_pes_packet(byte *buffer, word size);
bool WriteAllTeletext(char *AbsOutFile);
bool CloseTeletextOut(void);
void TtxProcessor_Free(void);

#endif
