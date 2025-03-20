' SrtTranslator.vbs
' =================
'
' Schritt 1: Übergebe eine srt-Datei, die auf _fra.srt oder _fra.sup endet
'            -> Nummer, TimeStamps und Farbcodes werden entfernt, als Input für Deepl abgespeichert
'
' Schritt 2: Übergebe eine Deepl Output-Datei (die auf _deeplout.srt endet)
'            -> Nummer, TimeStamps und Farbcodes werden aus der _fra.srt wiederhergestellt und in _deu.srt gespeichert
'
' Usage:
' ------
' SrtTranslator.vbs <Eingabe.srt>
'
' (c) 2025 Christian Wünsch


Option Explicit

Dim fso, objRegEx, objRegEx2, nameIn, nameRef, nameOut, fIn, fRef, fOut, mode, numbers, fonts, times, line, lastLine, outLine, skipline, matches, firstChr, lastChr, closeFont, p, n, i

Set fso = CreateObject("Scripting.FileSystemObject")
Set objRegEx = CreateObject("VBScript.RegExp")
Set objRegEx2 = CreateObject("VBScript.RegExp")

objRegEx.Pattern = "^\d+?:\d+?:\d+?,\d+ --> \d+?:\d+?:\d+?,\d+$"
objRegEx.global = True
objRegEx2.Pattern = "^(<font .+?>)(.*?)(</font>)?$"
objRegEx2.global = True


' Argumente verarbeiten
If (WScript.Arguments.Count >= 1) Then
  ' Quelle angegeben, Ziel berechnen
  nameIn = fso.GetFile(WScript.Arguments(0))
  nameOut = fso.GetBaseName(WScript.Arguments(0))
  If (Mid(nameOut, InstrRev(nameOut, "_") + 1) = "fra") Then
    mode = 1
    nameOut = fso.GetParentFolderName(WScript.Arguments(0)) & "\" & Left(nameOut, InstrRev(nameOut, "_")-1) & "_deeplin." & fso.GetExtensionName(WScript.Arguments(0))
  ElseIf (Mid(nameOut, InstrRev(nameOut, "_") + 1) = "deeplout") Then
    mode = 2
    nameRef = fso.GetParentFolderName(WScript.Arguments(0)) & "\" & Left(nameOut, InstrRev(nameOut, "_")-1) & "_fra." & fso.GetExtensionName(WScript.Arguments(0))
    nameOut = fso.GetParentFolderName(WScript.Arguments(0)) & "\" & Left(nameOut, InstrRev(nameOut, "_")-1) & "_ger." & fso.GetExtensionName(WScript.Arguments(0))
  Else
    MsgBox "Dateiname nicht erkannt!" & vbCrLf & "Bitte eine ""*_fra.srt"" oder ""*_deeplout.srt"" Datei übergeben!", 16, "SrtTranslator"
    WScript.Quit
  End If
Else
  MsgBox "Bitte eine ""*_fra.srt"" oder ""*_deeplout.srt"" Datei übergeben!", 64, "SrtTranslator"
  WScript.Quit
End If

MsgBox nameIn & vbcrlf & nameRef & vbcrlf & nameOut, , "SrtTranslator"


Function ExtractFont(line)
  Set matches = objRegEx2.Execute(line)
  If (matches.Count > 0) Then
    ExtractFont = matches.Item(0).SubMatches(0)
  Else
    ExtractFont = ""
  End If
End Function

Function RemoveFont(line)
  Set matches = objRegEx2.Execute(line)
  If (matches.Count > 0) Then _
    line = matches.Item(0).SubMatches(1)
  RemoveFont = line
End Function

Function RemoveFontEnd(line)
  If (Right(line, 7) = "</font>") Then _
    line = Left(line, Len(line) - 7)
  RemoveFontEnd = line
End Function


' Schritt 1: _fra.srt in _deeplin.srt konvertieren
If (mode = 1) Then
  Set fIn = fso.OpenTextFile(nameIn, 1)
  Set fOut = fso.CreateTextFile(nameOut, True)
Else
  Set fIn = fso.OpenTextFile(nameRef, 1)
End If

While Not fIn.AtEndOfStream
  line = fIn.ReadLine
  If (objRegEx.Test(line)) Then
    ' Wir haben einen TimeStamp (neuer Subtitle) gefunden
    If (outLine <> "") Then
      fonts = fonts & "|" & (ExtractFont(outLine))
      If (mode = 1) Then _
        fOut.WriteLine(RemoveFont(outLine) & vbCrLf)
    End If
    numbers = numbers & "|" & CInt(lastLine)
    times = times & "|" & line
    outLine = ""
    skipline = True
    If (mode = 1) Then _
      fOut.WriteLine(lastLine)
    n = n + 1
'    If (n <> CInt(lastLine)) Then MsgBox "Problem: Sprung in Nummer: " & lastLine & " -> " & n
  ElseIf (lastLine <> "" And Not skipline) Then
    ' Wir haben keinen TimeStamp und keine Nr gefunden und sind noch im Text
    If (outLine <> "") Then
      If (((Left(lastLine, 5) <> "<font") And (Left(outLine, 5) <> "<font")) Or (Left(lastLine, 22) = Left(outLine, 22))) Then
        ' Text hat immernoch gleiche Farbe
        outLine = outLine & " " & RemoveFont(lastLine)
      Else
        ' Textfarbe wechselt -> neue Zeile
        fonts = fonts & "|" & ExtractFont(outLine)
        If (mode = 1) Then _
          fOut.WriteLine(RemoveFont(outLine))
        outLine = RemoveFontEnd(lastLine)
      End If
    Else
      ' erste Zeile des Textes
      outLine = RemoveFontEnd(lastLine)
    End If
  Else
    skipline = False
  End If
  lastLine = line
Wend
If (outLine <> "") Then
  ' letzte Zeile noch ausgeben
  fonts = fonts & "|" & (ExtractFont(outLine))
  If (mode = 1) Then _
    fOut.WriteLine(RemoveFont(outLine))
End If
If (mode = 1) Then fOut.Close
fIn.Close

'MsgBox Mid(fonts, 2)
'MsgBox Mid(times, 2)
n = 0

'Schritt 2: _deeplout.srt in _ger.srt konvertieren
If (mode = 2) Then
  Set fIn = fso.OpenTextFile(nameIn, 1)
  Set fOut = fso.CreateTextFile(nameOut, True)
  numbers = Split(Mid(numbers, 2), "|")
  times = Split(Mid(times, 2), "|")
  fonts = Split(Mid(fonts, 2), "|")

  fOut.WriteLine(n + 1)
  fOut.WriteLine(times(n))
  n = n + 1

  line = fIn.ReadLine
  While Not fIn.AtEndOfStream
    line = fIn.ReadLine
    If (line <> "") Then
      ' Wir sind noch im Text -> Zeile ausgeben
      line = Replace(line, "„", """")
      line = Replace(line, "“", """")
      If (Left(line, 1) = ",") Then line = Mid(line, 2)
      If (Left(line, 1) = " ") Then line = Mid(line, 2)
      If (Left(firstChr, 1) <> Left(line, 1)) Then _
        line = firstChr & line
      If (closeFont > 0) Then _
        If (closeFont = 2) Then fOut.Write("</font>" & vbCrLf) Else fOut.Write(vbCrLf)

      If (len(line) >= 40) Then
        p = InStrRev(Left(line, 36), " ")
        outLine = Left(line, p-1)
        If (fonts(i) <> "") Then _
          outLine = fonts(i) & outLine & "</font>"
        fOut.WriteLine(outLine)
        line = Mid(line, p+1)
      End If

      closeFont = 1
      If (fonts(i) <> "") Then
        line = fonts(i) & line
        closeFont = 2
      End If
      fOut.Write(line)
      firstChr = ""
      lastChr = Right(line, 1)
      i = i + 1
    Else
      ' neuer Subtitle gestartet
      line = Trim(fIn.ReadLine)
      If (Len(line) > Len(CStr(CInt(line)))) Then
        If (Len(line) > Len(CStr(CInt(line))) + 1) Then
          firstChr = Mid(line, Len(CStr(CInt(line))) + 1)
        ElseIf (Mid(line, Len(CStr(CInt(line))) + 1, 1) <> Left(lastChr, 1)) Then
          fOut.Write(Mid(line, Len(CStr(CInt(line))) + 1))
        End If
      End If
      If (closeFont = 2) Then fOut.Write("</font>" & vbCrLf) Else fOut.Write(vbCrLf)
      closeFont = 0

      If (CInt(line) <> CInt(numbers(n))) Then MsgBox "Problem: Sprung in Nummer: " & numbers(n) & " -> " & line
      fOut.WriteLine("")
      fOut.WriteLine(CInt(line))
      fOut.WriteLine(times(n))
      lastChr = ""
      n = n + 1
    End If
  Wend
  If (closeFont > 0) Then _
    If (closeFont = 2) Then fOut.Write("</font>" & vbCrLf) Else fOut.Write(vbCrLf)
  fOut.WriteLine("")
  fOut.Close
  fIn.Close
End If

MsgBox "Konvertierung abgeschlossen.", 64, "SrtTranslator"
