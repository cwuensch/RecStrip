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


// Globale Variablen
static FILE            *fNavIn = NULL, *fNavOut = NULL;
static byte             FrameType;
static dword            PTS = 0, DTS;

//HDNAV
static tnavHD           navHD;
static long long        SEI = 0, SPS = 0, AUD, NextAUD = -1, SPSLen;
static tPPS             PPS[10];
static int              PPSCount = 0;
static byte             SlicePPSID;
static int              FrameIndex = 0;
static dword            FirstSEIPTS = 0, SEIPTS, LastTimems = 0;

//SDNAV
static tnavSD           SDNav[2];
static unsigned long long LastPictureHeader = 0;
static dword            FirstPTS = 0, LastdPTS = 0;
static int              NavPtr;
static unsigned long long CurrentSeqHeader = 0;
static dword            PacketNr = 0;
static dword            FirstSHPHOffset = 0;
static dword            FrameCtr = 0, FrameOffset = 0;


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

  d = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  if(d == 0)
    return 0;

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

  return (1 << leadingZeroBits) - 1 + d;
}

static bool GetPTS(byte *Buffer, dword *PTS, dword *DTS)
{
  if((Buffer[0] == 0x00) && (Buffer[1] == 0x00) && (Buffer[2] == 0x01) && ((Buffer[3] & 0xf0) == 0xe0) && (Buffer[7] & 0x80))
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
    *PTS = ((Buffer[ 9] & 0x0e) << 28) |
           ((Buffer[10] & 0xff) << 21) |
           ((Buffer[11] & 0xfe) << 13) |
           ((Buffer[12] & 0xff) <<  6) |
           ((Buffer[13] & 0xfe) >>  2);

    if(Buffer[7] & 0x40)
      *DTS = ((Buffer[14] & 0x0e) << 28) |
             ((Buffer[15] & 0xff) << 21) |
             ((Buffer[16] & 0xfe) << 13) |
             ((Buffer[17] & 0xff) <<  6) |
             ((Buffer[18] & 0xfe) >>  2);
    else
      *DTS = 0;
    return TRUE;
  }
  return FALSE;
}

static bool GetPCR(byte *pBuffer, dword *PCR)
{
  if ((pBuffer[0] == 0x47) && ((pBuffer[3] & 0x20) != 0) && (pBuffer[4] > 0) && (pBuffer[5] & 0x10))
  {
    //Extract the time out of the PCR bit pattern
    //The PCR is clocked by a 90kHz generator. To convert to milliseconds
    //the 33 bit number can be shifted right and divided by 45
    *PCR = (dword)((((dword)pBuffer[6] << 24) | (pBuffer[7] << 16) | (pBuffer[8] << 8) | pBuffer[9]) / 45);
    return TRUE;
  }
  return FALSE;
}

static dword DeltaPCR(dword FirstPCR, dword SecondPCR)
{
  if(FirstPCR <= SecondPCR)
    return (SecondPCR - FirstPCR);
  else
    return (95443718 - FirstPCR + SecondPCR);
}

static dword FindSequenceHeaderCode(byte *Buffer)
{
  int                   i;

  for(i = 0; i < 180; i++)
  {
    if((Buffer[i] == 0x00) && (Buffer[i + 1] == 0x00) && (Buffer[i + 2] == 0x01) && (Buffer[i + 3] == 0xb3))
      return i + 8;
  }
  return 0;
}

static dword FindPictureHeader(byte *Buffer, byte *FrameType)
{
  int                   i;

  for(i = 0; i < 180; i++)
  {
    if((Buffer[i] == 0x00) && (Buffer[i + 1] == 0x00) && (Buffer[i + 2] == 0x01) && (Buffer[i + 3] == 0x00))
    {
      *FrameType = (Buffer[i + 5] >> 3) & 0x03;
      return i + 8;
    }
  }
  return 0;
}

bool HDNAV_ParsePacket(trec *Packet, unsigned long long FilePositionOfPacket)
{
  byte                  NALType, NALRefIdc;
  int                   Ptr, PayloadStart;
  char                  s[80];
  dword                 PCR;
  static dword          FirstPCR = 0;

  static trec           PrimaryPacket, SecondaryPacket;
  static unsigned long long PrimaryTSOffset, SecondaryTSOffset;
  unsigned long long    PrimaryPayloadOffset;
  byte                  PSBuffer[2*184], PrimaryPayloadSize;

  bool                  ret = FALSE;

  //Valid TS packet and payload available?
  if((Packet->TSH[0] != 0x47) || ((Packet->TSH[3] & 0x10) == 0))
    return FALSE;

  //Shift packets from secondary to primary buffer
  memcpy(&PrimaryPacket, &SecondaryPacket, sizeof(trec));
  PrimaryTSOffset = SecondaryTSOffset;
  memcpy(&SecondaryPacket, Packet, sizeof(trec));
  SecondaryTSOffset = FilePositionOfPacket;

  #if DEBUGLOG != 0
    //Start of a new PES?
    if(PrimaryPacket.TSH[1] & 0x40)
      printf("\n%8.8llx: PES Start\n", PrimaryTSOffset);
  #endif

  //Convert primary and secondary TS buffers into a PS packet
//  memset(PSBuffer, 0, 2*184);

  //Start with the primary buffer
  PrimaryPayloadOffset = PrimaryTSOffset + 4 + PACKETOFFSET;

  //Adaptation field available?
  if(PrimaryPacket.TSH[3] & 0x20)
  {
    //If available, get the PCR
    if(GetPCR(PrimaryPacket.TSH, &PCR))
    {
      if(FirstPCR == 0) FirstPCR = PCR;
      #if DEBUGLOG != 0
        printf("%8.8llx: PCR = 0x%8.8x, dPCR = 0x%8.8x\n", PrimaryTSOffset, PCR, DeltaPCR(FirstPCR, PCR));
      #endif
    }
    PayloadStart = (PrimaryPacket.Data[0] + 1);
    PrimaryPayloadOffset += PayloadStart;
  }
  else
  {
    PayloadStart = 0;
  }

  PrimaryPayloadSize = 184 - PayloadStart;
  memcpy(PSBuffer, &PrimaryPacket.Data[PayloadStart], PrimaryPayloadSize);

  //Now the secondary buffer
  if(SecondaryPacket.TSH[3] & 0x20)
  {
    PayloadStart = (SecondaryPacket.Data[0] + 1);
  }
  else
  {
    PayloadStart = 0;
  }
  memcpy(&PSBuffer[PrimaryPayloadSize], &SecondaryPacket.Data[PayloadStart], 184 - PayloadStart);

  //Search for start codes in the primary buffer. The secondary buffer is used if a NALU runs over the 184 byte TS packet border
  Ptr = 0;
  while(Ptr < PrimaryPayloadSize)
  {
    if((PSBuffer[Ptr] == 0x00) && (PSBuffer[Ptr+1] == 0x00) && (PSBuffer[Ptr+2] == 0x01))
    {
      //Calculate the length of the SPS NAL
      if((SPS != 0) && (SPSLen == 0)) SPSLen = PrimaryPayloadOffset + Ptr - SPS;

      //Calculate the length of the previous PPS NAL
      if((PPSCount > 0) && (PPS[PPSCount - 1].Len == 0))
      {
        PPS[PPSCount - 1].Len = (byte) (PrimaryPayloadOffset + Ptr - PPS[PPSCount - 1].Offset);
      }

      if((PSBuffer[Ptr+3] & 0x80) == 0x00)
      {
        NALRefIdc = (PSBuffer[Ptr+3] >> 5) & 3;
        NALType = PSBuffer[Ptr+3] & 0x1f;

        switch(NALType)
        {
          case NAL_SEI:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_SEI");
            #endif

            SEI = PrimaryPayloadOffset + Ptr;
            SEIPTS = PTS;

            if(FirstSEIPTS == 0) FirstSEIPTS = PTS;

            break;
          };

          case NAL_SPS:
          {
            #if DEBUGLOG != 0
              strcpy(s, "NAL_SPS");
            #endif

            SPS = PrimaryPayloadOffset + Ptr;
            SPSLen = 0;
            PPSCount = 0;
            break;
          };

          case NAL_PPS:
          {
            byte        Bit;

            #if DEBUGLOG != 0
              strcpy(s, "NAL_PPS");
            #endif

            //We need to remember every PPS because the upcoming slice will point to one of them
            if(PPSCount < 10)
            {
              PPS[PPSCount].Offset = PrimaryPayloadOffset + Ptr;
              Bit = 31;
              PPS[PPSCount].ID = get_ue_golomb32(&PSBuffer[Ptr+4], &Bit);
              #if DEBUGLOG != 0
                sprintf(&s[strlen(s)], " (ID=%d)", PPS[PPSCount].ID);
              #endif
              PPS[PPSCount].Len = 0;
              PPSCount++;
            }

            FrameType = 1;
            break;
          };

          case NAL_AU_DELIMITER:
          case NAL_FILLER_DATA:
          {
            byte    k;

            #if DEBUGLOG != 0
              if(NALType == NAL_AU_DELIMITER)
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
              else
                strcpy(s, "NAL_FILLER_DATA");
            #endif

            AUD = PrimaryPayloadOffset + Ptr;

            if((SEI != 0) && (SPS != 0) && (PPSCount != 0))
            {
              switch(FrameType)
              {
                case 1:
                {
                  FrameIndex = 0;
                  break;
                }

                case 2:
                case 3:
                {
                  FrameIndex++;
                  break;
                }
              }

//              memset(&navHD, 0, sizeof(tnavHD));

              for(k = 0; k < PPSCount; k++)
                if(PPS[k].ID == SlicePPSID)
                {
                  navHD.SEIPPS = SEI - PPS[k].Offset;
                  break;
                }

              navHD.FrameType = FrameType;
              navHD.MPEGType = 0x30;
              navHD.FrameIndex = FrameIndex;

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
              navHD.NextAUD = (dword) (AUD - SEI);

              if (!fNavIn && !navHD.Timems)
                navHD.Timems = (SEIPTS - FirstSEIPTS) / 45;
              if(navHD.Timems > LastTimems) LastTimems = navHD.Timems;
              navHD.Timems = LastTimems;

              navHD.SEISPS = (dword) (SEI - SPS);
              navHD.SPSLen = SPSLen;
              navHD.IFrame = 0;

//              TAP_Hdd_Fwrite(&navHD, sizeof(tnavHD), 1, fTF);
              if (fNavOut && !fwrite(&navHD, sizeof(tnavHD), 1, fNavOut))
              {
                printf("ProcessNavFile(): Error writing to nav file!\n");
                fclose(fNavOut); fNavOut = NULL;
              }
              ret = TRUE;
              NavPtr++;

              SEI = 0;
              FrameType = 3;
//memset(&navHD, 0, sizeof(tnavHD));
//navHD.SEIOffsetHigh = 0; navHD.SEIOffsetLow = 0;
              navHD.Timems = 0;
            }
            break;
          }

          case NAL_SLICE_IDR:
          case NAL_SLICE:
          {
            byte        Bit;

            #if DEBUGLOG != 0
              strcpy(s, "NAL_SLICE");
            #endif

            Bit = 31;
            //first_mb_in_slice
            get_ue_golomb32(&PSBuffer[Ptr+4], &Bit);
            //slice_type
            get_ue_golomb32(&PSBuffer[Ptr+4], &Bit);
            //pic_parameter_set_id
            SlicePPSID = get_ue_golomb32(&PSBuffer[Ptr+4], &Bit);

            #if DEBUGLOG != 0
              sprintf(&s[strlen(s)], " (PPS ID=%hu)\n", SlicePPSID);
            #endif

            if((FrameType == 3) && (NALRefIdc > 0)) FrameType = 2;
            break;
          }

          #if DEBUGLOG != 0
            default:
              printf("Unexpected NAL 0x%2.2x\n", NALType);
          #endif
        }

        #if DEBUGLOG != 0
          if(NALRefIdc != 0) sprintf(&s[strlen(s)], " (NALRefIdc=%hu)", NALRefIdc);
          printf("%8.8llx: %s\n", PrimaryPayloadOffset + Ptr, s);
        #endif

        Ptr += 4;
      }
      else if(GetPTS(&PSBuffer[Ptr], &PTS, &DTS))
      {
        #if DEBUGLOG != 0
          printf("%8.8llx: PTS = 0x%8.8x, DTS = 0x%8.8x\n", PrimaryPayloadOffset + Ptr, PTS, DTS);
        #endif
        Ptr+=13;
      }
    }
    Ptr++;
  }
  return ret;
}

bool SDNAV_ParsePacket(trec *Packet, unsigned long long FilePositionOfPacket)
{
  unsigned long long    SeqHeader, PictHeader;
  byte                  FrameType;
  bool                  ret = TRUE;
  int                   i;

  SeqHeader = FindSequenceHeaderCode(&Packet->Data[0]);
  if(SeqHeader != 0)
    CurrentSeqHeader = FilePositionOfPacket + SeqHeader;

  for(i = 0; i < 180; i++)
  {
    if(GetPTS(&Packet->Data[i], &PTS, &DTS))
    {
      if((FirstPTS == 0) && (PTS > 0)) FirstPTS = PTS;
      break;
    }
  }

  if(CurrentSeqHeader > 0)
  {
    PictHeader = FindPictureHeader(&Packet->Data[0], &FrameType);
    if(PictHeader > 0)
    {
//      if(NavPtr > 0) TAP_Hdd_Fwrite(&SDNav[0], sizeof(tnavSD), 1, fTF);
      if (NavPtr > 0 && SDNav[0].PHOffset > 0)
        if (fNavOut && !fwrite(&SDNav[0], sizeof(tnavSD), 1, fNavOut))
        {
          printf("ProcessNavFile(): Error writing to nav file!\n");
          fclose(fNavOut); fNavOut = NULL;
        }
      ret = TRUE;
      memcpy(&SDNav[0], &SDNav[1], sizeof(tnavSD));
      SDNav[1].Timems=0;
      NavPtr++;

      PictHeader += FilePositionOfPacket;

      if(FrameType == 1)
      {
        FirstSHPHOffset = (dword) (PictHeader - CurrentSeqHeader);
        FrameOffset     = FrameCtr;
        FrameCtr        = 0;
      }

      if(FrameType == 2) FrameOffset = 0;

      SDNav[1].SHOffset     = (dword)(PictHeader - CurrentSeqHeader) | (FrameType << 24);
      SDNav[1].MPEGType     = 0x20;
      SDNav[1].FrameIndex   = FrameCtr + FrameOffset;
      SDNav[1].Field5       = FirstSHPHOffset;

      //Some magic for the AUS pvrs
      SDNav[1].PHOffset     = (dword)(PictHeader - 4 + PACKETOFFSET);
      SDNav[1].PHOffsetHigh = (dword)((PictHeader - 4 + PACKETOFFSET) >> 32);
	    SDNav[1].PTS2         = PTS;

      if(NavPtr > 0)
      {
        if(SeqHeader != 0) SDNav[0].NextPH = (dword) (CurrentSeqHeader - LastPictureHeader);
                      else SDNav[0].NextPH = (dword) (PictHeader - LastPictureHeader);
      }

      SDNav[1].Timems = (PTS - FirstPTS) / 45;
      if(SDNav[1].Timems > LastdPTS) LastdPTS = SDNav[1].Timems;
      else SDNav[1].Timems = LastdPTS;

      SDNav[1].Zero1 = 0;
      SDNav[1].Zero5 = 0;

      LastPictureHeader = PictHeader;
      FrameCtr++;
    }
  }
  return ret;
}


bool LoadNavFiles(const char* AbsInNav, const char* AbsOutNav)
{
  memset(&navHD, 0, sizeof(tnavHD));
  memset(SDNav, 0, 2*sizeof(tnavSD));

  fNavIn = fopen(AbsInNav, "rb");
//  if (fNavIn)
  {
//    setvbuf(fNavIn, NULL, _IOFBF, BUFSIZE);
    fNavOut = fopen(AbsOutNav, "wb");
    if (fNavOut)
    {
//        setvbuf(fNavOut, NULL, _IOFBF, BUFSIZE);
    }
    else
      printf("WARNING: Cannot create nav file %s.\n", AbsOutNav);
    return TRUE;
  }
  return FALSE;
}

bool CloseNavFiles(void)
{
  if (!isHDVideo && (NavPtr > 0) && fNavOut)
    fwrite(&SDNav[0], sizeof(tnavSD), 1, fNavOut);

  if (fNavIn) fclose(fNavIn);
  fNavIn = NULL;
  if (fNavOut)
    return (fflush(fNavOut) == 0 && fclose(fNavOut) == 0);
  else
    return TRUE;
}

void ProcessNavFile(const unsigned long long CurrentPosition, const unsigned long long PositionOffset, trec *Packet)
{
  static byte           NavBuffer[sizeof(tnavHD)];
  static tnavSD        *curSDNavRec = (tnavSD*) &NavBuffer[0];
  static bool           FirstRun = TRUE;
  static unsigned long long CurPictureHeaderOffset = 0, NextPictureHeaderOffset = 0;
  bool WriteNavRec;

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
    FirstRun = FALSE;
  }

  if (fNavOut)
  {
    if (isHDVideo)
      WriteNavRec = HDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);
    else
      WriteNavRec = SDNAV_ParsePacket(Packet, CurrentPosition - PositionOffset);

    if (CurrentPosition >= NextPictureHeaderOffset + PACKETSIZE)
    {

// for Debugging only: CurPictureHeaderOffset sollte IMMER mit der zu schreibenden Position �bereinstimmen
CurPictureHeaderOffset = NextPictureHeaderOffset - PositionOffset;
if (isHDVideo)
{
  if (fNavIn && SEI != CurPictureHeaderOffset && SEI-CurPictureHeaderOffset > 3*192)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu\n", CurrentPosition, PositionOffset, NextPictureHeaderOffset, SEI);
}
else
  if (fNavIn && LastPictureHeader != CurPictureHeaderOffset && LastPictureHeader-CurPictureHeaderOffset > 3*192)
    printf("DEBUG: Problem! pos=%llu, offset=%llu, Orig-Nav-PHOffset=%llu, Rebuilt-Nav-PHOffset=%llu\n", CurrentPosition, PositionOffset, NextPictureHeaderOffset, LastPictureHeader-PACKETOFFSET);

      if (isHDVideo)
        navHD.Timems = curSDNavRec->Timems;
      else if (fNavIn)
        SDNav[1].Timems = curSDNavRec->Timems;

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
}
