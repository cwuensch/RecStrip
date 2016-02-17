#define _LARGEFILE64_SOURCE
#define __USE_LARGEFILE64  1
#define _FILE_OFFSET_BITS  64
#ifdef _MSC_VER
  #define __const const
  #define __attribute__(a)
  #pragma pack(1)
  #define inline
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../../../../../Topfield/API/TMS/include/type.h"
#include "NALUDump.h"
#include "RecStrip.h"

#ifdef _WIN32
  #define fseeko64 _fseeki64
  #define ftello64 _ftelli64
#endif

extern FILE            *fIn;  // dirty Hack

// Globale Variablen
static eNaluFillState   NaluFillState = NALU_NONE;
static bool             SliceState = TRUE;

static unsigned int     History = 0xffffffff;

static int              LastContinuityInput = -1;
static int              LastContinuityOutput = -1;
//static int              ContinuityOffset = 0;

static bool             DropAllPayload = FALSE;

static int              PesId = -1;
static int              PesOffset = 0;
static int              NaluOffset = 0;

static bool             LastEndedWithNull = TRUE, LastEndedWith3Nulls = FALSE;

// ----------------------------------------------
// *****  NALU Dump  *****
// ----------------------------------------------

inline int TsGetPID(tTSPacketHeader *Packet)
{
  return ((Packet->PID1 *256) | Packet->PID2);
}
inline int TsPayloadOffset(tTSPacketHeader *Packet)
{
  int o = (Packet->Adapt_Field_Exists) ? Packet->Adapt_Field_Length+5 : 4;
  return (o <= TS_SIZE ? o : TS_SIZE);
}

static void TsExtendAdaptionField(byte *Packet, int ToLength)
{
  // Hint: ExtenAdaptionField(p, TsPayloadOffset(p) - 4) is a null operation
  tTSPacketHeader *TSPacket;
  int Offset;
  int NewPayload;

  TRACEENTER;
  TSPacket = (tTSPacketHeader*) Packet;
  Offset = TsPayloadOffset(TSPacket); // First byte after existing adaption field

  if (ToLength <= 0)
  {
    // Remove adaption field
    TSPacket->Adapt_Field_Exists = 0;
    TRACEEXIT;
    return;
  }

  // Set adaption field present
  TSPacket->Adapt_Field_Exists = 1;

  // Set new length of adaption field:
  TSPacket->Adapt_Field_Length = (ToLength <= TS_SIZE-4) ? ToLength-1 : TS_SIZE-4-1;

  if (TSPacket->Adapt_Field_Length == TS_SIZE-4-1)
  {
    // No more payload, remove payload flag
    TSPacket->Payload_Exists = 0;
  }

  NewPayload = TSPacket->Adapt_Field_Length + 5; // First byte after new adaption field

  // Fill new adaption field
  if (Offset == 4 && Offset < NewPayload)
    Offset++; // skip adaptation_field_length
  if (Offset == 5 && Offset < NewPayload)
    Packet[Offset++] = 0; // various flags set to 0
  while (Offset < NewPayload)
    Packet[Offset++] = 0xff; // stuffing byte

  TRACEEXIT;
}

static void ProcessPayload_HD(unsigned char *Payload, int size, bool PayloadStart, sPayloadInfo *Info)
{
  bool DropByte;
  int LastKeepByte = -1, i;

  TRACEENTER;
  Info->DropPayloadStartBytes = 0;
  Info->DropPayloadEndBytes = 0;
  Info->ZerosOnly = TRUE;

  if (PayloadStart)
  {
    History = 0xffffffff;
    PesId = -1;
    NaluFillState = NALU_NONE;
  }

  for (i = 0; i < size; i++)
  {
    if (Payload[i] != 0) Info->ZerosOnly = FALSE;
    DropByte = FALSE;

    History = (History << 8) | Payload[i];
    PesOffset++;
    NaluOffset++;

    if (History >= 0x000001B9 && History <= 0x000001FF)  // im Original: >= 0x00000180
    {
      // Start of PES packet
      PesId = History & 0xff;
      PesOffset = 0;
      NaluFillState = NALU_NONE;
    }
    else if (PesId >= 0xe0 && PesId <= 0xef // video stream
     && History >= 0x00000100 && History <= 0x0000017F) // NALU start code
    {
      int NaluId = History & 0xff;
      NaluOffset = 0;
      NaluFillState = ((NaluId & 0x1f) == 0x0c) ? NALU_FILL : NALU_NONE;
    }

    if (PesId >= 0xe0 && PesId <= 0xef // video stream
     && PesOffset >= 1 && PesOffset <= 2)
    {
      Payload[i] = 0; // Zero out PES length field
    }

    if (NaluFillState == NALU_FILL && NaluOffset > 0) // Within NALU fill data
    {
      // We expect a series of 0xff bytes terminated by a single 0x80 byte.

      if (Payload[i] == 0xFF)
      {
        DropByte = TRUE;
      }
      else if (Payload[i] == 0x80)
      {
        NaluFillState = NALU_TERM; // Last byte of NALU fill, next byte sets NaluFillEnd=true
        DropByte = TRUE;
      }
      else // Invalid NALU fill
      {
        printf("cNaluDumper: Unexpected NALU fill data: %02x\n", Payload[i]);
        NaluFillState = NALU_END;
        if (LastKeepByte == -1)
        {
          // Nalu fill from beginning of packet until last byte
          // packet start needs to be dropped
          Info->DropPayloadStartBytes = i;
        }
      }
    }
    else if (NaluFillState == NALU_TERM) // Within NALU fill data
    {
      // We are after the terminating 0x80 byte
      NaluFillState = NALU_END;
      if (LastKeepByte == -1)
      {
        // Nalu fill from beginning of packet until last byte
        // packet start needs to be dropped
        Info->DropPayloadStartBytes = i;
      }
    }

    if (!DropByte)
      LastKeepByte = i; // Last useful byte
  }

  Info->DropAllPayloadBytes = (LastKeepByte == -1);
  Info->DropPayloadEndBytes = size-1-LastKeepByte;
  TRACEEXIT;
}

static void ProcessPayload_SD(unsigned char *Payload, int size, bool PayloadStart, sPayloadInfo *Info)
{
  byte StartCodeID = 0;
  int i;

  TRACEENTER;
  Info->ZerosOnly = TRUE;

  if (PayloadStart)
  {
    History = 0xffffffff;
    PesId = -1;
    SliceState = FALSE;
  }

  for (i = 0; i < size; i++)
  {
    if (Payload[i] != 0) Info->ZerosOnly = FALSE;

    History = (History << 8) | Payload[i];
    PesOffset++;

    if ((History & 0xFFFFFF00) == 0x00000100)
    {
      StartCodeID = Payload[i];
      if (StartCodeID >= 0xB9)
      {
        // Start of PES packet
        PesId = StartCodeID;
        PesOffset = 0;
      }
      if (PesId >= 0xe0 && PesId <= 0xef)  // video stream
        SliceState = (StartCodeID >= 0x01 && StartCodeID <= 0xAF);
    }

    if (PesId >= 0xe0 && PesId <= 0xef  // video stream
     && PesOffset >= 1 && PesOffset <= 2)
    {
      Payload[i] = 0; // Zero out PES length field
    }
  }
  TRACEEXIT;
}

int ProcessTSPacket(unsigned char *Packet, unsigned long long FilePosition)
{
  int ContinuityOffset = 0;
  int ContinuityInput;
  tTSPacketHeader *TSPacket = (tTSPacketHeader*) Packet;

  TRACEENTER;

  // Check continuity:
  ContinuityInput = TSPacket->ContinuityCount;
  if (LastContinuityInput >= 0)
  {
    int NewContinuityInput = TSPacket->Payload_Exists ? (LastContinuityInput + 1) % 16 : LastContinuityInput;
    ContinuityOffset = (NewContinuityInput - ContinuityInput) % 16;
    if (ContinuityOffset > 0)
      printf("cNaluDumper: TS continuity offset %i (pos=%llu)\n", ContinuityOffset, FilePosition);
//    if (Offset > ContinuityOffset)
//      ContinuityOffset = Offset; // max if packets get dropped, otherwise always the current one.
  }
  LastContinuityInput = ContinuityInput;

  if (TSPacket->Payload_Exists)
  {
    sPayloadInfo Info;
    bool DropThisPayload = FALSE;
    int Offset = TsPayloadOffset(TSPacket);

    if (isHDVideo)
    {
      ProcessPayload_HD(&Packet[Offset], TS_SIZE - Offset, TSPacket->Payload_Unit_Start, &Info);

      if (DropAllPayload && !Info.DropAllPayloadBytes)
      {
        // Return from drop packet mode to normal mode
        DropAllPayload = FALSE;

        // Does the packet start with some remaining NALU fill data?
        if (Info.DropPayloadStartBytes > 0)
        {
          // Add these bytes as stuffing to the adaption field.

          // Sample payload layout:
          // FF FF FF FF FF 80 00 00 01 xx xx xx xx
          //     ^DropPayloadStartBytes

          TsExtendAdaptionField(Packet, Offset - 4 + Info.DropPayloadStartBytes);
        }
      }

      DropThisPayload = DropAllPayload;
      if (!DropAllPayload && Info.DropPayloadEndBytes > 0) // Payload ends with 0xff NALU Fill
      {
        // Last packet of useful data
        // Do early termination of NALU fill data
        Packet[TS_SIZE-1] = 0x80;
        DropAllPayload = TRUE;
        // Drop all packets AFTER this one

        // Since we already wrote the 0x80, we have to make sure that
        // as soon as we stop dropping packets, any beginning NALU fill of next
        // packet gets dumped. (see DropPayloadStartBytes above)
      }
    }
    else
      ProcessPayload_SD(Packet + Offset, TS_SIZE - Offset, TSPacket->Payload_Unit_Start, &Info);

    if (DropThisPayload)
    {
      if (TSPacket->Adapt_Field_Exists)
        // Drop payload data, but keep adaption field data
        TsExtendAdaptionField(Packet, TS_SIZE-4);
      else
      {
        TRACEEXIT;
        return 1;  // Drop packet
      }
    }

    if (Info.ZerosOnly && !TSPacket->Adapt_Field_Exists && (isHDVideo || SliceState) && LastEndedWithNull)
    {
      // Pr�fe ob Folgepaket mit 00 00 anf�ngt
      // dirty Hack: Greife auf den FileStream von RecStrip zu (unsch�n, aber ich wei� keine bessere L�sung)
      byte               Buffer[192];
      tTSPacketHeader   *tmpPacket = (tTSPacketHeader*) &Buffer[PACKETOFFSET];
//      unsigned long long OldFilePos = ftello64(fIn);
      int                CurPid, tmpPayload, i;

//printf("Potential zero-byte-stuffing found at position %llu", FilePosition);
      if (LastEndedWith3Nulls)  // wenn 3 Nullen am Ende -> dann darf FolgePaket ohne anfangen
      {
//printf(" --> confirmed by LastEndedWith3Nulls!\n");
        TRACEEXIT;
        return 2;
      }

      CurPid = TsGetPID(TSPacket);
      for (i = 0; i < 10; i++)
      {
        size_t ReadBytes = fread(Buffer, 1, PACKETSIZE, fIn);
        if (ReadBytes > 0)
        {
          if ((tmpPacket->SyncByte=='G') && (TsGetPID(tmpPacket)==CurPid) && ((tmpPayload = TsPayloadOffset(tmpPacket)) < TS_SIZE-2))
          {
            if (Buffer[PACKETOFFSET + tmpPayload] == 0 && Buffer[PACKETOFFSET + tmpPayload + 1] == 0)
            {
//printf(" --> confirmed by NextStartsWith00!\n");
              fseeko64(fIn, FilePosition + PACKETSIZE, SEEK_SET);
              TRACEEXIT;
              return 2;
            }
            else
            {
printf("WARNING!!! No StartCode in following packet!!! (pos=%llu)\n", FilePosition);
              break;
            }
          }
        }
      }
      fseeko64(fIn, FilePosition + PACKETSIZE, SEEK_SET);
    }
  }
  LastEndedWithNull   = (Packet[TS_SIZE-1] == 0);  // wird nur gesetzt f�r das zuletzt erhaltene Paket
  LastEndedWith3Nulls = (Packet[TS_SIZE-1] == 0 && Packet[TS_SIZE-2] == 0 && Packet[TS_SIZE-3] == 0);

  // Fix Continuity Counter and reproduce incoming offsets:
  {
    int NewContinuityOutput = ContinuityInput;
    if (LastContinuityOutput >= 0)
      NewContinuityOutput = (TSPacket->Payload_Exists) ? (LastContinuityOutput + 1) % 16 : LastContinuityOutput;
    NewContinuityOutput = (NewContinuityOutput + ContinuityOffset) % 16;
    TSPacket->ContinuityCount = NewContinuityOutput;
    LastContinuityOutput = NewContinuityOutput;
    ContinuityOffset = 0;
  }
  TRACEEXIT;
  return 0; // Keep packet
}
