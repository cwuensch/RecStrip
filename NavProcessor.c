#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "type.h"
#include "NavProcessor.h"
#include "InfProcessor.h"
#include "RecStrip.h"

//#define DEBUGLOG 1
#define COUNTSTACKSIZE 8


// Globale Variablen
dword                   NrFrames = 0;
dword                   LastTimems = 0;
dword                  *pOutNextTimeStamp = NULL;
FILE                   *fNavIn = NULL;
static FILE            *fNavOut = NULL;
static long long        PosFirstNull = 0, PosSecondNull = 0, HeaderFound = 0;
static dword            PTS = 0;
static byte             PTSBuffer[16];
static int              PTSBufFill = 0;  // 0: keinen PTS suchen, 1..15 Puffer-F�llstand, bei 16 PTS auslesen und zur�cksetzen
static byte             FrameCtr = 0, FrameOffset = 0;
static bool             WaitForIFrame = TRUE, WaitForPFrame = FALSE, FirstPacketAfterCut = FALSE, FirstRecordAfterCut = TRUE;
static dword            FirstNavPTS = 0, LastNavPTS = 0, PTSJump = 0;
static bool             FirstNavPTSOK = FALSE;
static int              TimeOffset = 0;

//HDNAV
static tnavHD           navHD;
static tPPS             PPS[10];
static int              PPSCount = 0;
static unsigned long long SEI = 0, SPS = 0, AUD = 0;
static bool             GetPPSID = FALSE, GetSlicePPSID = FALSE, GetPrimPicType = FALSE;
static byte             SlicePPSID = 0;
static dword            FirstSEIPTS = 0, SEIPTS = 0, IFramePTS = 0, SPSLen = 0;
static dword            FirstPCR = 0;
static dword            Golomb = 0;
static byte             GolombFull = 0;
//static tFrameCtr        CounterStack[COUNTSTACKSIZE];
//static int              LastIFrame = 0;

static long long        dbg_NavPictureHeaderOffset = 0, dbg_SEIFound = 0;
static long long        dbg_CurrentPosition = 0, dbg_PositionOffset = 0, dbg_HeaderPosOffset = 0, dbg_SEIPositionOffset = 0;

//SDNAV
static tnavSD           navSD;
static unsigned long long PictHeader = 0, LastPictureHeader = 0;
static unsigned long long CurrentSeqHeader = 0;
static dword            FirstPTS = 0 /*, LastdPTS = 0*/;
static int              NavPtr = 0;
static word             FirstSHPHOffset = 0;

//ProcessNavFile / QuickNavProcess
static byte             NavBuffer[sizeof(tnavHD)];
static tnavSD          *curSDNavRec = (tnavSD*) &NavBuffer[0];
static long long        NextPictureHeaderOffset = 0;
static bool             FirstRun = TRUE;


// ----------------------------------------------
// *****  PROCESS NAV FILE  *****
// ----------------------------------------------

static int get_ue_golomb32(/* byte *p */ dword d, byte *StartBit)
{
  int                   leadingZeroBits;
//  dword                 d;

  TRACEENTER;
//  d = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  if(d == 0 || *StartBit > 32)
  {
    TRACEEXIT;
    return 0;
  }

  leadingZeroBits = 0;
  while((*StartBit > 1) && (((d >> *StartBit) & 1) == 0))
  {
    leadingZeroBits++;
    *StartBit = *StartBit - 1;
  }
  *StartBit = *StartBit - 1;
  
  if (*StartBit >= leadingZeroBits)
  {
    d >>= (*StartBit + 1 - leadingZeroBits);
    d &= ((1 << leadingZeroBits) - 1);

    *StartBit = *StartBit - leadingZeroBits;
    TRACEEXIT;
    return (1 << leadingZeroBits) - 1 + d;
  }
  printf("Assertion error: get_ue_golomb32() used more than 32 bits!\n");
  *StartBit = 0;
  TRACEEXIT;
  return 0;
}

/*static int get_ue_golomb8(byte d, byte *StartBit)
{
  int                   leadingZeroBits;

  TRACEENTER;
  if(d == 0 || *StartBit > 7)
  {
    TRACEEXIT;
    return 0;
  }

  leadingZeroBits = 0;
  while((*StartBit > 1) && (((d >> *StartBit) & 1) == 0))
  {
    leadingZeroBits++;
    *StartBit = *StartBit - 1;
  }
  *StartBit = *StartBit - 1;

  if (*StartBit >= leadingZeroBits)
  {
    d >>= (*StartBit + 1 - leadingZeroBits);
    d &= ((1 << leadingZeroBits) - 1);

    *StartBit = *StartBit - leadingZeroBits;
    TRACEEXIT;
    return (1 << leadingZeroBits) - 1 + d;
  }
  TRACEEXIT;
  printf("Assertion error: get_ue_golomb8() used more than 8 bits!\n");
  *StartBit = 0;
  return 0;
} */

bool GetPTS2(byte *pBuffer, dword *pPTS, dword *pDTS)  // 33 bits, 90 kHz, returns divided by 2
{
  bool ret = FALSE;
  TRACEENTER;
  if (/*pBuffer &&*/ (((pBuffer[0] & 0xf0) == 0xE0) || (pBuffer[0] >= 0xB9)) && ((pBuffer[3] & 0xC0) == 0x80))
  {
    //MPEG Video Stream
    //00 00 01 E0 00 00 88 C0 0B 35 BD E9 8A 85 15 BD E9 36 25 FF
    //0000            = PES Packet Length
    //88              = PES Scrambling = no; PES Priority
    //C0              = PTS/DTS
    //0B              = PES Header Data Length
    //35 BD E9 8A 85  = PTS  = 1010 1111 0111 1010 0100 0101 0100 0010 = AF7A4542
    //15 BD E9 36 25  = DTS  = 1010 1011 0111 1010 0001 1011 0001 0010 = AB7A1B12
    //FF

    //...1010.XXXXXXXXXXXXXXXXXXXXXXXXXXXXX.
    //.......10111101XXXXXXXXXXXXXXXXXXXXXX.
    //...............1110100.XXXXXXXXXXXXXX.
    //......................10001010XXXXXXX.
    //..............................1000010.

    //Return PTS >> 1 so that we do not need 64 bit variables
    if ((pBuffer[4] & 0x80) && ((pBuffer[6] & 0xe1) == 0x21) && (pBuffer[8] & 0x01) && (pBuffer[10] & 0x01))
    {
      dword PTS;
      PTS = ((pBuffer[ 6] & 0x0e) << 28) |
            ((pBuffer[ 7] & 0xff) << 21) |
            ((pBuffer[ 8] & 0xfe) << 13) |
            ((pBuffer[ 9] & 0xff) <<  6) |
            ((pBuffer[10] & 0xfe) >>  2);
      if(pPTS) *pPTS = PTS;
      if(pDTS) *pDTS = PTS;
      ret = TRUE;
    }
    if (pDTS && (pBuffer[4] & 0xC0) && ((pBuffer[6] & 0xf1) == 0x31) && ((pBuffer[11] & 0xf1) == 0x11) && (pBuffer[13] & 0x01) && (pBuffer[15] & 0x01))
      *pDTS = ((pBuffer[11] & 0x0e) << 28) |
              ((pBuffer[12] & 0xff) << 21) |
              ((pBuffer[13] & 0xfe) << 13) |
              ((pBuffer[14] & 0xff) <<  6) |
              ((pBuffer[15] & 0xfe) >>  2);
  }
  TRACEEXIT;
  return ret;
}
bool GetPTS(byte *pBuffer, dword *pPTS, dword *pDTS)
{
//  if(/*pBuffer &&*/ (pBuffer[0] == 0x00) && (pBuffer[1] == 0x00) && (pBuffer[2] == 0x01))
  if (/*pBuffer &&*/ (memcmp(pBuffer, "\0\0\1", 3) == 0))
    return GetPTS2(&pBuffer[3], pPTS, pDTS);
  else return FALSE;
}
bool SetPTS2(byte *pBuffer, dword newPTS)  // 33 bits, 90 kHz, returns divided by 2
{
  TRACEENTER;
  if (pBuffer && (((pBuffer[0] & 0xf0) == 0xE0) || (pBuffer[0] >= 0xB9)) && ((pBuffer[3] & 0xC0) == 0x80))
  {
    if ((pBuffer[4] & 0x80) && ((pBuffer[6] & 0xe0) == 0x20))
    {
      pBuffer[ 6] = (pBuffer[4] & 0xf0) | (newPTS >> 28) | 0x01;
      pBuffer[ 7] = ((newPTS >> 21) & 0xff);
      pBuffer[ 8] = ((newPTS >> 13) & 0xfe) | 0x01;
      pBuffer[ 9] = ((newPTS >>  6) & 0xff);
      pBuffer[10] = ((newPTS <<  2) & 0xfc) | 0x01;
      TRACEEXIT;
      return TRUE;
    }
  }
  TRACEEXIT;
  return FALSE;
}

/*byte* FindPTS(byte *pBuffer, int BufferLen, dword *pPTS)
{
  int NrNullBytes = 0;
  byte *p = pBuffer, *BufferEnd = &pBuffer[BufferLen - 11];

  while (p < BufferEnd)
  {
    if (*p == 0)
    {
      if(NrNullBytes < 2) NrNullBytes++;
    }
    else if ((*p == 1) && (NrNullBytes >= 2))
    {
      if ((((p[1] & 0xf0) == 0xE0) || (p[1] >= 0xB9)) && ((p[4] & 0xC0) == 0x80))
        return (GetPTS2(&p[1], pPTS, NULL) ? p+1 : NULL);
    }
    else NrNullBytes = 0;
  }
  return NULL;
} */
byte* FindPTS(byte *pBuffer, int BufferLen, dword *pPTS)
{
  int Header = 0xffffffff;
  byte *p = pBuffer, *BufferEnd = &pBuffer[BufferLen - 10];

  while (p < BufferEnd)
  {
    Header = (Header << 8) | *p;
    if ((((Header & 0xffffff00) == 0x00000100) && (((p[0] & 0xf0) == 0xE0) || (p[0] >= 0xB9))) && ((p[3] & 0xC0) == 0x80))
    {
      if (GetPTS2(p, pPTS, NULL)) return p;
      else break;
    }
    p++;
  }
  return NULL;
}


bool GetPCR(byte *pBuffer, long long *pPCR)  // 33 bits (90 kHz) + 9 bits (27 MHz)
{
  TRACEENTER;
  if (/*pBuffer &&*/ (pBuffer[0] == 0x47) && ((pBuffer[3] & 0x20) != 0) && (pBuffer[4] > 0) && (pBuffer[5] & 0x10))
  {
    if (pPCR)
    {
      //Extract the time out of the PCR bit pattern
      //The PCR is clocked by a 27 MHz generator.
      *pPCR = (((long long)pBuffer[6] << 25) | (pBuffer[7] << 17) | (pBuffer[8] << 9) | pBuffer[9] << 1 | pBuffer[10] >> 7);
      *pPCR = *pPCR * 300 + ((pBuffer[10] & 0x1) << 8 | pBuffer[11]);
    }
    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}
bool GetPCRms(byte *pBuffer, dword *pPCR)  // 33 bits, 90 kHz, returns divided by 2, then by 45
{
  TRACEENTER;
  if (/*pBuffer &&*/ (pBuffer[0] == 0x47) && ((pBuffer[3] & 0x20) != 0) && (pBuffer[4] > 0) && (pBuffer[5] & 0x10))
  {
    if (pPCR)
    {
      //Extract the time out of the PCR bit pattern
      //The PCR is clocked by a 90kHz generator. To convert to milliseconds
      //the 33 bit number can be shifted right and divided by 45
      *pPCR = (dword)((((dword)pBuffer[6] << 24) | (pBuffer[7] << 16) | (pBuffer[8] << 8) | pBuffer[9]) / 45);
    }
    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}
bool SetPCR(byte *pBuffer, long long pPCR)
{
  TRACEENTER;
  if (pBuffer && (pBuffer[0] == 0x47) && ((pBuffer[3] & 0x20) != 0) && (pBuffer[4] >= 7))
  {
    long long base = (pPCR / 300LL);
    int ext = (int) (pPCR % 300);

    pBuffer[5]   = 0x10;
    pBuffer[10]  = 0x7e;
    pBuffer[6]   = (byte) ((base >> 25) & 0xff);
    pBuffer[7]   = (byte) ((base >> 17) & 0xff);
    pBuffer[8]   = (byte) ((base >> 9) & 0xff);
    pBuffer[9]   = (byte) ((base >> 1) & 0xff);
    pBuffer[10] |= (byte) ((base & 1) << 7);
    pBuffer[10] |= (byte) ((ext >> 8) & 0xff);
    pBuffer[11]  = (byte) (ext & 0xff);
  }
  TRACEEXIT;
  return FALSE;
}

int DeltaPCR(dword FirstPCR, dword SecondPCR)
{
  if (FirstPCR <= SecondPCR)
    return (SecondPCR - FirstPCR);
  else
  {
    if (FirstPCR - SecondPCR <= 450000)
      // Erlaube "R�ckspr�nge", wenn weniger als 10 sek
      return (int)(SecondPCR - FirstPCR);
    else
      // �berlauf des 90 kHz Counters (halbiert)
      return (0xffffffff - FirstPCR + SecondPCR + 1);
  }
}
int DeltaPCRms(dword FirstPCR, dword SecondPCR)
{
  if (FirstPCR <= SecondPCR)
    return (SecondPCR - FirstPCR);
  else
  {
    if (FirstPCR - SecondPCR <= 10000)
      // Erlaube "R�ckspr�nge", wenn weniger als 10 sek
      return (int)(SecondPCR - FirstPCR);
    else
      // �berlauf des 90 kHz Counters (in Millisekunden)
      return (95443718 - FirstPCR + SecondPCR);
  }
}

dword FindSequenceHeaderCode(byte *Buffer, int BufferLen)
{
  dword buf = 0xffffffff;
  byte *p;
  for(p = Buffer; p < Buffer + BufferLen; p++)
  {
    buf = (buf << 8) | *p;
    if (buf == 0x000001b3)
      return (dword)(p - Buffer + 5);
  }
  return 0;
}

dword FindPictureHeader(byte *Buffer, int BufferLen, byte *pFrameType, dword *pInOutEndNulls)  // 1 = I-Frame, 2 = P-Frame, 3 = B-Frame (?)
{
  dword buf = 0xffffffff, ret = 0;
  byte *p, *q;

  TRACEENTER;
  if(pInOutEndNulls)
    buf = *pInOutEndNulls;
  if(pFrameType) *pFrameType = 0;

  for(p = Buffer; p < Buffer + BufferLen - (isHDVideo ? 1 : 2); p++)
  {
    buf = (buf << 8) | *p;
    if ((buf & 0xffffff80) == 0x00000100)
    {
      if (isHDVideo)
      {
        // MPEG4 picture header: 
        if(pFrameType)
        {
          *pFrameType = ((p[1] >> 5) & 7) + 1;
          if (*pFrameType == 3)
          {
            buf = 0xffffffff;
            for(q = p+1; q < Buffer + BufferLen; q++)
            {
              if ((buf & 0x00ffffff) == 0x00000001)
              {
                byte NALType = *q & 0x1f;
                if ((NALType >= NAL_SLICE) && (NALType <= NAL_SLICE_IDR))
                {
                  // byte NALRefIdc = ((q[1] >> 5) & 3);
                  if (((*q >> 5) & 3) >= 2) *pFrameType = 2;
                  break;
                }
              }
              buf = (buf << 8) | *q;
            }
          }
        }
        ret = (dword)(p - Buffer + 3);
        break;
      }
      else if (*p == 0x00)
      {
        // MPEG2 picture header: http://dvdnav.mplayerhq.hu/dvdinfo/mpeghdrs.html#picture
        if(pFrameType)  *pFrameType = (p[2] >> 3) & 0x03;
        ret = (dword)(p - Buffer + 6);
        break;
      }
    }
  }

  if (pInOutEndNulls)
  {
    if(p < Buffer + BufferLen - 3)
    {
      buf = 0xffffffff;
      p = Buffer + BufferLen - 3;
    }
    while (p < Buffer + BufferLen)
      buf = (buf << 8) | *(p++);
    *pInOutEndNulls = buf;
  }
  TRACEEXIT;
  return ret;
}


void NavProcessor_Init(void)
{
  TRACEENTER;

  // Globale Variablen
  LastTimems = 0; TimeOffset = 0;
  pOutNextTimeStamp = NULL;
//  *fNavIn = NULL; *fNavOut = NULL;
  PosFirstNull = 0; PosSecondNull = 0; HeaderFound = 0;
  PTS = 0;
  PTSBufFill = 0;  // 0: keinen PTS suchen, 1..15 Puffer-F�llstand, bei 16 PTS auslesen und zur�cksetzen
  NrFrames = 0; FrameCtr = 0; FrameOffset = 0;
  WaitForIFrame = TRUE; WaitForPFrame = FALSE; FirstPacketAfterCut = FALSE; FirstRecordAfterCut = TRUE;
  FirstNavPTS = 0; LastNavPTS = 0; PTSJump = 0;
  FirstNavPTSOK = FALSE;

  //HDNAV
  PPSCount = 0;
  SEI = 0; SPS = 0; AUD = 0;
  GetPPSID = FALSE; GetSlicePPSID = FALSE; GetPrimPicType = FALSE;
  SlicePPSID = 0;
  FirstSEIPTS = 0; SEIPTS = 0; IFramePTS = 0; SPSLen = 0;
  FirstPCR = 0;
  GolombFull = 0;
//  LastIFrame = 0;

  dbg_NavPictureHeaderOffset = 0; dbg_SEIFound = 0;
  dbg_CurrentPosition = 0; dbg_PositionOffset = 0; dbg_HeaderPosOffset = 0; dbg_SEIPositionOffset = 0;

  //SDNAV
  PictHeader = 0; LastPictureHeader = 0;
  CurrentSeqHeader = 0;
  FirstPTS = 0 /*, LastdPTS = 0*/;
  NavPtr = 0;
  FirstSHPHOffset = 0;

  //ProcessNavFile / QuickNavProcess
  NextPictureHeaderOffset = 0;
  FirstRun = TRUE;

  memset(&navHD, 0, sizeof(tnavHD));
  memset(&navSD, 0, sizeof(tnavSD));

  TRACEEXIT;
}

void SetFirstPacketAfterBreak()
{
  FirstPacketAfterCut = TRUE;
  CurrentSeqHeader = 0;
//  SEI = 0;
//  WaitForIFrame = TRUE;
}

static void HDNAV_ParsePacket(tTSPacket *Packet, long long FilePositionOfPacket)
{
  byte                  PayloadStart, Ptr;
  byte                  NALType;
  dword                 PCR;
  bool                  SEIFoundInPacket = FALSE;
  #if DEBUGLOG != 0
    char                  s[80];
  #endif

  TRACEENTER;

  // PROCESS THE TS-PACKET

  // Valid TS packet and payload available?
  if((Packet->SyncByte != 'G') || !Packet->Payload_Exists)
  {
    TRACEEXIT;
    return;
  }

  if (Packet->Payload_Unit_Start)
  {
    #if DEBUGLOG != 0
      printf("\n%8.8llx: PES Start\n", FilePositionOfPacket);
    #endif
    PosFirstNull = 0; PosSecondNull = 0; HeaderFound = 0; PTSBufFill = 0;
    // alles andere auch zur�cksetzen?
  }

  // Adaptation field available?
  if(Packet->Adapt_Field_Exists)
  {
    // If available, get the PCR
    if(GetPCRms((byte*)Packet, &PCR))
    {
      if(FirstPCR == 0) FirstPCR = PCR;
      #if DEBUGLOG != 0
        printf("%8.8llx: PCR = 0x%8.8x, dPCR = 0x%8.8x\n", PrimaryTSOffset, PCR, DeltaPCRms(FirstPCR, PCR));
      #endif
    }
    PayloadStart = Packet->Data[0] + 1;
  }
  else
    PayloadStart = 0;


  // PROCESS THE PAYLOAD

  // Search for start codes in the packet. Continue from last packet
  Ptr = PayloadStart;
  while(Ptr < 184)
  {
    // Check for Header
    if (!HeaderFound)
    {
      if (PosFirstNull && PosSecondNull && (Packet->Data[Ptr] == 1))
      {
        HeaderFound = PosFirstNull;
dbg_HeaderPosOffset = dbg_PositionOffset - (Ptr-PayloadStart <= 1 ? dbg_DelBytesSinceLastVid : 0);
      }
      
      if (Packet->Data[Ptr] == 0)
      {
        PosFirstNull = PosSecondNull;
        PosSecondNull = FilePositionOfPacket + 4 + Ptr;
      }
      else
        PosSecondNull = 0;

      if (HeaderFound) { Ptr++; continue; }
    }

    // Process PTS
    if (PTSBufFill)
    {
      PTSBuffer[PTSBufFill++] = Packet->Data[Ptr];
      if (PTSBufFill > 10)
      {
        if(GetPTS2(PTSBuffer, &PTS, NULL))
        {
          #if DEBUGLOG != 0
            printf("%8.8llx: PTS = 0x%8.8x, DTS = 0x%8.8x\n", PrimaryPayloadOffset + Ptr, PTS, DTS);
          #endif
        }
        PTSBufFill = 0;
      }
    }

    // Process access on Packet[Ptr+4]
    if (GetPPSID || GetSlicePPSID)
    {
      if (GolombFull < 4)
      {
        Golomb = Golomb << 8 | Packet->Data[Ptr];
        GolombFull++;
      }
      if (GolombFull >= 4)
      {
        if (GetPPSID)
        {
          byte Bit = 31;
          PPS[PPSCount-1].ID = get_ue_golomb32(Golomb, &Bit);

          #if DEBUGLOG != 0
            snprintf(&s[strlen(s)], sizeof(s)-strlen(s), " (ID=%d)", PPS[PPSCount-1].ID);
          #endif
          GetPPSID = FALSE;
        }
        if (GetSlicePPSID)
        {
          byte Bit = 31;
          if (Ptr >= 180)
            printf("Assertion error (warning): get_ue_golomb32() called with less than 4 bytes left!\n");

          //first_mb_in_slice
          get_ue_golomb32(Golomb, &Bit);
          //slice_type
          get_ue_golomb32(Golomb, &Bit);
          //pic_parameter_set_id
          SlicePPSID = get_ue_golomb32(Golomb, &Bit);

          #if DEBUGLOG != 0
            snprintf(&s[strlen(s)], sizeof(s)-strlen(s), " (PPS ID=%hu)\n", SlicePPSID);
          #endif
          GetSlicePPSID = FALSE;
        }
      }
    }
    if (GetPrimPicType)
    {
      navHD.FrameType = ((Packet->Data[Ptr] >> 5) & 7) + 1;
      GetPrimPicType = FALSE;
    }

    // Process a NALU header
    if (HeaderFound)
    {
      // Calculate the length of the SPS NAL
      if ((SPS != 0) && (SPSLen == 0))
        SPSLen = (dword) (HeaderFound - SPS);

      // Calculate the length of the previous PPS NAL
      if ((PPSCount > 0) && (PPS[PPSCount - 1].Len == 0))
        PPS[PPSCount - 1].Len = (word) (HeaderFound - PPS[PPSCount - 1].Offset);

      if((Packet->Data[Ptr] & 0x80) != 0x00)
      {
        // PTS-Puffer starten
        PTSBuffer[0] = Packet->Data[Ptr];
        PTSBufFill = 1;
      }
      else
      {
        NALType = Packet->Data[Ptr] & 0x1f;

        switch(NALType)
        {
          case NAL_SEI:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_SEI");
            #endif
//            if (SEI == 0 || NavPtr == 0)  // jkIT: nur die erste SEI seit dem letzten Nav-Record nehmen
            if (!SEIFoundInPacket)        // CW: nur die erste SEI innerhalb des Packets nehmen
            {
              SEI = HeaderFound;
              SEIPTS = PTS;
              SEIFoundInPacket = TRUE;

dbg_SEIPositionOffset = dbg_HeaderPosOffset;
dbg_SEIFound = dbg_CurrentPosition/PACKETSIZE;
//printf("%lld: SEI found: %lld\n", dbg_CurrentPosition/PACKETSIZE, SEI);

              if (FirstPacketAfterCut)
              {
                if (NavPtr > 0)
                {
                  WaitForIFrame = TRUE;
                  FirstRecordAfterCut = TRUE;
                }
                if (LastNavPTS && ((int)(SEIPTS - LastNavPTS) > 45000))
                  PTSJump += (SEIPTS - LastNavPTS);
                FirstPacketAfterCut = FALSE;
              }
            }            
            break;
          };

          case NAL_SPS:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_SPS");
            #endif

            SPS = HeaderFound;
            SPSLen = 0;
            PPSCount = 0;
            break;
          };

          case NAL_PPS:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_PPS");
            #endif

            //We need to remember every PPS because the upcoming slice will point to one of them
            if(PPSCount < 10)
            {
              PPS[PPSCount].Offset = HeaderFound;
              PPS[PPSCount].Len = 0;
              PPSCount++;
              GetPPSID = TRUE;
              GolombFull = 0;
            }

            if (!navHD.FrameType)
              navHD.FrameType = 1;
            break;
          };

          case NAL_SLICE:
          case NAL_SLICE + 1:
          case NAL_SLICE + 2:
          case NAL_SLICE + 3:
          case NAL_SLICE_IDR:
          {
            byte NALRefIdc = (Packet->Data[Ptr] >> 5) & 3;

            #if DEBUGLOG != 0
              strcpy(s, "NAL_SLICE");
            #endif
            GetSlicePPSID = TRUE;
            GolombFull = 0;
            if((navHD.FrameType == 3) && (NALRefIdc >= 2))
              navHD.FrameType = 2;
            break;
          }

          case NAL_FILLER_DATA:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_FILLER_DATA");
            #endif
            AUD = HeaderFound;
            break;
          }

          case NAL_AU_DELIMITER:
          {
            int k;

            #if DEBUGLOG != 0
              if (NALType == NAL_AU_DELIMITER)
              {
                strcpy(s, "NAL_AU_DELIMITER (");

                switch(PSBuffer[Ptr+4] >> 5)
                {
                  case 0: strcat(s, "I)"); break;
                  case 1: strcat(s, "I, P)"); break;
                  case 2: strcat(s, "I, P, B)"); break;
                  case 3: strcat(s, "SI)"); break;
                  case 4: strcat(s, "SI, SP)"); break;
                  case 5: strcat(s, "I, SI)"); break;
                  case 6: strcat(s, "I, SI, P, SP)"); break;
                  case 7: strcat(s, "I, SI, P, SP, B)"); break;
                }
              }
            #endif


            if((SEI != 0) /*&& (SPS != 0) && (PPSCount != 0)*/)
            {
              if (WaitForIFrame) {FrameOffset = 0; FrameCtr = 0;}
//              if (WaitForPFrame && NavPtr < 25) FrameCtr = 1;

              // MEINE VARIANTE
              if((navHD.FrameType != 1) && IFramePTS && ((int)(SEIPTS-IFramePTS) >= 0))
              {
                FrameOffset = 0;  // P-Frame
                IFramePTS = 0;
              }

              if (navHD.FrameIndex == 0)
                navHD.FrameIndex  = FrameCtr + FrameOffset;

              if(navHD.FrameType == 1)  // I-Frame
              {
                FrameOffset     = FrameCtr + ((/*SystemType==ST_TMSC ||*/ !FrameCtr) ? 0 : 1);  // Position des I-Frames in der aktuellen Z�hlung (diese geht weiter, bis P kommt) - SRP-2401 z�hlt nach dem I-Frame + 2 (neuer CRP auch)
                FrameCtr        = 0;        // z�hlt die Distanz zum letzten I-Frame
                IFramePTS       = SEIPTS;
                if(FirstSEIPTS == 0 /*|| (PTS < FirstSEIPTS && FirstSEIPTS-PTS < 10000)*/)
                {
                  FirstSEIPTS = SEIPTS;
                  FirstNavPTS = SEIPTS;
                }
                else
                  if(!FirstNavPTSOK)  FirstNavPTSOK = TRUE;
              }
              else if (!FirstNavPTSOK && !WaitForPFrame)
                if(FirstNavPTS && ((int)(SEIPTS - FirstNavPTS) < 0))
                  FirstNavPTS = SEIPTS;

//              FrameCtr++;


              // VARIANTE VON jkIT
/*              // jeder beliebige Frame-Typ
              if (navHD.FrameIndex == 0)
              {
                // CounterStack von oben durchgehen, bis ein PTS gefunden wird, der <= dem aktuellen ist
                for (i = COUNTSTACKSIZE; i > 0; i--)
                {
                  p = (LastIFrame + i) % COUNTSTACKSIZE;          // Schleife l�uft von LastIFrame r�ckw�rts, max. Stackgr��e
                  navHD.FrameIndex += CounterStack[p].FrameCtr;   // alle Counter auf dem Weg zum passenden I-Frame addieren
                  if ((int)(SEIPTS - CounterStack[p].PTS) > 0)    // �berlaufsichere Pr�fung, ob PTS > Counter-PTS
                    break;
                }
              }

              if(FrameType == 1)  // I-Frame
              {
                if (SystemType != ST_TMSC && CounterStack[LastIFrame].PTS != 0)
                  CounterStack[LastIFrame].FrameCtr++;  // SRP-2401 z�hlt nach dem I-Frame + 2

                // neuen I-Frame in den CounterStack einf�gen, an Position LastIFrame+1
                LastIFrame = (LastIFrame + 1) % COUNTSTACKSIZE;
                CounterStack[LastIFrame].PTS = SEIPTS;
                CounterStack[LastIFrame].FrameCtr = 0;
              }

              // aktuellen Frame-Counter erh�hen
              CounterStack[LastIFrame].FrameCtr++;  */


              for(k = 0; k < PPSCount; k++)
                if(PPS[k].ID == SlicePPSID)
                {
                  navHD.SEIPPS = (dword) (SEI - PPS[k].Offset);
                  break;
                }

              navHD.MPEGType = 0x30;

              navHD.PPSLen = 8;
              if(PPSCount != 0)
              {
                for(k = 0; k < PPSCount; k++)
                  if(PPS[k].ID == SlicePPSID)
                  {
                    navHD.PPSLen = PPS[k].Len;
                    break;
                  }
              }

              navHD.SEIOffsetHigh = SEI >> 32;
              navHD.SEIOffsetLow = SEI & 0x00000000ffffffffll;
              navHD.SEIPTS = SEIPTS;

              navHD.SEISPS = (dword) (SEI - SPS);
              navHD.SPSLen = SPSLen;
              navHD.IFrame = 0;

              if (AUD == 0 || NavPtr == 0 /*|| SystemType == ST_TMSC*/)  // CRP-2401 z�hlt Filler-NALU nicht als Delimiter? (2015Mar26 schon!)
                AUD = HeaderFound;
              if (AUD >= SEI)
                navHD.NextAUD = (dword) (AUD - SEI);

              if((SEI != 0) && (SPS != 0) && (PPSCount != 0))
              {
                if (!fNavIn && !navHD.Timems && FirstSEIPTS)
                  navHD.Timems = ((int)(SEIPTS - FirstSEIPTS) / 45);

                // nach Schnittpunkt die fehlende Zeit von Timems abziehen
                navHD.Timems -= TimeOffset;
                if (FirstRecordAfterCut)
                {
                  int TimeOffset_new;
                  navHD.Timems += TimeOffset;
//                  if(navHD.Timems < 1000) navHD.Timems = 0;
                  TimeOffset_new = (navHD.Timems >= 1000 ? navHD.Timems : 0) - LastTimems;
                  if(TimeOffset_new + 1000 < TimeOffset || TimeOffset_new > TimeOffset + 1000) TimeOffset = TimeOffset_new;
                  if(abs(TimeOffset) < 1000)
                    TimeOffset = 0;
                  navHD.Timems -= TimeOffset;
                  FirstRecordAfterCut = FALSE;
                }
                else if (abs((int)(navHD.Timems - LastTimems)) >= 3000)
                {
                  navHD.Timems += TimeOffset;
                  TimeOffset = navHD.Timems - LastTimems;
                  if(abs(TimeOffset) >= 1000)  // TimeOffset = 0;
                    navHD.Timems -= TimeOffset;
                }
                if((int)navHD.Timems < 0) navHD.Timems = 0;

                if (pOutNextTimeStamp)
                {
                  *pOutNextTimeStamp = navHD.Timems;
                  pOutNextTimeStamp = NULL;
                }

                if (WaitForPFrame && navHD.FrameType <= 2)
                  WaitForPFrame = FALSE;

                if (WaitForIFrame && navHD.FrameType == 1) {
                  WaitForIFrame = FALSE;  WaitForPFrame = TRUE; }
      
                if (!WaitForIFrame && (!WaitForPFrame || navHD.FrameType<=2))
                {
                  // sicherstellen, dass Timems monoton ansteigt
                  /* if( ((int)(navHD.Timems - LastTimems)) >= 0) */ LastTimems = navHD.Timems;
                  /* else  navHD.Timems = LastTimems; */
                  if (FirstNavPTSOK)
                    if(!LastNavPTS || ((int)(navHD.SEIPTS - LastNavPTS) > 0))  LastNavPTS = navHD.SEIPTS;
#ifdef _DEBUG
{
  long long RefPictureHeaderOffset = dbg_NavPictureHeaderOffset - dbg_SEIPositionOffset;
  if (fNavIn && (long long)SEI != RefPictureHeaderOffset && dbg_CurrentPosition/PACKETSIZE != dbg_SEIFound)
    fprintf(stderr, "DEBUG: Problem! pos=%lld, offset=%lld, Orig-Nav-PHOffset=%lld, Rebuilt-Nav-PHOffset=%lld, Differenz= %lld * %hhu + %lld\n", dbg_CurrentPosition, dbg_SEIPositionOffset, dbg_NavPictureHeaderOffset, SEI, ((long long int)(SEI-RefPictureHeaderOffset))/PACKETSIZE, PACKETSIZE, ((long long int)(SEI-RefPictureHeaderOffset))%PACKETSIZE);
//printf("%lld: fwrite: SEI=%lld, nav=%lld\n", dbg_CurrentPosition/PACKETSIZE, SEI, NavPictureHeaderOffset);
}
#endif
                  // Write the nav record
                  if (fNavOut && !fwrite(&navHD, sizeof(tnavHD), 1, fNavOut))
                  {
                    printf("ProcessNavFile(): Error writing to nav file!\n");
                    fclose(fNavOut); fNavOut = NULL;
                  }
                  NrFrames++;
                  FrameCtr++;
                  NavPtr++;
                }

                SEI = 0;
                AUD = 0;
//memset(&navHD, 0, sizeof(tnavHD));
//navHD.SEIOffsetHigh = 0; navHD.SEIOffsetLow = 0;
                navHD.Timems = 0;
                navHD.FrameIndex = 0;
                GetPrimPicType = TRUE;
              }
            }
            break;
          }

          #if DEBUGLOG != 0
            default:
              printf("Unexpected NAL 0x%2.2x\n", NALType);
          #endif
        }

        #if DEBUGLOG != 0
          if(NALRefIdc != 0) snprintf(&s[strlen(s)], sizeof(s)-strlen(s), " (NALRefIdc=%hu)", NALRefIdc);
          printf("%8.8llx: %s\n", HeaderFound, s);
        #endif
      }
    }

    HeaderFound = 0;
    Ptr++;
  }
  TRACEEXIT;
}

static void SDNAV_ParsePacket(tTSPacket *Packet, long long FilePositionOfPacket)
{
  byte                  PayloadStart, Ptr;
  byte                  FrameType;

  TRACEENTER;

  // PROCESS THE TS-PACKET

  // Valid TS packet and payload available?
  if((Packet->SyncByte != 'G') || !Packet->Payload_Exists)
  {
    TRACEEXIT;
    return;
  }

  if (Packet->Payload_Unit_Start)
  {
    PosFirstNull = 0; PosSecondNull = 0; HeaderFound = 0; PTSBufFill = 0;
  }

  // Adaptation field available?
  if(Packet->Adapt_Field_Exists)
    PayloadStart = Packet->Data[0] + 1;
  else
    PayloadStart = 0;

  // PROCESS THE PAYLOAD

  // Search for start codes in the packet. Continue from last packet
  Ptr = PayloadStart;
  while(Ptr < 184)
  {
    // Check for Header
    if (!HeaderFound)
    {
      if (PosFirstNull && PosSecondNull && (Packet->Data[Ptr] == 1))
        HeaderFound = PosFirstNull;

      if (Packet->Data[Ptr] == 0)
      {
        PosFirstNull = PosSecondNull;
        PosSecondNull = FilePositionOfPacket + 4 + Ptr;
      }
      else
        PosSecondNull = 0;

      if (HeaderFound) { Ptr++; continue; }
    }

    // Process PTS
    if (PTSBufFill)
    {
      PTSBuffer[PTSBufFill++] = Packet->Data[Ptr];
      if (PTSBufFill > 10)
      {
        GetPTS2(PTSBuffer, &PTS, NULL);
        PTSBufFill = 0;
      }
    }

    // Process a header
    if (HeaderFound)
    {
      if (PictHeader)
      {
        // noch ein Byte skippen f�r FrameType
        HeaderFound = 0;
        Ptr++;
        continue;
      }

      if (Packet->Data[Ptr] == 0xB3)
      {
        // Sequence Header
        CurrentSeqHeader = HeaderFound;
        Ptr++;
        continue;
      }
      else if (Packet->Data[Ptr] == 0x00)
      {
        // Picture Header
//        if (CurrentSeqHeader > 0)
        {
          PictHeader = HeaderFound;
          Ptr++;  // n�chstes Byte f�r FrameType
          continue;
        }
      }

      // PTS-Puffer starten
      PTSBuffer[0] = Packet->Data[Ptr];
      PTSBufFill = 1;
    }

    // Process Picture Header
    if (PictHeader)
    {
      // OLD NAV RECORD
      navSD.NextPH = (dword) (((CurrentSeqHeader>LastPictureHeader) ? CurrentSeqHeader : PictHeader) - LastPictureHeader);

      if (!fNavIn && !navSD.Timems && FirstPTS)
        navSD.Timems = ((int)(navSD.PTS2 - FirstPTS) / 45);

      // nach Schnittpunkt die fehlende Zeit von Timems abziehen
      navSD.Timems -= TimeOffset;
      if (FirstRecordAfterCut)
      {
        int TimeOffset_new;
        navSD.Timems += TimeOffset;
//        if(navSD.Timems < 1000) navSD.Timems = 0;
        TimeOffset_new = (navSD.Timems >= 1000 ? navSD.Timems : 0) - LastTimems;
        if(TimeOffset_new + 1000 < TimeOffset || TimeOffset_new > TimeOffset + 1000) TimeOffset = TimeOffset_new;
        if(abs(TimeOffset) < 1000)
          TimeOffset = 0;
        navSD.Timems -= TimeOffset;
        FirstRecordAfterCut = FALSE;
      }
      else if (abs((int)(navSD.Timems - LastTimems)) >= 3000)
      {
        navSD.Timems += TimeOffset;
        TimeOffset = navSD.Timems - LastTimems;
        if(abs(TimeOffset) >= 1000)  // TimeOffset = 0;
          navSD.Timems -= TimeOffset;
      }
      if((int)navSD.Timems < 0) navSD.Timems = 0;

      if (pOutNextTimeStamp)
      {
        *pOutNextTimeStamp = navSD.Timems;
        pOutNextTimeStamp = NULL;
      }

      if (WaitForPFrame && navSD.FrameType <= 2)
        WaitForPFrame = FALSE;

      if (WaitForIFrame && navSD.FrameType == 1) {
        WaitForIFrame = FALSE;  WaitForPFrame = TRUE; }

      if(NavPtr > 0 && !WaitForIFrame && (!WaitForPFrame || navSD.FrameType<=2))
      {
        // sicherstellen, dass Timems monoton ansteigt
        /* if( ((int)(navSD.Timems - LastTimems)) >= 0) */ LastTimems = navSD.Timems;
        /* else  navSD.Timems = LastTimems; */
        if (FirstNavPTSOK)
          if(!LastNavPTS || (int)(navSD.PTS2 - LastNavPTS) > 0)  LastNavPTS = navSD.PTS2;
#ifdef _DEBUG
{
  unsigned long long RefPictureHeaderOffset = dbg_NavPictureHeaderOffset - dbg_HeaderPosOffset;
  if (fNavIn && LastPictureHeader != RefPictureHeaderOffset)
    fprintf(stderr, "DEBUG: Problem! pos=%lld, offset=%lld, Orig-Nav-PHOffset=%lld, Rebuilt-Nav-PHOffset=%lld, Differenz= %lld * %hhu + %lld\n", dbg_CurrentPosition, dbg_HeaderPosOffset, dbg_NavPictureHeaderOffset, LastPictureHeader, ((long long int)(LastPictureHeader-RefPictureHeaderOffset))/PACKETSIZE, PACKETSIZE, ((long long int)(LastPictureHeader-RefPictureHeaderOffset))%PACKETSIZE);
}
#endif
        // Write the nav record
        if (fNavOut && !fwrite(&navSD, sizeof(tnavSD), 1, fNavOut))
        {
          printf("ProcessNavFile(): Error writing to nav file!\n");
          fclose(fNavOut); fNavOut = NULL;
        }
        NrFrames++;
      }

      if (FirstPacketAfterCut)
      {
        WaitForIFrame = TRUE;
        FirstRecordAfterCut = TRUE;
        FirstPacketAfterCut = FALSE;
        if (LastNavPTS && ((int)(PTS - LastNavPTS) > 45000))
          PTSJump += (PTS - LastNavPTS);
      }
      NavPtr++;

      // NEW NAV RECORD
      FrameType = (Packet->Data[Ptr] >> 3) & 0x03;
      if (WaitForIFrame) {FrameOffset = 0; FrameCtr = 0;}
//      if (WaitForPFrame && NavPtr < 25) FrameCtr = 1;

      if(FrameType == 2) FrameOffset = 0;  // P-Frame

      navSD.FrameIndex  = FrameCtr + FrameOffset;

      if(FrameType == 1)  // I-Frame
      {
        FirstSHPHOffset = (word) (PictHeader - CurrentSeqHeader);
        FrameOffset         = FrameCtr;  // Position des I-Frames in der aktuellen Z�hlung (diese geht weiter, bis P kommt)
        FrameCtr            = 0;         // z�hlt die Distanz zum letzten I-Frame
        if((FirstPTS == 0) /*|| (PTS < FirstPTS && FirstPTS-PTS < 10000)*/)
        {
          FirstPTS = PTS;
          FirstNavPTS = PTS;
        }
        else
          if(!FirstNavPTSOK)  FirstNavPTSOK = TRUE;
      }
      else if (!FirstNavPTSOK && !WaitForPFrame)
        if(FirstNavPTS && ((int)(PTS - FirstNavPTS) < 0))
          FirstNavPTS = PTS;

      navSD.SHOffset        = (dword)(PictHeader - CurrentSeqHeader);
      navSD.FrameType       = FrameType;
      navSD.MPEGType        = 0x20;
      navSD.iFrameSeqOffset = FirstSHPHOffset;
      //Some magic for the AUS pvrs
      navSD.PHOffset        = (dword)PictHeader;
      navSD.PHOffsetHigh    = (dword)(PictHeader >> 32);
      navSD.PTS2            = PTS;
      navSD.Timems          = 0;

      navSD.Zero5 = 0;
      navSD.NextPH = 0;

      LastPictureHeader = PictHeader;
dbg_HeaderPosOffset = dbg_PositionOffset - (Ptr-PayloadStart-3 <= 1 ? dbg_DelBytesSinceLastVid : 0);  // Ptr-PayloadStart in {3,4} -> nur dann ist HeaderFound falsch
      PictHeader = 0;
      if(NavPtr > 0 && (!WaitForIFrame || navSD.FrameType==1) && (!WaitForPFrame || navSD.FrameType<=2))
        FrameCtr++;
    }

    HeaderFound = 0;
    Ptr++;
  }
  TRACEEXIT;
  return;
}

void ProcessNavFile(tTSPacket *Packet, const long long CurrentPosition, const long long PositionOffset)
{
  TRACEENTER;
  if (FirstRun && fNavIn)
  {
    if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
      NextPictureHeaderOffset = ((long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
    }

//    memset(CounterStack, 0, COUNTSTACKSIZE * sizeof(tFrameCtr));
    FirstRun = FALSE;
  }

  if (fNavOut)
  {
dbg_CurrentPosition = CurrentPosition;
dbg_PositionOffset = PositionOffset;

    if (isHDVideo)
      HDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);
    else
      SDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);

    if (fNavIn && !FirstPacketAfterCut)
    {
      if (isHDVideo && !navHD.Timems && NrFrames > 0)
        navHD.Timems = curSDNavRec->Timems;
      else if (!navSD.Timems && NrFrames > 0)
        navSD.Timems = curSDNavRec->Timems;

      while (fNavIn && !FirstPacketAfterCut && (CurrentPosition + 188 > NextPictureHeaderOffset))
      {
dbg_NavPictureHeaderOffset = NextPictureHeaderOffset;

        if (isHDVideo)
          navHD.Timems = curSDNavRec->Timems;
//          navHD.FrameIndex = ((tnavHD*)NavBuffer)->FrameIndex;
        else
          navSD.Timems = curSDNavRec->Timems;
//          navSD.FrameIndex = curSDNavRec->FrameIndex;

        if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
          NextPictureHeaderOffset = ((long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
        else
        {
          break;  // fclose(fNavIn); fNavIn = NULL;
        }
      }
    }
  }
  TRACEEXIT;
}


void QuickNavProcess(const long long CurrentPosition, const long long PositionOffset)
{
  TRACEENTER;
  if (FirstRun && fNavIn)
  {
    if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
      NextPictureHeaderOffset = ((long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
//      if(fNavOut) fclose(fNavOut); fNavOut = NULL;
    }
    FirstRun = FALSE;
  }

  if (FirstPacketAfterCut)
  {
    while (fNavIn && (NextPictureHeaderOffset <= CurrentPosition))
    {
      // n�chsten Record einlesen
      if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
        NextPictureHeaderOffset = ((long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
      else
      {
        fclose(fNavIn); fNavIn = NULL;
//        if(fNavOut) fclose(fNavOut); fNavOut = NULL;
      }
    }

    // nach Schnittpunkt die fehlende Zeit von Timems abziehen
    if (LastTimems > 0 || curSDNavRec->Timems > 3000)
    {
      int TimeOffset_new = (curSDNavRec->Timems > 1000 ? curSDNavRec->Timems : 0) - LastTimems;
      if(TimeOffset_new < TimeOffset || TimeOffset_new > TimeOffset + 1000) TimeOffset = TimeOffset_new;
    }

    if (LastNavPTS && ((int)(curSDNavRec->PTS2 - LastNavPTS) > 45000))
      PTSJump += (curSDNavRec->PTS2 - LastNavPTS);

    WaitForIFrame = TRUE;
    FirstPacketAfterCut = FALSE;
  }
  else
  {
    while (fNavIn && (NextPictureHeaderOffset <= CurrentPosition))
    {
      // Position anpassen
      NextPictureHeaderOffset   -= PositionOffset;
      curSDNavRec->PHOffset      = (dword)(NextPictureHeaderOffset & 0xffffffff);
      curSDNavRec->PHOffsetHigh  = (dword)(NextPictureHeaderOffset >> 32);

      // Zeit anpassen
      curSDNavRec->Timems -= TimeOffset;
//      if((int)curSDNavRec->Timems < 0) curSDNavRec->Timems = 0;
      if (pOutNextTimeStamp)
      {
        *pOutNextTimeStamp = curSDNavRec->Timems;
        pOutNextTimeStamp = NULL;
      }
      LastTimems = curSDNavRec->Timems;

      // I-Frame pr�fen
      if (WaitForPFrame && curSDNavRec->FrameType <= 2)
        WaitForPFrame = FALSE;

      if (WaitForIFrame && (curSDNavRec->FrameType == 1)) {
        WaitForIFrame = FALSE; WaitForPFrame = TRUE; }

      // FirstPTS ermitteln
      if (curSDNavRec->FrameType == 1)
      {
        if (FirstNavPTS == 0)
          FirstNavPTS = curSDNavRec->PTS2;
        else
          if(!FirstNavPTSOK)  FirstNavPTSOK = TRUE;
      }
      else if (!FirstNavPTSOK && !WaitForPFrame)
        if(FirstNavPTS && ((int)(curSDNavRec->PTS2 - FirstNavPTS) < 0))
          FirstNavPTS = curSDNavRec->PTS2;

      if (!WaitForIFrame && (/*isHDVideo ||*/ !WaitForPFrame || curSDNavRec->FrameType<=2))
      {
        if (FirstNavPTSOK)
          if(!LastNavPTS || (int)(curSDNavRec->PTS2 - LastNavPTS) > 0)  LastNavPTS =  curSDNavRec->PTS2;

        // Record schreiben
        if (fNavOut && !fwrite(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavOut))
        {
          printf("ProcessNavFile(): Error writing to nav file!\n");
          fclose(fNavIn); fNavIn = NULL;
          fclose(fNavOut); fNavOut = NULL;
          break;
        }
        NrFrames++;
      }

      // n�chsten Record einlesen
      if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
        NextPictureHeaderOffset = ((long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
      else
      {
        fclose(fNavIn); fNavIn = NULL;
//        if(fNavOut) fclose(fNavOut); fNavOut = NULL;
      }
    }
  }
  TRACEEXIT;
}

bool LoadNavFileIn(const char* AbsInNav)
{
  unsigned long long    NavSize;
  dword                 start = 0, skippedFrames = 0, NrFrames = 0, FirstPTS = 0, LastPTS = 0, FirstPTSms, LastPTSms, TimemsStart = 0, TimemsEnd = 0;
  int                   dPTS, DurationMS;
  byte                  FrameType = 0;
  byte                  FirstTimeOK = FALSE;
  tnavSD                navSD;
  TRACEENTER;

  fNavIn = fopen(AbsInNav, "rb");
  if (fNavIn)
  {
//    setvbuf(fNavIn, NULL, _IOFBF, BUFSIZE);

    // Versuche, nav-Dateien aus Timeshift-Aufnahmen zu unterst�tzen ***experimentell***
    dword FirstDword = 0;
    if (fread(&FirstDword, 4, 1, fNavIn)
      && (FirstDword == 0x72767062))  // 'bpvr'
    {  
      start = 1056;
      fseek(fNavIn, start, SEEK_SET);
    }
    else
      rewind(fNavIn);

    // erstes I-Frame der nav ermitteln (bzw. danach decodierte fr�here B-Frames)
    while (TRUE)
    {
      if (!fread(&navSD, sizeof(tnavSD), 1, fNavIn)) break;
      if (!FirstTimeOK || (int)(navSD.PTS2 - FirstPTS) < 0)
      {
        TimemsStart = navSD.Timems;
        FirstPTS = navSD.PTS2;
      }
      if (navSD.FrameType == 1)
      {
        if(!FirstTimeOK) FirstTimeOK = TRUE;
        else break;
      }
      if(!FirstTimeOK) skippedFrames++;
      if(isHDVideo) fseek(fNavIn, sizeof(tnavSD), SEEK_CUR);
    }

    // letztes Frame der nav ermitteln
    FrameType = 0x0f;
    fseek(fNavIn, -(int)(isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD)), SEEK_END);
    while (!TimemsEnd || FrameType > 1)
    {
      if (fread(&navSD, sizeof(tnavSD), 1, fNavIn))
      {
        if (FrameType == 0x0f)
        {
          NavSize = ftell(fNavIn) + (isHDVideo ? sizeof(tnavSD) : 0);
          NrFrames = (dword)((NavSize-start) / (isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD))) - skippedFrames;
        }
        FrameType = navSD.FrameType;
        if (!TimemsEnd || (int)(navSD.PTS2 - LastPTS) > 0)
        {
          TimemsEnd = navSD.Timems;
          LastPTS = navSD.PTS2;
        }
      }
      else break;
      if (!TimemsEnd || FrameType >= 2)
        fseek(fNavIn, -(int)(isHDVideo ? sizeof(tnavHD) + sizeof(tnavSD) : 2*sizeof(tnavSD)), SEEK_CUR);
    }  

//    HDD_GetFileSize(AbsInNav, &NavSize);
    fseek(fNavIn, start, SEEK_SET);

    FirstPTSms = FirstPTS / 45;
    LastPTSms = LastPTS / 45;
    dPTS = DeltaPCR(FirstPTS, LastPTS) / 45;
    DurationMS = TimemsEnd - TimemsStart;
printf("  NAV: FirstPTS = %u (%01u:%02u:%02u,%03u), Last: %u (%01u:%02u:%02u,%03u)\n", FirstPTS, (FirstPTSms/3600000), (FirstPTSms/60000 % 60), (FirstPTSms/1000 % 60), (FirstPTSms % 1000), LastPTS, (LastPTSms/3600000), (LastPTSms/60000 % 60), (LastPTSms/1000 % 60), (LastPTSms % 1000));
printf("  NAV: Duration = %02d:%02u:%02u,%03u (PTS: %02d:%02u:%02u,%03u)\n", DurationMS/3600000, abs(DurationMS/60000) % 60, abs(DurationMS/1000) % 60, abs(DurationMS) % 1000, dPTS/3600000, abs(dPTS/60000) % 60, abs(dPTS/1000) % 60, abs(dPTS) % 1000);
printf("  NAV: Frames   = %d (%.1f fps)\n", NrFrames, NrFrames / ((double)DurationMS / 1000));
    NavFrames += NrFrames;
    NavDurationMS += DurationMS;
  }

  NextPictureHeaderOffset = 0;
  FirstRun = TRUE;

  TRACEEXIT;
  return (fNavIn != NULL);
}
bool LoadNavFileOut(const char* AbsOutNav)
{
  TRACEENTER;

  fNavOut = fopen(AbsOutNav, ((DoMerge==1) ? "r+b" : "wb"));
  if (fNavOut)
  {
//  setvbuf(fNavOut, NULL, _IOFBF, BUFSIZE);
  }
  else
    printf("WARNING: Cannot create nav file %s.\n", AbsOutNav);

  TRACEEXIT;
  return (fNavOut != NULL);
}

void GoToEndOfNav(FILE* fNav)
{
  tnavSD navRec[2];
  TRACEENTER;

  if (fNav == NULL) fNav = fNavOut;
  if (fNav && fseek(fNav, (isHDVideo ? -(int)sizeof(tnavHD) : -(int)sizeof(tnavSD)), SEEK_END) == 0)
    if (fread(&navRec, (isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD)), 1, fNav))
      LastTimems = navRec[0].Timems;
//      TimeOffset = 0 - LastTimems;
  fseek(fNav, 0, SEEK_END);

  TRACEEXIT;
}

void CloseNavFileIn(void)
{
  TRACEENTER;

  if (fNavIn) fclose(fNavIn);
  fNavIn = NULL;

  TRACEEXIT;
}
bool CloseNavFileOut(void)
{
  bool                  ret = TRUE;

  TRACEENTER;
  if (fNavOut && !isHDVideo)
  {
    if (NavPtr > 0)
    {
      if (FirstPTS && !navSD.Timems)
        navSD.Timems = ((int)(navSD.PTS2 - FirstPTS) / 45);
      navSD.Timems -= TimeOffset;

      // Timems soll monoton ansteigen
      if(fNavIn && ((int)(navSD.Timems - LastTimems)) < 0)
        navSD.Timems = LastTimems;
      if (pOutNextTimeStamp)
        *pOutNextTimeStamp = navSD.Timems;

      if(!WaitForIFrame && (!WaitForPFrame || navSD.FrameType<=2))
      {
        if(fNavOut) fwrite(&navSD, sizeof(tnavSD), 1, fNavOut);
        if (FirstNavPTSOK && (!LastNavPTS || ((int)(navSD.PTS2 - LastNavPTS) > 0)))
          LastNavPTS = navSD.PTS2;
        NrFrames++;
      }
    }
  }
  else if (fNavOut && isHDVideo)
  {
//    unsigned long long DummyPack = 0x0901000010000047;
    tTSPacket DummyPack;
    memset(&DummyPack, 0, sizeof(tTSPacket));
    DummyPack.SyncByte = 'G';
    DummyPack.Payload_Exists = 1;
    DummyPack.Data[2] = 1;
    DummyPack.Data[3] = NAL_AU_DELIMITER;
//    if(fNavIn && !navHD.Timems) navHD.Timems = LastTimems;

    if (SEI && SPS && navHD.FrameType)
      HDNAV_ParsePacket(&DummyPack, dbg_CurrentPosition-dbg_PositionOffset);
  }

  if (NrFrames > 0)
    printf("\nNewNav: %u of %u frames written.\n", NrFrames, NavFrames);
  if(FirstNavPTSOK && LastNavPTS)
  {
    TYPE_RecHeader_TMSS *RecInf = (TYPE_RecHeader_TMSS*)InfBuffer;
    dword FirstPTSms = (dword)(FirstNavPTS/45);
    dword LastPTSms = (dword)(LastNavPTS/45);
    int dPTS = DeltaPCR(FirstNavPTS, (LastNavPTS - PTSJump)) / 45;
    RecInf->RecHeaderInfo.DurationMin = (word)(dPTS / 60000);
    RecInf->RecHeaderInfo.DurationSec = (word)abs((dPTS / 1000) % 60);
printf("NewNav: FirstPTS = %u (%01u:%02u:%02u,%03u), Last: %u (%01u:%02u:%02u,%03u)\n", FirstNavPTS, (FirstPTSms/3600000), (FirstPTSms/60000 % 60), (FirstPTSms/1000 % 60), (FirstPTSms % 1000), LastNavPTS, (LastPTSms/3600000), (LastPTSms/60000 % 60), (LastPTSms/1000 % 60), (LastPTSms % 1000));
printf("NewNav: Duration = %01d:%02u:%02u,%03u", dPTS / 3600000, abs(dPTS / 60000) % 60, abs(dPTS / 1000) % 60, abs(dPTS) % 1000);

if (((dPTS = PTSJump / 45)) != 0)
  printf(" (time jumps %02d:%02u,%03u)\n", dPTS / 60000, abs(dPTS / 1000) % 60, abs(dPTS) % 1000);
else printf("\n");
  }
  printf("\n");

  if (fNavOut)
    ret = (/*fflush(fNavOut) == 0 &&*/ fclose(fNavOut) == 0);
  fNavOut = NULL;

  TRACEEXIT;
  return ret;
}


tTimeStamp2* NavLoad(const char *AbsInRec, int *const OutNrTimeStamps, byte PacketSize)
{
  char                  AbsFileName[FBLIB_DIR_SIZE];
  FILE                 *fNav = NULL;
  tnavSD                NavBuffer[2], *CurNavRec = &NavBuffer[0];
  tTimeStamp2          *TimeStampBuffer = NULL;
  tTimeStamp2          *TimeStamps = NULL;
  int                   NavRecordsNr, NrTimeStamps = 0;
//  dword                 FirstPTS = 0xffffffff;  // FirstTime = 0xffffffff, LastTime = 0xffffffff;
  dword                 FirstDword = 0;
  long long             AbsPos;
  unsigned long long    NavSize = 0;

  TRACEENTER;
  if(OutNrTimeStamps) *OutNrTimeStamps = 0;

  // Open the nav file
  snprintf(AbsFileName, sizeof(AbsFileName), "%s.nav", AbsInRec);
  fNav = fopen(AbsFileName, "rb");
  if(!fNav)
  {
//    printf("  Could not open nav file.\n");
    TRACEEXIT;
    return(NULL);
  }

  // Reserve a (temporary) buffer to hold the entire file
  HDD_GetFileSize(AbsFileName, &NavSize);
  NavRecordsNr = (dword)((NavSize / (sizeof(tnavSD) * ((isHDVideo) ? 2 : 1))) / 4);  // h�chstens jedes 4. Frame ist ein I-Frame (?)

  if (!NavRecordsNr || !((TimeStampBuffer = (tTimeStamp2*) malloc(NavRecordsNr * sizeof(tTimeStamp2)))))
  {
    fclose(fNav);
    printf("  Nav could not be loaded.\n");
    TRACEEXIT;
    return(NULL);
  }

  // Versuche, nav-Dateien aus Timeshift-Aufnahmen zu unterst�tzen ***experimentell***
  if (fread(&FirstDword, 4, 1, fNav)
    && (FirstDword == 0x72767062))  // 'bpvr'
    fseek(fNav, 1056, SEEK_SET);
  else
    rewind(fNav);

  //Count and save all the _different_ time stamps in the .nav
  while (fread(CurNavRec, sizeof(tnavSD) * (isHDVideo ? 2 : 1), 1, fNav) && (NrTimeStamps < NavRecordsNr))
  {
//    if(FirstTime == 0xFFFFFFFF) FirstTime = CurNavRec->Timems;
    if(CurNavRec->FrameType == 1)  // erfasse nur noch I-Frames
    {
      AbsPos = ((long long)(CurNavRec->PHOffsetHigh) << 32) | CurNavRec->PHOffset;
//      if(FirstPTS == 0xffffffff) FirstPTS = CurNavRec->PTS2;
/*      if(CurNavRec->Timems == LastTime)
      {
TAP_PrintNet("Achtung! I-Frame an %llu hat denselben Timestamp wie sein Vorg�nger!\n", AbsPos);
      } */
      TimeStampBuffer[NrTimeStamps].Position  = (AbsPos / PacketSize) * PacketSize - PacketSize;
      TimeStampBuffer[NrTimeStamps].Timems    = CurNavRec->Timems;

/*        if (CurNavRec->Timems >= FirstTime)
        // Timems ist gr��er als FirstTime -> kein �berlauf
        TimeStampBuffer[*NrTimeStamps].Timems = CurNavRec->Timems - FirstTime;
      else if (FirstTime - CurNavRec->Timems <= 3000)
        // Timems ist kaum kleiner als FirstTime -> liegt vermutlich am Anfang der Aufnahme
        TimeStampBuffer[*NrTimeStamps].Timems = 0;
      else
        // Timems ist (deutlich) kleiner als FirstTime -> ein �berlauf liegt vor
        TimeStampBuffer[*NrTimeStamps].Timems = (0xffffffff - FirstTime) + CurNavRec->Timems + 1;
*/
      NrTimeStamps++;
//      LastTime = CurNavRec->Timems;
    }
  }

  // Free the nav-Buffer and close the file
  fclose(fNav);

/*  // Reserve a new buffer of the correct size to hold only the different time stamps
  TimeStamps = (tTimeStamp2*) malloc(NrTimeStamps * sizeof(tTimeStamp2));
  if(!TimeStamps)
  {
    free(TimeStampBuffer);
    printf("  Not enough memory to copy timestamps.");
    TRACEEXIT;
    return(NULL);
  }

  // Copy the time stamps to the new array
  memcpy(TimeStamps, TimeStampBuffer, NrTimeStamps * sizeof(tTimeStamp2));  
  free(TimeStampBuffer);  */

  if (NrTimeStamps > 0)
  {
    TimeStamps = (tTimeStamp2*) realloc(TimeStampBuffer, NrTimeStamps * sizeof(tTimeStamp2));
    if(!TimeStamps) TimeStamps = TimeStampBuffer;
  }
  if(OutNrTimeStamps) *OutNrTimeStamps = NrTimeStamps;

  TRACEEXIT;
  return(TimeStamps);
}

dword NavGetPosTimeStamp(tTimeStamp2 TimeStamps[], int NrTimeStamps, long long FilePosition)
{
  tTimeStamp2 *LastTimeStamp = TimeStamps;

  TRACEENTER;
  if (TimeStamps)
  {
    // Search the TimeStamp-Array in forward direction
    while ((LastTimeStamp->Position < FilePosition) && (LastTimeStamp < TimeStamps + NrTimeStamps-1))
      LastTimeStamp++;
    if ((LastTimeStamp > TimeStamps) && (LastTimeStamp->Position > FilePosition))
      LastTimeStamp--;

    TRACEEXIT;
    return LastTimeStamp->Timems;  // (LastTimeStamp->Position <= FilePosition) ? LastTimeStamp->Timems : 0;
  }
  else
  {
    TRACEEXIT;
    return ((dword) ((float)FilePosition / RecFileSize) * InfDuration * 1000);
  }
}
