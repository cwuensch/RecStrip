#ifndef __HUMAXHEADERH__
#define __HUMAXHEADERH__

/*
 Das Format
 ----------
 - Humax-Header (u.a. Dateiname) befinden sich am Ende jedes 32768 Byte (0x8000) Blocks
   und sind jeweils 1184 Bytes lang.
     
 - Der erste Header beginnt also bei 0x7B60, der zweite bei 0xFB60, usw.
 - Es gibt 3 Sorten von Headern:
   1. Header-Block: Allgemeiner Header THumaxHeader (enthält z.B. die PIDs, Datum und Zeit der Aufnahme usw.)
   3. Header-Block: der Bookmark-Header THumaxHeader_Bookmarks
   4. Header-Block: der Tonspur-Header THumaxHeader_Tonspuren
   alle anderen Header-Blöcke: können mit dem allgemeinen Header beschrieben werden

 - Ich habe diese 3 Header-Typen aufgeteilt in folgende Blöcke:
   (a) THumaxBlock_allg           -> allgemeiner Block (PIDs, Datum, Zeit, Dateiname, usw.)
                                     der Teil ist in allen Headern gleich
                                     Beachte: bei Änderung des Dateinamens oder des Schutzes wird nur der 1. Header aktualisiert! In den anderen bleibt die alte Info stehen.
   (b) ZusInfoID As Integer       -> gibt an, um welche Art von Header es sich handelt
   (c) ZusInfos(1 To 404) As Byte -> das ist der zusätzliche Info-Block, der sich je nach Header-Typ unterscheidet
                                     da stehen dann z.B. die Bookmarks oder Tonspurinfos drin
   (d) THumaxBlock_Ende           -> in diesem Teil stehen keine relevanten Informationen mehr
*/

#include "type.h"

#define HumaxHeaderIntervall    0x8000           // nach diesem Intervall wird der Header wiederholt
#define HumaxHeaderLaenge       0x4A0            // die Länge eines jeden Headers
#define HumaxHeaderAnfang       0xFD04417F       // das erste DWORD eines jeden Headers
#define HumaxHeaderAnfang2      0x78123456       // die DWORDs 2-9 eines jeden Headers

typedef enum
{
  HumaxBookmarksID = 0x5514,
  HumaxTonSpurenID = 0x4823
} THumaxZusInfoIDs;

typedef struct
{
  word                  PID;
  char                  Name[6];                 // meist kürzer, nullterminiert
}__attribute__((packed)) THumaxTonSpur;

typedef struct
{
  dword Anfang;              // dient zur Identifikation des Headers (?)
  dword Anfang2[8];          // 8 Mal Wiederholung von "V4.x"
  word VideoPID;
  word AudioPID;
  word TeletextPID;
  byte Leer1[10];            // 10 Bytes leer
  dword Konstante;           // scheint immer 0x40201 zu sein
  byte Leer2[8];             // 8 Bytes meist(?) leer
  dword Unbekannt1;
  dword Datum;               // Anzahl der Tage seit 17.11.1858 -> Unix = (MJD-40587)*86400
  dword Zeit;                // Anzahl der Minuten seit 00:00
  dword Dauer;               // Aufnahmedauer in Sekunden
  byte Schreibschutz;        // 1 (geschützt) - 0 (nicht geschützt)
  byte Unbekannt2[15];
  char Dateiname[32];        // evtl. kürzer, nullterminiert
}__attribute__((packed)) THumaxBlock_allg;

typedef struct
{
  byte Unbekannt3[620];
  byte Ende[30];             // vielleicht zur Markierung des Endes (-> eher nicht!)
}__attribute__((packed)) THumaxBlock_Ende;


typedef struct
{
  word Anzahl;               // (vermutlich ist die Anzahl ein Long, aber zur Sicherheit...)
  word Leer;
  dword Items[100];
}__attribute__((packed)) THumaxBlock_Bookmarks;

typedef struct
{
  word Anzahl;
  word Leer;
  THumaxTonSpur Items[50];
}__attribute__((packed)) THumaxBlock_Tonspuren;


typedef struct
{
  THumaxBlock_allg Allgemein;  // allgemeiner Block (Dateiname und Schreibschutz nur im 1. Header aktuell!)
  word ZusInfoID;              // ID des ZusatzInfo-Blocks
  byte ZusInfos[404];          // z.B. 3. Header: Bookmarks, 4. Header: Tonspuren
  THumaxBlock_Ende Ende;
}__attribute__((packed)) THumaxHeader;



typedef struct
{
  byte TableID;
  word SectionLen1:4;
  word Reserved1:2;
  word Reserved0:1;
  word SectionSyntax:1;
  word SectionLen2:8;
  word TS_ID1:8;
  word TS_ID2:8;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved11:2;
  byte SectionNr;
  byte LastSection;
//  for i = 0 to N  {
    word ProgramNr1:8;
    word ProgramNr2:8;
    word PMTPID1:5;  // oder NetworkPID, falls ProgramNr==0
    word Reserved111:3;
    word PMTPID2:8;  // oder NetworkPID, falls ProgramNr==0
//  }
  dword CRC32;
}__attribute__((packed)) TTSPAT;


typedef struct
{
  byte stream_type;
  word ESPID1:5;
  word ReservedZ:3;
  word ESPID2:8;
  word ESInfoLen1:4;
  word ReservedQ:4;
  word ESInfoLen2:8;
}__attribute__((packed)) TElemStream;

typedef struct
{
  byte TableID;
  word SectionLen1:4;
  word Reserved1:2;
  word Reserved0:1;
  word SectionSyntax:1;
  word SectionLen2:8;
  word ProgramNr1:8;
  word ProgramNr2:8;
  byte CurNextInd:1;
  byte VersionNr:5;
  byte Reserved11:2;
  byte SectionNr;
  byte LastSection;

  word PCRPID1:5;
  word ReservedX:3;
  word PCRPID2:8;

  word ProgInfoLen1:4;
  word ReservedY:4;
  word ProgInfoLen2:8;
}__attribute__((packed)) TTSPMT;


extern char PATPMTBuf[];


bool LoadHumaxHeader(FILE *fIn, TYPE_RecHeader_TMSS *RecInf);

#endif
