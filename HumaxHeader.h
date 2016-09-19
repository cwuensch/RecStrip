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


Private Const HumaxHeaderIntervall = &H8000&  ' nach diesem Intervall wird der Header wiederholt
Private Const HumaxHeaderLaenge = &H4A0       ' die Länge eines jeden Headers
Private Const HumaxHeaderAnfang = &HFD04417F  ' das erste DWORD eines jeden Headers
Private Const HumaxHeaderAnfang2 = &H78123456 ' die DWORDs 2-9 eines jeden Headers
Private Const HumaxBasisDatum = #11/17/1858#

Private Enum THumaxZusInfoIDs
  HumaxBookmarksID = &H5514
  HumaxTonSpurenID = &H4823
End Enum


Private Type THumaxTonSpur
  PID As Integer
  Name As String * 6            ' meist kürzer, nullterminiert
End Type

Private Type THumaxBlock_allg
  Anfang As Long                ' dient zur Identifikation des Headers (?)
  Anfang2(1 To 8) As Long       ' 8 Mal Wiederholung von "V4.x"
  VideoPID As Integer
  AudioPID As Integer
  TeletextPID As Integer
  Leer1(1 To 10) As Byte        ' 10 Bytes leer
  Konstante As Long             ' scheint immer 0x40201 zu sein
  Leer2(1 To 8) As Byte         ' 8 Bytes meist(?) leer
  Unbekannt1 As Long
  Datum As Long                 ' Anzahl der Tage seit 17.11.1858
  Zeit As Long                  ' Anzahl der Minuten seit 00:00
  Dauer As Long                 ' Aufnahmedauer in Sekunden
  Schreibschutz As Byte         ' 1 (geschützt) - 0 (nicht geschützt)
  Unbekannt2(1 To 15) As Byte
  Dateiname As String * 32      ' evtl. kürzer, nullterminiert
End Type

Private Type THumaxBlock_Ende
  Unbekannt3(1 To 620) As Byte
  Ende(1 To 30) As Byte         ' vielleicht zur Markierug des Endes (-> eher nicht!)
End Type

Private Type THumaxBlock_Bookmarks
  Anzahl As Integer             ' (vermutlich ist die Anzahl ein Long, aber zur Sicherheit...)
  Leer As Integer
  Items(1 To 100) As Long
End Type

Private Type THumaxBlock_Tonspuren
  Anzahl As Integer
  Leer As Integer
  Items(1 To 50) As THumaxTonSpur
End Type


Private Type THumaxHeader
  Allgemein As THumaxBlock_allg ' allgemeiner Block (Dateiname und Schreibschutz nur im 1. Header aktuell!)
  ZusInfoID As Integer          ' ID des ZusatzInfo-Blocks
  ZusInfos(1 To 404) As Byte    ' z.B. 3. Header: Bookmarks, 4. Header: Tonspuren
  Ende As THumaxBlock_Ende
End Type

Private Type THumaxHeader_Bookmarks
  Allgemein As THumaxBlock_allg
  ZusInfoID As Integer          ' wenn Bookmarks vorhanden, ID=0x5514
  Bookmarks As THumaxBlock_Bookmarks
  Ende As THumaxBlock_Ende
End Type

Private Type THumaxHeader_Tonspuren
  Allgemein As THumaxBlock_allg
  ZusInfoID As Integer          ' wenn Tonspur-Block, ID=0x4823
  Tonspuren As THumaxBlock_Tonspuren
  Ende As THumaxBlock_Ende
End Type

#endif