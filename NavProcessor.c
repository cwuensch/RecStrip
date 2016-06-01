#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
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
#include "type.h"
#include "NavProcessor.h"
#include "RecStrip.h"

//#define DEBUGLOG 1
#define COUNTSTACKSIZE 8


// Globale Variablen
dword                   LastTimems = 0, TimeOffset = 0;
dword                  *pOutNextTimeStamp = NULL;
FILE                   *fNavIn = NULL, *fNavOut = NULL;
static unsigned long long PosFirstNull = 0, PosSecondNull = 0, HeaderFound = 0;
static dword            PTS = 0;
static byte             PTSBuffer[16];
static int              PTSBufFill = 0;  // 0: keinen PTS suchen, 1..15 Puffer-Füllstand, bei 16 PTS auslesen und zurücksetzen
static byte             FrameCtr = 0, FrameOffset = 0;
static bool             WaitForIFrame = TRUE, FirstPacketAfterCut = FALSE, FirstRecordAfterCut = TRUE;

//HDNAV
static tnavHD           navHD;
static tPPS             PPS[10];
static unsigned long long SEI = 0, SPS = 0, AUD = 0;
static bool             GetPPSID = FALSE, GetSlicePPSID = FALSE, GetPrimPicType = FALSE;
static byte             SlicePPSID = 0;
static int              PPSCount = 0;
static dword            FirstSEIPTS = 0, SEIPTS = 0, IFramePTS = 0, SPSLen = 0;
//static tFrameCtr        CounterStack[COUNTSTACKSIZE];
//static int              LastIFrame = 0;

static unsigned long long dbg_NavPictureHeaderOffset = 0, dbg_SEIFound = 0;
static unsigned long long dbg_CurrentPosition = 0, dbg_PositionOffset = 0, dbg_HeaderPosOffset = 0, dbg_SEIPositionOffset = 0;

//SDNAV
static tnavSD           navSD;
static unsigned long long PictHeader = 0, LastPictureHeader = 0;
static unsigned long long CurrentSeqHeader = 0;
static dword            FirstPTS = 0 /*, LastdPTS = 0*/;
static int              NavPtr = 0;
static word             FirstSHPHOffset = 0;


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
  bool ret = FALSE;
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
    if (pPTS && (Buffer[4] & 0x80) && ((Buffer[6] & 0xe1) ==  0x21) && (Buffer[8] & 0x01) && (Buffer[10] & 0x01))
    {
      *pPTS = ((Buffer[ 6] & 0x0e) << 28) |
              ((Buffer[ 7] & 0xff) << 21) |
              ((Buffer[ 8] & 0xfe) << 13) |
              ((Buffer[ 9] & 0xff) <<  6) |
              ((Buffer[10] & 0xfe) >>  2);
      ret = TRUE;
    }
    if (pDTS && (Buffer[4] & 0xC0) && ((Buffer[6] & 0xf1) ==  0x31) && ((Buffer[11] & 0xf1) == 0x01) && (Buffer[13] & 0x01) && (Buffer[15] & 0x01))
      *pDTS = ((Buffer[11] & 0x0e) << 28) |
              ((Buffer[12] & 0xff) << 21) |
              ((Buffer[13] & 0xfe) << 13) |
              ((Buffer[14] & 0xff) <<  6) |
              ((Buffer[15] & 0xfe) >>  2);
  }
  TRACEEXIT;
  return ret;
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

void SetFirstPacketAfterBreak()
{
  FirstPacketAfterCut = TRUE;
//  WaitForIFrame = TRUE;
}

void HDNAV_ParsePacket(tTSPacket *Packet, unsigned long long FilePositionOfPacket)
{
  byte                  PayloadStart, Ptr;

  static dword          FirstPCR = 0;
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
    // alles andere auch zurücksetzen?
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
dbg_HeaderPosOffset = dbg_PositionOffset;
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
              if(FirstSEIPTS == 0) FirstSEIPTS = PTS;
              SEIFoundInPacket = TRUE;

dbg_SEIPositionOffset = dbg_HeaderPosOffset;
dbg_SEIFound = dbg_CurrentPosition/PACKETSIZE;
//printf("%llu: SEI found: %llu\n", dbg_CurrentPosition/PACKETSIZE, SEI);
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
            if((navHD.FrameType == 3) && (NALRefIdc > 0)) navHD.FrameType = 2;
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
              if (WaitForIFrame) {FrameOffset = 0; FrameCtr = 1;}

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
                FrameOffset     = FrameCtr + ((/*SystemType==ST_TMSC ||*/ !FrameCtr) ? 0 : 1);  // Position des I-Frames in der aktuellen Zählung (diese geht weiter, bis P kommt) - SRP-2401 zählt nach dem I-Frame + 2 (neuer CRP auch)
                FrameCtr        = 0;        // zählt die Distanz zum letzten I-Frame
                IFramePTS       = SEIPTS;
              }
              FrameCtr++;


              // VARIANTE VON jkIT
/*              // jeder beliebige Frame-Typ
              if (navHD.FrameIndex == 0)
              {
                // CounterStack von oben durchgehen, bis ein PTS gefunden wird, der <= dem aktuellen ist
                for (i = COUNTSTACKSIZE; i > 0; i--)
                {
                  p = (LastIFrame + i) % COUNTSTACKSIZE;          // Schleife läuft von LastIFrame rückwärts, max. Stackgröße
                  navHD.FrameIndex += CounterStack[p].FrameCtr;   // alle Counter auf dem Weg zum passenden I-Frame addieren
                  if ((int)(SEIPTS - CounterStack[p].PTS) > 0)    // überlaufsichere Prüfung, ob PTS > Counter-PTS
                    break;
                }
              }

              if(FrameType == 1)  // I-Frame
              {
                if (SystemType != ST_TMSC && CounterStack[LastIFrame].PTS != 0)
                  CounterStack[LastIFrame].FrameCtr++;  // SRP-2401 zählt nach dem I-Frame + 2

                // neuen I-Frame in den CounterStack einfügen, an Position LastIFrame+1
                LastIFrame = (LastIFrame + 1) % COUNTSTACKSIZE;
                CounterStack[LastIFrame].PTS = SEIPTS;
                CounterStack[LastIFrame].FrameCtr = 0;
              }

              // aktuellen Frame-Counter erhöhen
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

              if (AUD == 0 || NavPtr == 0 /*|| SystemType == ST_TMSC*/)  // CRP-2401 zählt Filler-NALU nicht als Delimiter? (2015Mar26 schon!)
                AUD = HeaderFound;
              if (AUD >= SEI)
                navHD.NextAUD = (dword) (AUD - SEI);

              if (!fNavIn && !navHD.Timems)
                navHD.Timems = (SEIPTS - FirstSEIPTS) / 45;

              // nach Schnittpunkt die fehlende Zeit von Timems abziehen
              if (FirstRecordAfterCut)
              {
                TimeOffset = navHD.Timems - LastTimems;
                FirstRecordAfterCut = FALSE;
              }
              navHD.Timems -= TimeOffset;

              if (pOutNextTimeStamp)
              {
                *pOutNextTimeStamp = navHD.Timems;
                pOutNextTimeStamp = NULL;
              }

              // sicherstellen, dass Timems monoton ansteigt
              if( ((int)(navHD.Timems - LastTimems)) >= 0)  LastTimems = navHD.Timems;
              else  navHD.Timems = LastTimems;

{
  unsigned long long RefPictureHeaderOffset = dbg_NavPictureHeaderOffset - dbg_SEIPositionOffset;
  if (fNavIn && SEI != RefPictureHeaderOffset && dbg_CurrentPosition/PACKETSIZE != dbg_SEIFound)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu, Differenz= %lld * %d + %lld\n", dbg_CurrentPosition, dbg_SEIPositionOffset, dbg_NavPictureHeaderOffset, SEI, ((long long int)(SEI-RefPictureHeaderOffset))/PACKETSIZE, PACKETSIZE, ((long long int)(SEI-RefPictureHeaderOffset))%PACKETSIZE);
//printf("%llu: fwrite: SEI=%llu, nav=%llu\n", dbg_CurrentPosition/PACKETSIZE, SEI, NavPictureHeaderOffset);
}

              if (WaitForIFrame && navHD.FrameType == 1)
                WaitForIFrame = FALSE;

              if (fNavOut && !WaitForIFrame && !fwrite(&navHD, sizeof(tnavHD), 1, fNavOut))
              {
                printf("ProcessNavFile(): Error writing to nav file!\n");
                fclose(fNavOut); fNavOut = NULL;
              }

              if (FirstPacketAfterCut)
              {
                if (NavPtr > 0)
                {
                  WaitForIFrame = TRUE;
                  FirstRecordAfterCut = TRUE;
                }
                FirstPacketAfterCut = FALSE;
              }
              NavPtr++;
              SEI = 0;
              AUD = 0;
//memset(&navHD, 0, sizeof(tnavHD));
//navHD.SEIOffsetHigh = 0; navHD.SEIOffsetLow = 0;
              navHD.Timems = 0;
              navHD.FrameIndex = 0;
              GetPrimPicType = TRUE;
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

void SDNAV_ParsePacket(tTSPacket *Packet, unsigned long long FilePositionOfPacket)
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
    PosFirstNull = 0; PosSecondNull = 0; HeaderFound = 0; PTSBufFill = 0;

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
        if (GetPTS2(PTSBuffer, &PTS, NULL))
          if((FirstPTS == 0) && (PTS > 0)) FirstPTS = PTS;
        PTSBufFill = 0;
      }
    }

    // Process a header
    if (HeaderFound)
    {
      if (PictHeader)
      {
        // noch ein Byte skippen für FrameType
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
        if (CurrentSeqHeader > 0)
        {
          PictHeader = HeaderFound;
          Ptr++;  // nächstes Byte für FrameType
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

      // nach Schnittpunkt die fehlende Zeit von Timems abziehen
      if (FirstRecordAfterCut)
      {
        TimeOffset = navSD.Timems - LastTimems;
        FirstRecordAfterCut = FALSE;
      }
      navSD.Timems -= TimeOffset;

      if (pOutNextTimeStamp)
      {
        *pOutNextTimeStamp = navSD.Timems;
        pOutNextTimeStamp = NULL;
      }

      // sicherstellen, dass Timems monoton ansteigt
      if( ((int)(navSD.Timems - LastTimems)) >= 0)  LastTimems = navSD.Timems;
      else  navSD.Timems = LastTimems;

      if (WaitForIFrame && navSD.FrameType == 1)
        WaitForIFrame = FALSE;

      if(NavPtr > 0)
      {
{
  unsigned long long RefPictureHeaderOffset = dbg_NavPictureHeaderOffset - dbg_HeaderPosOffset;
  if (fNavIn && LastPictureHeader != RefPictureHeaderOffset)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu, Differenz= %lld * %d + %lld\n", dbg_CurrentPosition, dbg_HeaderPosOffset, dbg_NavPictureHeaderOffset, LastPictureHeader, ((long long int)(LastPictureHeader-RefPictureHeaderOffset))/PACKETSIZE, PACKETSIZE, ((long long int)(LastPictureHeader-RefPictureHeaderOffset))%PACKETSIZE);
}

        // Write the nav record
        if (fNavOut && !WaitForIFrame && !fwrite(&navSD, sizeof(tnavSD), 1, fNavOut))
        {
          printf("ProcessNavFile(): Error writing to nav file!\n");
          fclose(fNavOut); fNavOut = NULL;
        }
      }

      if (FirstPacketAfterCut)
      {
        WaitForIFrame = TRUE;
        FirstRecordAfterCut = TRUE;
        FirstPacketAfterCut = FALSE;
      }
      NavPtr++;

      // NEW NAV RECORD
      FrameType = (Packet->Data[Ptr] >> 3) & 0x03;
      if (WaitForIFrame) {FrameOffset = 0; FrameCtr = 1;}

      if(FrameType == 2) FrameOffset = 0;  // P-Frame

      navSD.FrameIndex  = FrameCtr + FrameOffset;

      if(FrameType == 1)  // I-Frame
      {
        FirstSHPHOffset = (word) (PictHeader - CurrentSeqHeader);
        FrameOffset            = FrameCtr;  // Position des I-Frames in der aktuellen Zählung (diese geht weiter, bis P kommt)
        FrameCtr               = 0;         // zählt die Distanz zum letzten I-Frame
      }

      navSD.SHOffset        = (dword)(PictHeader - CurrentSeqHeader);
      navSD.FrameType       = FrameType;
      navSD.MPEGType        = 0x20;
      navSD.iFrameSeqOffset = FirstSHPHOffset;

      //Some magic for the AUS pvrs
      navSD.PHOffset        = (dword)PictHeader;
      navSD.PHOffsetHigh    = (dword)(PictHeader >> 32);
      navSD.PTS2            = PTS;

      navSD.Timems = (PTS - FirstPTS) / 45;
      navSD.Zero5 = 0;
      navSD.NextPH = 0;

      LastPictureHeader = PictHeader;
dbg_HeaderPosOffset = dbg_PositionOffset;
      PictHeader = 0;
      FrameCtr++;
    }

    HeaderFound = 0;
    Ptr++;
  }
  TRACEEXIT;
  return;
}

void ProcessNavFile(const unsigned long long CurrentPosition, const unsigned long long PositionOffset, tTSPacket *Packet)
{
  static byte           NavBuffer[sizeof(tnavHD)];
  static tnavSD        *curSDNavRec = (tnavSD*) &NavBuffer[0];
  static unsigned long long NextPictureHeaderOffset = 0;
  static bool           FirstRun = TRUE;

  TRACEENTER;
  if (FirstRun && fNavIn)
  {
    if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
      NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
      if(fNavOut) fclose(fNavOut); fNavOut = NULL;
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

    while(fNavIn && !FirstPacketAfterCut && (CurrentPosition + 188 > NextPictureHeaderOffset))
    {
dbg_NavPictureHeaderOffset = NextPictureHeaderOffset;

      if (isHDVideo)
        navHD.Timems = curSDNavRec->Timems;
//        navHD.FrameIndex = ((tnavHD*)NavBuffer)->FrameIndex;
      else
        navSD.Timems = curSDNavRec->Timems;
//        navSD.FrameIndex = curSDNavRec->FrameIndex;

      if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
        NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
      else
      {
        fclose(fNavIn); fNavIn = NULL;
      }
    }
  }
  TRACEEXIT;
}


void QuickNavProcess(const unsigned long long CurrentPosition, const unsigned long long PositionOffset)
{
  static byte           NavBuffer[sizeof(tnavHD)];
  static tnavSD        *curSDNavRec = (tnavSD*) &NavBuffer[0];
  static unsigned long long NextPictureHeaderOffset = 0;
  static bool           FirstRun = TRUE;

  TRACEENTER;
  if (FirstRun && fNavIn)
  {
    if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
      NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
    else
    {
      fclose(fNavIn); fNavIn = NULL;
      if(fNavOut) fclose(fNavOut); fNavOut = NULL;
    }
    FirstRun = FALSE;
  }

  if (FirstPacketAfterCut)
  {
    while (fNavIn && (NextPictureHeaderOffset <= CurrentPosition))
    {
      // nächsten Record einlesen
      if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
        NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
      else
      {
        fclose(fNavIn); fNavIn = NULL;
        if(fNavOut) fclose(fNavOut); fNavOut = NULL;
      }
    }

    // nach Schnittpunkt die fehlende Zeit von Timems abziehen
    TimeOffset = curSDNavRec->Timems - LastTimems;

    WaitForIFrame = TRUE;
    FirstPacketAfterCut = FALSE;
  }
  else
  {
    while (fNavIn && (NextPictureHeaderOffset <= CurrentPosition))
    {
      // Position anpassen
      NextPictureHeaderOffset   -= PositionOffset;
      curSDNavRec->PHOffset      = (dword)NextPictureHeaderOffset;
      curSDNavRec->PHOffsetHigh  = (dword)(NextPictureHeaderOffset >> 32);

      // Zeit anpassen
      curSDNavRec->Timems -= TimeOffset;
      LastTimems = curSDNavRec->Timems;
      if (pOutNextTimeStamp)
      {
        *pOutNextTimeStamp = curSDNavRec->Timems;
        pOutNextTimeStamp = NULL;
      }

      // I-Frame prüfen
      if (WaitForIFrame && (curSDNavRec->FrameType == 1))
        WaitForIFrame = FALSE;

      // Record schreiben
      if (fNavOut && !WaitForIFrame && !fwrite(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavOut))
      {
        printf("ProcessNavFile(): Error writing to nav file!\n");
        fclose(fNavIn); fNavIn = NULL;
        fclose(fNavOut); fNavOut = NULL;
        break;
      }

      // nächsten Record einlesen
      if (fread(NavBuffer, isHDVideo ? sizeof(tnavHD) : sizeof(tnavSD), 1, fNavIn))
        NextPictureHeaderOffset = ((unsigned long long)(curSDNavRec->PHOffsetHigh) << 32) | curSDNavRec->PHOffset;
      else
      {
        fclose(fNavIn); fNavIn = NULL;
        if(fNavOut) fclose(fNavOut); fNavOut = NULL;
      }
    }
  }
  TRACEEXIT;
}

bool LoadNavFiles(const char* AbsInNav, const char* AbsOutNav)
{
  TRACEENTER;
  memset(&navHD, 0, sizeof(tnavHD));
  memset(&navSD, 0, sizeof(tnavSD));

  fNavIn = fopen(AbsInNav, "rb");
  if (fNavIn)
  {
//    setvbuf(fNavIn, NULL, _IOFBF, BUFSIZE);

    // Versuche, nav-Dateien aus Timeshift-Aufnahmen zu unterstützen ***experimentell***
    dword FirstDword = 0;
    fread(&FirstDword, 4, 1, fNavIn);
    if(FirstDword == 0x72767062)  // 'bpvr'
      fseek(fNavIn, 1056, SEEK_SET);
    else
      rewind(fNavIn);
  }

  fNavOut = fopen(AbsOutNav, "wb");
  if (fNavOut)
  {
//  setvbuf(fNavOut, NULL, _IOFBF, BUFSIZE);
  }
  else
    printf("WARNING: Cannot create nav file %s.\n", AbsOutNav);

  TRACEEXIT;
  return TRUE;
}

bool CloseNavFiles(void)
{
  bool                  ret = TRUE;

  TRACEENTER;
  if (fNavOut && !isHDVideo && (NavPtr > 0))
  {
    navSD.Timems -= TimeOffset;
    LastTimems = navSD.Timems;
    if (pOutNextTimeStamp)
      *pOutNextTimeStamp = navSD.Timems;
    fwrite(&navSD, sizeof(tnavSD), 1, fNavOut);
  }

  if (fNavIn) fclose(fNavIn);
  fNavIn = NULL;
  if (fNavOut)
    ret = (/*fflush(fNavOut) == 0 &&*/ fclose(fNavOut) == 0);
  fNavOut = NULL;

  TRACEEXIT;
  return ret;
}
