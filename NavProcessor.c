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
#include "NavProcessor.h"
#include "RecStrip.h"

//#define DEBUGLOG 1
#define COUNTSTACKSIZE 8


// Globale Variablen
static FILE            *fNavIn = NULL, *fNavOut = NULL;
static byte             FrameType;
static unsigned long long PosFirstNull = 0, PosSecondNull = 0, HeaderFound = 0;
static dword            PTS = 0, LastTimems = 0;
static byte             PTSBuffer[16];
static int              PTSBufFill = 0;  // 0: keinen PTS suchen, 1..15 Puffer-F�llstand, bei 16 PTS auslesen und zur�cksetzen

//HDNAV
static tnavHD           navHD;
static tPPS             PPS[10];
static unsigned long long SEI = 0, SPS = 0, AUD = 0;
static bool             GetPPSID = FALSE, GetSlicePPSID = FALSE;
static int              PPSCount = 0;
static byte             SlicePPSID = 0;
static dword            FirstSEIPTS = 0, SEIPTS = 0, IFramePTS = 0, SPSLen = 0;
static tFrameCtr        CounterStack[COUNTSTACKSIZE];
static int              LastIFrame = 0;

static unsigned long long dbg_CurPictureHeaderOffset = 0, dbg_SEIFound = 0;
static unsigned long long dbg_CurrentPosition = 0, dbg_PositionOffset = 0, dbg_HeaderPosOffset = 0, dbg_SEIPositionOffset = 0;

//SDNAV
static tnavSD           SDNav[2];
static unsigned long long LastPictureHeader = 0;
static unsigned long long CurrentSeqHeader = 0;
static dword            FirstPTS = 0 /*, LastdPTS = 0*/;
static int              NavPtr = 0;
static word             FirstSHPHOffset = 0;
static byte             FrameCtr = 0, FrameOffset = 0;
static bool             PictHeaderFound = FALSE, PictHeaderSkipped = FALSE;


// ----------------------------------------------
// *****  PROCESS NAV FILE  *****
// ----------------------------------------------

/*void HDNAV_Init(void)
{
  SPS = 0;
  PPSCount = 0;
  SEI = 0;
  FirstSEIPTS = 0;
  PTS = 0;
  FrameIndex = 0;
  LastTimems = 0;
}*/

static int get_ue_golomb32(byte *p, byte *StartBit)
{
  int                   leadingZeroBits;
  dword                 d;

  TRACEENTER;
  d = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  if(d == 0)
  {
    TRACEEXIT;
    return 0;
  }

  leadingZeroBits = 0;
  while(((d >> *StartBit) & 1) == 0)
  {
    leadingZeroBits++;
    *StartBit = *StartBit - 1;
  }
  *StartBit = *StartBit - 1;

  d >>= (*StartBit + 1 - leadingZeroBits);
  d &= ((1 << leadingZeroBits) - 1);

  *StartBit = *StartBit - leadingZeroBits;

  TRACEEXIT;
  return (1 << leadingZeroBits) - 1 + d;
}

static bool GetPTS2(byte *Buffer, dword *pPTS, dword *pDTS)
{
  TRACEENTER;
  if((Buffer[0] & 0xf0) == 0xe0)
  {
    //MPEG Video Stream
    //00 00 01 E0 00 00 88 C0 0B 35 BD E9 8A 85 15 BD E9 36 25 FF
    //0000            = PES Packet Length
    //88              = PES Scrambling = no; PES Priority
    //C0              = PTS/DTS
    //0B              = PES Header Data Length
    //35 BD E9 8A 85  = PTS  = 0 1010 1111 0111 1010 0100 0101 0100 0010 = 0AF7A4542
    //15 BD E9 36 25  = DTS  = 0 1010 1011 0111 1010 0001 1011 0001 0010 = 0AB7A1B12
    //FF

    //...1010.XXXXXXXXXXXXXXXXXXXXXXXXXXXXX.
    //.......10111101XXXXXXXXXXXXXXXXXXXXXX.
    //...............1110100.XXXXXXXXXXXXXX.
    //......................10001010XXXXXXX.
    //..............................1000010.

    //Return PTS >> 1 so that we do not need 64 bit variables
    if (pPTS && (Buffer[4] & 0x80))
      *pPTS = ((Buffer[ 6] & 0x0e) << 28) |
              ((Buffer[ 7] & 0xff) << 21) |
              ((Buffer[ 8] & 0xfe) << 13) |
              ((Buffer[ 9] & 0xff) <<  6) |
              ((Buffer[10] & 0xfe) >>  2);

    if (pDTS && (Buffer[4] & 0xC0))
    {
      if (Buffer[7] & 0x40)
        *pDTS = ((Buffer[11] & 0x0e) << 28) |
                ((Buffer[12] & 0xff) << 21) |
                ((Buffer[13] & 0xfe) << 13) |
                ((Buffer[14] & 0xff) <<  6) |
                ((Buffer[15] & 0xfe) >>  2);
      else
        *pDTS = 0;
    }
    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}
static bool GetPTS(byte *Buffer, dword *pPTS, dword *pDTS)
{
  if((Buffer[0] == 0x00) && (Buffer[1] == 0x00) && (Buffer[2] == 0x01))
    return GetPTS2(&Buffer[3], pPTS, pDTS);
}

static bool GetPCR(byte *pBuffer, dword *pPCR)
{
  TRACEENTER;
  if (pPCR && (pBuffer[0] == 0x47) && ((pBuffer[3] & 0x20) != 0) && (pBuffer[4] > 0) && (pBuffer[5] & 0x10))
  {
    //Extract the time out of the PCR bit pattern
    //The PCR is clocked by a 90kHz generator. To convert to milliseconds
    //the 33 bit number can be shifted right and divided by 45
    *pPCR = (dword)((((dword)pBuffer[6] << 24) | (pBuffer[7] << 16) | (pBuffer[8] << 8) | pBuffer[9]) / 45);

    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}

static dword DeltaPCR(dword FirstPCR, dword SecondPCR)
{
  if(FirstPCR <= SecondPCR)
    return (SecondPCR - FirstPCR);
  else
    return (95443718 - FirstPCR + SecondPCR);
}

/*static dword FindSequenceHeaderCode(byte *Buffer)
{
  int                   i;

  TRACEENTER;
  for(i = 0; i < 176; i++)
  {
    if((Buffer[i] == 0x00) && (Buffer[i + 1] == 0x00) && (Buffer[i + 2] == 0x01) && (Buffer[i + 3] == 0xb3))
    {
      TRACEEXIT;
      return i + 8;
    }
  }
  TRACEEXIT;
  return 0;
}

static dword FindPictureHeader(byte *Buffer, byte *pFrameType)
{
  int                   i;

  TRACEENTER;
  for(i = 0; i < 176; i++)
  {
    if((Buffer[i] == 0x00) && (Buffer[i + 1] == 0x00) && (Buffer[i + 2] == 0x01) && (Buffer[i + 3] == 0x00))
    {
      if (pFrameType)
        *pFrameType = (Buffer[i + 5] >> 3) & 0x03;
      TRACEEXIT;
      return i + 8;
    }
  }
  TRACEEXIT;
  return 0;
}*/

bool HDNAV_ParsePacket(tTSPacket *Packet, unsigned long long FilePositionOfPacket)
{
  unsigned long long    PacketOffset;
  byte                  PayloadStart, PayloadSize, Ptr;

  static dword          FirstPCR = 0;
  byte                  NALType, NALRefIdc;
  dword                 PCR;
  bool                  ret = FALSE, SEIFoundInPacket = FALSE;
  byte                  i, p;
  #if DEBUGLOG != 0
    char                  s[80];
  #endif

  TRACEENTER;

  // PROCESS THE TS-PACKET

  // Valid TS packet and payload available?
  if((Packet->SyncByte != 'G') || !Packet->Payload_Exists)
  {
    TRACEEXIT;
    return FALSE;
  }

  if (Packet->Payload_Unit_Start)
  {
    #if DEBUGLOG != 0
      printf("\n%8.8llx: PES Start\n", FilePositionOfPacket);
    #endif
    PosFirstNull = 0; PosSecondNull = 0; HeaderFound = FALSE;
    // alles andere auch zur�cksetzen?
  }

  // Adaptation field available?
  if(Packet->Adapt_Field_Exists)
  {
    // If available, get the PCR
    if(GetPCR((byte*)Packet, &PCR))
    {
      if(FirstPCR == 0) FirstPCR = PCR;
      #if DEBUGLOG != 0
        printf("%8.8llx: PCR = 0x%8.8x, dPCR = 0x%8.8x\n", PrimaryTSOffset, PCR, DeltaPCR(FirstPCR, PCR));
      #endif
    }
    PayloadStart = (Packet->Data[0] + 1);
  }
  else
    PayloadStart = 0;

  PacketOffset = FilePositionOfPacket + 4 + PACKETOFFSET;
  PayloadSize = 184 - min(PayloadStart, 184);


  // PROCESS THE PAYLOAD

  // Search for start codes in the packet. Continue from last packet
  Ptr = PayloadStart;
  while(Ptr < PayloadSize)
  {
    // Check for Header
    if (!HeaderFound)
    {
      if (PosFirstNull && PosSecondNull && (Packet->Data[Ptr] == 1))
      {
        HeaderFound = PosFirstNull;
dbg_HeaderPosOffset = dbg_PositionOffset;
      }
      
      if (Packet->Data[Ptr] == 0)
      {
        PosFirstNull = PosSecondNull;
        PosSecondNull = PacketOffset + Ptr;
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
    if (GetPPSID)
    {
      byte Bit = 31;
      PPS[PPSCount-1].ID = get_ue_golomb32(&Packet->Data[Ptr], &Bit);
      #if DEBUGLOG != 0
        snprintf(&s[strlen(s)], sizeof(s)-strlen(s), " (ID=%d)", PPS[PPSCount-1].ID);
      #endif
      GetPPSID = FALSE;
    }
    if (GetSlicePPSID)
    {
      byte Bit = 31;
      //first_mb_in_slice
      get_ue_golomb32(&Packet->Data[Ptr], &Bit);
      //slice_type
      get_ue_golomb32(&Packet->Data[Ptr], &Bit);
      //pic_parameter_set_id
      SlicePPSID = get_ue_golomb32(&Packet->Data[Ptr], &Bit);

      #if DEBUGLOG != 0
        snprintf(&s[strlen(s)], sizeof(s)-strlen(s), " (PPS ID=%hu)\n", SlicePPSID);
      #endif
      GetSlicePPSID = FALSE;
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
        PTSBuffer[0] = Packet->Data[Ptr];
        PTSBufFill = 1;
      }
      else
      {
        NALRefIdc = (Packet->Data[Ptr] >> 5) & 3;
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
              if(FirstSEIPTS == 0) FirstSEIPTS = PTS;
              SEIFoundInPacket = TRUE;

dbg_SEIPositionOffset = dbg_HeaderPosOffset;
dbg_SEIFound = dbg_CurrentPosition/192;
//printf("%llu: SEI found: %llu\n", dbg_CurrentPosition/192, SEI);
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
            }

            FrameType = 1;
            break;
          };

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
            byte    k;

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


            if((SEI != 0) && (SPS != 0) && (PPSCount != 0))
            {

/*              // MEINE VARIANTE
              if((FrameType != 1) && IFramePTS && ((int)(SEIPTS-IFramePTS) >= 0))
              {
                FrameOffset = 0;  // P-Frame
                IFramePTS = 0;
              }

              if (navHD.FrameIndex == 0)
                navHD.FrameIndex  = FrameCtr + FrameOffset;

              if(FrameType == 1)  // I-Frame
              {
                FrameOffset     = FrameCtr + ((SystemType==ST_TMSC || !FrameCtr) ? 0 : 1);  // Position des I-Frames in der aktuellen Z�hlung (diese geht weiter, bis P kommt) - SRP-2401 z�hlt nach dem I-Frame + 2
                FrameCtr        = 0;        // z�hlt die Distanz zum letzten I-Frame
                IFramePTS       = SEIPTS;
              }
              FrameCtr++;  */


              // VARIANTE VON jkIT
              // jeder beliebige Frame-Typ
              if (navHD.FrameIndex == 0)
              {
                // CounterStack von oben durchgehen, bis ein PTS gefunden wird, der <= dem aktuellen ist
int j = 0;
                for (i = COUNTSTACKSIZE; i > 0; i--)
                {
                  j++;
                  p = (LastIFrame + i) % COUNTSTACKSIZE;          // Schleife l�uft von LastIFrame r�ckw�rts, max. Stackgr��e
                  navHD.FrameIndex += CounterStack[p].FrameCtr;   // alle Counter auf dem Weg zum passenden I-Frame addieren
                  if ((int)(SEIPTS - CounterStack[p].PTS) > 0)    // �berlaufsichere Pr�fung, ob PTS > Counter-PTS
                    break;
                }
if (j > 2)
  printf("j=%d\n", j);
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
              CounterStack[LastIFrame].FrameCtr++;




//              memset(&navHD, 0, sizeof(tnavHD));

              for(k = 0; k < PPSCount; k++)
                if(PPS[k].ID == SlicePPSID)
                {
                  navHD.SEIPPS = (dword) (SEI - PPS[k].Offset);
                  break;
                }

              navHD.FrameType = FrameType;
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

              if (AUD == 0 || NavPtr == 0 || SystemType == ST_TMSC)  // CRP-2401 z�hlt Filler-NALU nicht als Delimiter
                AUD = HeaderFound;
              if (AUD >= SEI)
                navHD.NextAUD = (dword) (AUD - SEI);

              if (!fNavIn && !navHD.Timems)
                navHD.Timems = (SEIPTS - FirstSEIPTS) / 45;
              if(navHD.Timems > LastTimems) LastTimems = navHD.Timems;
              navHD.Timems = LastTimems;

              navHD.SEISPS = (dword) (SEI - SPS);
              navHD.SPSLen = SPSLen;
              navHD.IFrame = 0;

{
  unsigned long long CurPictureHeaderOffset = dbg_CurPictureHeaderOffset - dbg_SEIPositionOffset;
  if (fNavIn && SEI != CurPictureHeaderOffset && dbg_CurrentPosition/192 != dbg_SEIFound)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu, Differenz= %lld * 192 + %lld\n", dbg_CurrentPosition, dbg_SEIPositionOffset, dbg_CurPictureHeaderOffset, SEI, ((long long int)(SEI-CurPictureHeaderOffset))/192, ((long long int)(SEI-CurPictureHeaderOffset))%192);
//printf("%llu: fwrite: SEI=%llu, nav=%llu\n", dbg_CurrentPosition/192, SEI, CurPictureHeaderOffset);
}

              if (fNavOut && !fwrite(&navHD, sizeof(tnavHD), 1, fNavOut))
              {
                printf("ProcessNavFile(): Error writing to nav file!\n");
                fclose(fNavOut); fNavOut = NULL;
              }
              ret = TRUE;
              NavPtr++;

              SEI = 0;
              AUD = 0;
              FrameType = 3;
//memset(&navHD, 0, sizeof(tnavHD));
//navHD.SEIOffsetHigh = 0; navHD.SEIOffsetLow = 0;
              navHD.Timems = 0;
              navHD.FrameIndex = 0;
            }
            break;
          }

          case NAL_SLICE_IDR:
          case NAL_SLICE:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_SLICE");
            #endif
            GetSlicePPSID = TRUE;

            if((FrameType == 3) && (NALRefIdc > 0)) FrameType = 2;
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
  return ret;
}

bool SDNAV_ParsePacket(tTSPacket *Packet, unsigned long long FilePositionOfPacket)
{
  unsigned long long    PacketOffset;
  byte                  PayloadStart, PayloadSize, Ptr;

  unsigned long long    PictHeader;
  byte                  FrameType;
  bool                  ret = TRUE;

  TRACEENTER;

  // PROCESS THE TS-PACKET

  // Valid TS packet and payload available?
  if((Packet->SyncByte != 'G') || !Packet->Payload_Exists)
  {
    TRACEEXIT;
    return FALSE;
  }

  if (Packet->Payload_Unit_Start)
    PosFirstNull = 0; PosSecondNull = 0; HeaderFound = 0;

  // Adaptation field available?
  if(Packet->Adapt_Field_Exists)
    PayloadStart = (Packet->Data[0] + 1);
  else
    PayloadStart = 0;

  PacketOffset = FilePositionOfPacket + 4 + PACKETOFFSET;
  PayloadSize = 184 - min(PayloadStart, 184);


  // PROCESS THE PAYLOAD

  // Search for start codes in the packet. Continue from last packet
  Ptr = PayloadStart;
  while(Ptr < PayloadSize)
  {
    // Check for Header
    if (!HeaderFound)
    {
      if (PosFirstNull && PosSecondNull && (Packet->Data[Ptr] == 1))
        HeaderFound = PosFirstNull;
      
      if (Packet->Data[Ptr] == 0)
      {
        PosFirstNull = PosSecondNull;
        PosSecondNull = PacketOffset + Ptr;
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
        if (GetPTS2(PTSBuffer, &PTS, NULL))
          if((FirstPTS == 0) && (PTS > 0)) FirstPTS = PTS;
        PTSBufFill = 0;
      }
    }

    // Process a header
    if (HeaderFound)
    {
      PTSBuffer[0] = Packet->Data[Ptr];
      PTSBufFill = 1;

      if (Packet->Data[Ptr] == 0xB3)
        // Sequence Header
        CurrentSeqHeader = HeaderFound;  // +5 k�nnte problematisch sein
      else if (Packet->Data[Ptr] == 0x00)
      {
        // Picture Header
        if (CurrentSeqHeader > 0)
        {
          PictHeaderFound = TRUE;
          PictHeaderSkipped = FALSE;
          Ptr++;
          continue;
        }
      }
    }

    // Process Picture Header
    if (PictHeaderFound)
    {
      if (!PictHeaderSkipped)
      {
        Ptr++;
        PictHeaderSkipped = TRUE;
        continue;
      }
      else
      {
        if(SDNav[0].Timems > LastTimems)
          LastTimems = SDNav[0].Timems;
        else
          SDNav[0].Timems = LastTimems;

        if (NavPtr > 1)
          if (fNavOut && !fwrite(&SDNav[0], sizeof(tnavSD), 1, fNavOut))
          {
            printf("ProcessNavFile(): Error writing to nav file!\n");
            fclose(fNavOut); fNavOut = NULL;
          }
        ret = TRUE;
        memcpy(&SDNav[0], &SDNav[1], sizeof(tnavSD));
        NavPtr++;


        PictHeader = HeaderFound;  // +3 k�nnte problematisch sein

        FrameType = (Packet->Data[Ptr] >> 3) & 0x03;
        if(FrameType == 2) FrameOffset = 0;  // P-Frame

        SDNav[1].FrameIndex  = FrameCtr + FrameOffset;

        if(FrameType == 1)  // I-Frame
        {
          FirstSHPHOffset = (word) (PictHeader - CurrentSeqHeader);
          FrameOffset            = FrameCtr;  // Position des I-Frames in der aktuellen Z�hlung (diese geht weiter, bis P kommt)
          FrameCtr               = 0;         // z�hlt die Distanz zum letzten I-Frame
        }

        SDNav[1].SHOffset        = (dword)(PictHeader - CurrentSeqHeader);
        SDNav[1].FrameType       = FrameType;
        SDNav[1].MPEGType        = 0x20;
        SDNav[1].iFrameSeqOffset = FirstSHPHOffset;

        //Some magic for the AUS pvrs
        SDNav[1].PHOffset        = (dword)(PictHeader - 4 + PACKETOFFSET);
        SDNav[1].PHOffsetHigh    = (dword)((PictHeader - 4 + PACKETOFFSET) >> 32);
        SDNav[1].PTS2            = PTS;

        if(NavPtr > 0)
        {
          if(CurrentSeqHeader > LastPictureHeader)
            SDNav[0].NextPH = (dword) (CurrentSeqHeader - LastPictureHeader);
          else
            SDNav[0].NextPH = (dword) (PictHeader - LastPictureHeader);
        }

        SDNav[1].Timems = (PTS - FirstPTS) / 45;
        SDNav[1].Zero5 = 0;

        LastPictureHeader = PictHeader;
        FrameCtr++;
      }
      PictHeaderFound = FALSE;
    }

    HeaderFound = 0;
    Ptr++;
  }
  TRACEEXIT;
  return ret;
}

bool LoadNavFiles(const char* AbsInNav, const char* AbsOutNav)
{
  TRACEENTER;
  memset(&navHD, 0, sizeof(tnavHD));
  memset(SDNav, 0, 2*sizeof(tnavSD));

  fNavIn = fopen(AbsInNav, "rb");
//  if (fNavIn)
  {
//    setvbuf(fNavIn, NULL, _IOFBF, BUFSIZE);
    fNavOut = fopen(AbsOutNav, "wb");
    if (fNavOut)
    {
//    setvbuf(fNavOut, NULL, _IOFBF, BUFSIZE);
    }
    else
      printf("WARNING: Cannot create nav file %s.\n", AbsOutNav);

    TRACEEXIT;
    return TRUE;
  }
  TRACEEXIT;
  return FALSE;
}

bool CloseNavFiles(void)
{
  bool                  ret = TRUE;

  TRACEENTER;
  if (fNavOut && !isHDVideo && (NavPtr > 0))
    fwrite(&SDNav[0], sizeof(tnavSD), 2, fNavOut);

  if (fNavIn) fclose(fNavIn);
  fNavIn = NULL;
  if (fNavOut)
    ret = (/*fflush(fNavOut) == 0 &&*/ fclose(fNavOut) == 0);
  fNavOut = NULL;

  TRACEEXIT;
  return ret;
}

void ProcessNavFile(const unsigned long long CurrentPosition, const unsigned long long PositionOffset, tTSPacket *Packet)
{
  static byte           NavBuffer[sizeof(tnavHD)];
  static tnavSD        *curSDNavRec = (tnavSD*) &NavBuffer[0];
  static unsigned long long CurPictureHeaderOffset = 0, NextPictureHeaderOffset = 0;
  static bool           FirstRun = TRUE;
  bool WriteNavRec;

  TRACEENTER;
  if (FirstRun && fNavIn)
  {
    // Versuche, nav-Dateien aus Timeshift-Aufnahmen zu unterst�tzen ***experimentell***
    dword FirstDword = 0;
    fread(&FirstDword, 4, 1, fNavIn);
    if(FirstDword == 0x72767062)  // 'bpvr'
      fseek(fNavIn, 1056, SEEK_SET);
    else
      rewind(fNavIn);

    if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
      NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
      if(fNavOut) fclose(fNavOut); fNavOut = NULL;
    }

    memset(CounterStack, 0, COUNTSTACKSIZE * sizeof(tFrameCtr));
    FirstRun = FALSE;
  }

  if (fNavOut)
  {
dbg_CurrentPosition = CurrentPosition;
dbg_PositionOffset = PositionOffset;

    if (isHDVideo)
      WriteNavRec = HDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);
    else
      WriteNavRec = SDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);

    while(fNavIn &&  ( (isHDVideo  && (CurrentPosition > NextPictureHeaderOffset))
                    || (!isHDVideo && (CurrentPosition + PACKETSIZE > NextPictureHeaderOffset))) )
    {

// for Debugging only: CurPictureHeaderOffset sollte IMMER mit der zu schreibenden Position �bereinstimmen
if (isHDVideo)
  dbg_CurPictureHeaderOffset = NextPictureHeaderOffset;
//printf("%llu: Setze Offset aus nav: %llu\n", CurrentPosition/192, NextPictureHeaderOffset);
else
{
  CurPictureHeaderOffset = NextPictureHeaderOffset + 4 - PACKETOFFSET - PositionOffset;
  if (fNavIn && LastPictureHeader != CurPictureHeaderOffset)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu, Differenz= %lld * 192 + %lld\n", CurrentPosition, PositionOffset, NextPictureHeaderOffset, LastPictureHeader-PACKETOFFSET, ((long long int)(LastPictureHeader-CurPictureHeaderOffset))/192, ((long long int)(LastPictureHeader-CurPictureHeaderOffset))%192);
}

      if (isHDVideo)
      {
        navHD.Timems = curSDNavRec->Timems;
//        navHD.FrameIndex = ((tnavHD*)NavBuffer)->FrameIndex;
      }
      else if (fNavIn)
      {
        SDNav[1].Timems = curSDNavRec->Timems;
//        SDNav[1].FrameIndex = curSDNavRec->FrameIndex;
//        SDNav[1].Zero1 = curSDNavRec->Zero1;
      }

      if (fNavIn)
      {
        if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
          NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
        else
        {
          fclose(fNavIn); fNavIn = NULL;
        }
      }
    }
  }
  TRACEEXIT;
}

