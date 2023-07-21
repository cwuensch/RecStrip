#define _LARGEFILE_SOURCE   1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS  64

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "type.h"
#include "RecStrip.h"
#include "PESProcessor.h"
#include "PESFileLoader.h"
#include "NavProcessor.h"
#include "RebuildInf.h"
#include "NALUDump.h"


static byte            *p = NULL;
static word             TSPID = (word) -1;
static dword            LastDTS = 0;
static long long        CurPCR = 0;
static byte             ContCtr = 0;
static bool             FirstRun = FALSE, FirstPacket = FALSE, DiscontinuityFlag = FALSE, PayloadUnitStart = FALSE;


//------ TS to PS converter
bool PSBuffer_Init(tPSBuffer *PSBuffer, word PID, int BufferSize, bool TablePacket, bool DropBufferOnErr, bool SkipFirstIncomplete)
{
  TRACEENTER;

  memset(PSBuffer, 0, sizeof(tPSBuffer));
  PSBuffer->PID = PID;
  PSBuffer->TablePacket = TablePacket;
  PSBuffer->IgnoreContErrors = !DropBufferOnErr;
  PSBuffer->StartImmediate = !SkipFirstIncomplete;
  PSBuffer->BufferSize = BufferSize;

  PSBuffer->Buffer1 = (byte*) malloc(BufferSize);
  PSBuffer->Buffer2 = (byte*) malloc(BufferSize);
  PSBuffer->pBuffer = PSBuffer->Buffer1;
  PSBuffer->LastCCCounter = 255;

  TRACEEXIT;
  return (PSBuffer->Buffer1 && PSBuffer->Buffer2);
}

void PSBuffer_Reset(tPSBuffer *PSBuffer)
{
  TRACEENTER;
  if(PSBuffer)
  {
    if(PSBuffer->Buffer1)
    {
      free(PSBuffer->Buffer1);
      PSBuffer->Buffer1 = NULL;
    }

    if(PSBuffer->Buffer2)
    {
      free(PSBuffer->Buffer2);
      PSBuffer->Buffer2 = NULL;
    }

    memset(PSBuffer, 0, sizeof(tPSBuffer));
  }
  TRACEEXIT;
}

void PSBuffer_DropCurBuffer(tPSBuffer *PSBuffer)
{
  TRACEENTER;
  switch(PSBuffer->ValidBuffer)
  {
    case 0:
    case 1: PSBuffer->pBuffer = PSBuffer->Buffer1; break;
    case 2: PSBuffer->pBuffer = PSBuffer->Buffer2; break;
  }
  memset(PSBuffer->pBuffer, 0, PSBuffer->BufferSize);
  PSBuffer->BufferPtr = 0;
//  PSBuffer->NewPCR = 0;
  PSBuffer->LastCCCounter = 255;
  PSBuffer->curSectionLen = 0;
  TRACEEXIT;
}

void PSBuffer_StartNewBuffer(tPSBuffer *PSBuffer, bool SkipFirstIncomplete, bool ResetContinuity)
{
  TRACEENTER;

  if(PSBuffer->BufferPtr != 0)
  {
    // Paket strippen
    if (DoStrip && PSBuffer->PID == VideoPID)
    {
//      tPESHeader *CurPESpack = (tPESHeader*)(PSBuffer->pBuffer - PSBuffer->BufferPtr);
      byte *CurPESpack = (PSBuffer->pBuffer - PSBuffer->BufferPtr);
//      int HeaderLen = 6;

      // Strippen
      if (NALUDump_PES((byte*) CurPESpack, &PSBuffer->BufferPtr) == FALSE)

/*      // Prüfen, ob Paket noch Payload enthält
      if (CurPESpack->OptionalHeaderMarker == 2)
        HeaderLen += (3 + CurPESpack->PESHeaderLen);
            
      if (PSBuffer->BufferPtr <= HeaderLen) */
      {
        // Es sind nur Fülldaten im PES-Paket enthalten -> ganzes Paket überspringen
printf("DEBUG: Assertion. Empty PES packet (after stripping) found!");
        PSBuffer_DropCurBuffer(PSBuffer);
        TRACEEXIT;
        return;
      }
    }

    //Puffer mit den abfragbaren Daten markieren
    PSBuffer->ValidBuffer = (PSBuffer->ValidBuffer % 2) + 1;  // 0 und 2 -> 1, 1 -> 2

    PSBuffer->ValidBufLen = PSBuffer->BufferPtr;
    PSBuffer->ValidPayloadStart = PSBuffer->NewPayloadStart;
    PSBuffer->ValidDiscontinue = PSBuffer->NewDiscontinue;
//    PSBuffer->ValidPCR = PSBuffer->NewPCR;
//    PSBuffer->PSFileCtr++;

    //Neuen Puffer aktivieren
    switch(PSBuffer->ValidBuffer)
    {
      case 0:
      case 2: PSBuffer->pBuffer = PSBuffer->Buffer1; break;
      case 1: PSBuffer->pBuffer = PSBuffer->Buffer2; break;
    }

    PSBuffer->BufferPtr = 0;
    PSBuffer->NewPayloadStart = FALSE;
    PSBuffer->NewDiscontinue = FALSE;
//    PSBuffer->NewPCR = 0;
  }

  if (ResetContinuity)
  {
    PSBuffer->LastCCCounter = 255;
    PSBuffer->NewDiscontinue = TRUE;
  }
  PSBuffer->StartImmediate = !SkipFirstIncomplete;
  TRACEEXIT;
}

void PSBuffer_ProcessTSPacket(tPSBuffer *PSBuffer, tTSPacket *Packet, long long FilePosition)
{
  int                   RemainingBytes = 0, Start = 0;
  bool                  PESStart = Packet->Payload_Unit_Start;
  
  TRACEENTER;

  //Stimmt die PID?
  if((Packet->SyncByte == 'G') && ((Packet->PID1 *256) + Packet->PID2 == PSBuffer->PID) && Packet->Payload_Exists)
  {
    //Continuity Counter ok? Falls nicht Buffer komplett verwerfen
    if((PSBuffer->LastCCCounter != 255) && (Packet->ContinuityCount != ((PSBuffer->LastCCCounter + 1) % 16)))
    {
      //Unerwarteter Continuity Counter, Daten verwerfen
      if(PSBuffer->LastCCCounter != 255)
      {
        printf("  PESProcessor: TS continuity mismatch (PID=%hd, pos=%lld, expect=%hhu, found=%hhu)\n", PSBuffer->PID, FilePosition, ((PSBuffer->LastCCCounter + 1) % 16), Packet->ContinuityCount);
        if (PSBuffer->PID == VideoPID)
          AddContinuityError(PSBuffer->PID, FilePosition, ((PSBuffer->LastCCCounter + 1) % 16), Packet->ContinuityCount);
        if (PSBuffer->IgnoreContErrors)
          PSBuffer_StartNewBuffer(PSBuffer, FALSE, TRUE);
        else
          PSBuffer_DropCurBuffer(PSBuffer);
        PSBuffer->NewDiscontinue = TRUE;
      }
    }

    //Adaptation field gibt es nur bei PES Paketen
    if(Packet->Adapt_Field_Exists)
    {
      if(Packet->Data[0] > 183)
      {
        printf("  PESProcessor: Illegal Adaptation field length (%hu) on PID %hd\n", Packet->Data[0], PSBuffer->PID);
        PSBuffer->ErrorFlag = TRUE;
        Start = 184;
      }
      else
        Start += (1 + Packet->Data[0]);  // CW (Längen-Byte zählt ja auch noch mit!)

      // Liegt ein DiscontinueFlag vor?
      if ((Packet->Data[0] > 0) && ((Packet->Data[1] & 0x80) > 0))
      {
        printf("  PESProcessor: TS discontinuity flag (PID=%hd, pos=%lld)\n", PSBuffer->PID, FilePosition);
        if (PSBuffer->IgnoreContErrors)
          PSBuffer_StartNewBuffer(PSBuffer, FALSE, TRUE);
        else
          PSBuffer_DropCurBuffer(PSBuffer);
        PSBuffer->NewDiscontinue = TRUE;
      }
    }

    //Startet ein neues PES-Paket?
    if(Packet->Payload_Unit_Start)
    {
      // PES-Packet oder Table?
      if (PSBuffer->TablePacket)
      {
        if (Start < 184)
        {
          RemainingBytes = Packet->Data[Start];
          Start++;
        }
      }
      else
      {
//        GetPCR((byte*)Packet, &PSBuffer->NewPCR);
        for (RemainingBytes = 0; RemainingBytes < 184-Start-2; RemainingBytes++)
          if (Packet->Data[Start+RemainingBytes]==0 && Packet->Data[Start+RemainingBytes+1]==0 && Packet->Data[Start+RemainingBytes+2]==1)
            break;

        if (RemainingBytes == 184-Start-2)
        {
          if (Packet->Data[Start+RemainingBytes+1] == 0)
          {
            if (Packet->Data[Start+RemainingBytes] != 0)  RemainingBytes++;
          }
          else
            RemainingBytes += 2;
        }
      }

      if (RemainingBytes < 184 - Start)
      {
        if(PSBuffer->BufferPtr != 0)
        {
          //Restliche Bytes umkopieren
          if(RemainingBytes != 0)
          {
            if(PSBuffer->BufferPtr + RemainingBytes <= PSBuffer->BufferSize)
            {
              memcpy(PSBuffer->pBuffer, &Packet->Data[Start], RemainingBytes);
              PSBuffer->pBuffer += RemainingBytes;
              PSBuffer->BufferPtr += RemainingBytes;
            }
            else
            {
              printf("  PESProcessor: PS buffer overflow while parsing PID %hd\n", PSBuffer->PID);  // Beim Analysieren die maximale Länge des Puffers berücksichtigen (nicht darüberhinaus lesen)!!!
              PSBuffer->ErrorFlag = TRUE;
            }
          }
        }

#ifdef _DEBUG
        if(PSBuffer->BufferPtr > PSBuffer->maxPESLen)
          PSBuffer->maxPESLen = PSBuffer->BufferPtr;
#endif

        // Neuen Buffer beginnen
        PSBuffer_StartNewBuffer(PSBuffer, FALSE, FALSE);
      }
      else
      {
        printf("  PESProcessor: TS packet is marked as Payload Start, but does not contain a start code (PID=%hd, pos=%lld)\n", PSBuffer->PID, FilePosition);
        PESStart = FALSE;
        RemainingBytes = 0;
      }
    }

    if(PESStart || PSBuffer->StartImmediate)
    {
      // Überspringe Video-Pakete bis Sequence Header Code
//        if (!isSDVideo || PSBuffer->StartImmediate || Packet->Data[Start+RemainingBytes+3] == 0xB3)
//        {        
        //Erste Daten kopieren
        memset(PSBuffer->pBuffer, 0, PSBuffer->BufferSize);
        memcpy(PSBuffer->pBuffer, &Packet->Data[Start+RemainingBytes], 184-Start-RemainingBytes);
        PSBuffer->curSectionLen = ((tTSTableHeader*)(PSBuffer->pBuffer))->SectionLen1 * 256 + ((tTSTableHeader*)(PSBuffer->pBuffer))->SectionLen2;
        PSBuffer->pBuffer += (184-Start-RemainingBytes);
        PSBuffer->BufferPtr += (184-Start-RemainingBytes);
        PSBuffer->NewPayloadStart = PESStart;
        PSBuffer->StartImmediate = FALSE;
//        }
    }
    else
    {
      //Weiterkopieren
      if(PSBuffer->BufferPtr != 0)
      {
        if(PSBuffer->BufferPtr + 184-Start <= PSBuffer->BufferSize)
        {
          memcpy(PSBuffer->pBuffer, &Packet->Data[Start], 184-Start);
          PSBuffer->pBuffer += (184-Start);
          PSBuffer->BufferPtr += (184-Start);
        }
        else
        {
          printf("  PESProcessor: PS buffer overflow while parsing PID %hd\n", PSBuffer->PID);  // Beim Analysieren die maximale Länge des Puffers berücksichtigen (nicht darüberhinaus lesen)!!!
          PSBuffer->ErrorFlag = TRUE;
        }
      }
    }

    // Wenn Table-Packet und komplette Länge des Pakets ausgelesen -> dann schon jetzt als valid markieren
    if (PSBuffer->TablePacket && (PSBuffer->BufferPtr >= PSBuffer->curSectionLen + 3))
    {
      //Puffer mit den abfragbaren Daten markieren
      PSBuffer->ValidBuffer = (PSBuffer->ValidBuffer % 2) + 1;  // 0 und 2 -> 1, 1 -> 2
      PSBuffer->ValidBufLen = PSBuffer->curSectionLen + 3;

      //Neuen Puffer aktivieren
      switch(PSBuffer->ValidBuffer)
      {
        case 0:
        case 2: PSBuffer->pBuffer = PSBuffer->Buffer1; break;
        case 1: PSBuffer->pBuffer = PSBuffer->Buffer2; break;
      }
      PSBuffer->BufferPtr = 0;
    }
    PSBuffer->LastCCCounter = Packet->ContinuityCount;
  }
  TRACEEXIT;
}


//---------- PES to TS muxer -----------

void PESMuxer_Init(byte *PESBuffer, word PID, bool pPayloadStart, bool pDiscontinuity)
{
  dword CurTimeStep = TimeStepPerFrame;
  TRACEENTER;

  p = PESBuffer;
  TSPID = PID;
  FirstPacket = TRUE;
  PayloadUnitStart = pPayloadStart;
  DiscontinuityFlag = pDiscontinuity;
  if (DiscontinuityFlag)
  {
    ContCtr = 0;
    LastDTS = 0;
  }

  if (pPayloadStart)
  {
    if (LastDTS)
      CurPCR = (long long)LastDTS * 600 - PCRTOPTSOFFSET;
    GetPTS2(&PESBuffer[3], NULL, &LastDTS);

if (!TimeStepPerFrame)
{  
  if (CurPCR && LastDTS)
  {
    TimeStepPerFrame = (dword)((long long)LastDTS * 600 - PCRTOPTSOFFSET - CurPCR);
    CurTimeStep = TimeStepPerFrame;
    printf("  PESMuxer: Detected frame rate: %g frames per second.\n", (float)1000 / (TimeStepPerFrame/27000));
  }
  else if (VideoFPS != 0)
    CurTimeStep = (dword)(27000000.0 / VideoFPS);
  else
    CurTimeStep = (isHDVideo ? 540000 : 1080000);   // 540000 PCR = 20 ms = 1 Frame bei 50 fps
}

    if (!CurPCR && LastDTS)
      CurPCR = (long long)LastDTS * 600 - PCRTOPTSOFFSET - CurTimeStep;
    if(CurPCR < 0) CurPCR += 2576980377600LL;  // falls Überlauf von LastDTS (= 2^32 * 600)
  }
  else
    CurPCR = 0;
  TRACEEXIT;
}

void PESMuxer_StartNewFile(void)
{
  TRACEENTER;
  ContCtr = 0;
  LastDTS = 0;
  FirstRun = TRUE;
  TimeStepPerFrame = 0;
  TRACEEXIT;
}

bool PESMuxer_NextTSPacket(tTSPacket *const outPacket, int *const PESBufLen)
{
  int                   len = *PESBufLen;
  int                   PayloadBytes, PayloadStart;
  TRACEENTER;

  if (!outPacket && len)
  {
    TRACEEXIT;
    return FALSE;
  }
  
  if (FirstPacket)
  {
    memset(outPacket, 0, 12);
    outPacket->SyncByte = 'G';
    outPacket->PID1 = TSPID / 256;
    outPacket->PID2 = (TSPID & 0xff);
    outPacket->Payload_Exists = TRUE;
    outPacket->Payload_Unit_Start = PayloadUnitStart;
    FirstPacket = FALSE;
  }
  else
    outPacket->Payload_Unit_Start = FALSE;

  if (CurPCR || DiscontinuityFlag)
  {
    if (DiscontinuityFlag && !FirstRun)
    {
      outPacket->Adapt_Field_Exists = TRUE;
      outPacket->Data[0] = 1;
      outPacket->Data[1] = 0x80;
    }
    if (CurPCR)
    {
      outPacket->Adapt_Field_Exists = TRUE;
      outPacket->Data[0] = 7;  // Adaptation Field Length
      SetPCR((byte*)outPacket, CurPCR);
    }
    DiscontinuityFlag = FALSE;
    CurPCR = 0;
  }
  else
    outPacket->Adapt_Field_Exists = FALSE;

  outPacket->ContinuityCount = (ContCtr++) % 16;

  PayloadStart = (outPacket->Adapt_Field_Exists) ? (1 + outPacket->Data[0]) : 0;
  PayloadBytes = 184 - PayloadStart;

  if (len < PayloadBytes)
  {
    // Stuffing Bytes im Adaptation Field
    int FillBytes, FillStart;

    if (outPacket->Adapt_Field_Exists)
    {
      FillBytes = PayloadBytes - len;
      FillStart = 1 + outPacket->Data[0];
      outPacket->Data[0] += FillBytes;
      PayloadStart = 1 + outPacket->Data[0];  //  <==>  PayloadStart += FillBytes
    }
    else
    {
      outPacket->Adapt_Field_Exists = TRUE;
      FillBytes = PayloadBytes - len - 1;
      FillStart = 1;
      outPacket->Data[0] = FillBytes;
      PayloadStart = 1 + outPacket->Data[0];  //  <==>  PayloadStart = (1 + FillBytes)
    }

    if ((FillStart == 1) && (FillBytes > 0))
    {
      outPacket->Data[1] = 0;
      FillStart++;
      FillBytes--;
    }
    memset(&outPacket->Data[FillStart], 0xff, FillBytes);
    PayloadBytes = len;
  }
  memcpy(&outPacket->Data[PayloadStart], p, PayloadBytes);

  p += PayloadBytes;
  *PESBufLen -= PayloadBytes;  
  FirstRun = FALSE;

  TRACEEXIT;
  return TRUE;
}
