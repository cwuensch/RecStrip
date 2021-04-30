# RecStrip for Topfield PVR v2.7
(C) 2016-2021 Christian Wuensch

based on Naludump 0.1.1 by Udo Richter -

based on MovieCutter 3.6 -

portions of Mpeg2cleaner (S. Poeschel), RebuildNav (Firebird) & TFTool (jkIT)


Usage:
------
 RecStrip <RecFile>           Scan the rec file and set Crypt- and RbN-Flag and
                              StartTime (seconds) in the source inf.
                              If source inf/nav not present, generate them new.

 RecStrip <InFile> <OutFile>  Create a copy of the input rec.
                              If a inf/nav/cut file exists, copy and adapt it.
                              If source inf is present, set Crypt and RbN-Flag
                              and reset ToBeStripped if successfully stripped.

Parameters:
-----------
  -n/-i:     Always generate a new nav/inf file from the rec.
             If no OutFile is specified, source nav/inf will be overwritten!

  -r:        Cut the recording according to cut-file. (if OutFile specified)
             Copies only the selected segments into the new rec.

  -c:        Copies each selected segment into a single new rec-file.
             The output files will be auto-named. OutFile is ignored.
             (instead of OutFile an output folder may be specified.)

  -a:        Append second, ... file to the first file. (file1 gets modified!)
             If combined with -r, only the selected segments are appended.
             If combined with -s, only the copied part will be stripped.

  -m:        Merge file2, file3, ... into a new file1. (file1 is created!)
             If combined with -s, all input files will be stripped.

  -s:        Strip the recording. (if OutFile specified)
             Removes unneeded filler packets. May be combined with -c, -r, -a.

  -ss:       Strip and skip. Same as -s, but skips already stripped files.

  -e:        Remove also the EPG data. (can be combined with -s)

  -t:        Remove also the teletext data. (can be combined with -s)
  -tt <page> Extract subtitles from teletext. (combine with -t to remove ttx)

  -x:        Remove packets marked as scrambled. (flag could be wrong!)

  -o1/-o2:   Change the packet size for output-rec: 
             1: PacketSize = 188 Bytes, 2: PacketSize = 192 Bytes.

  -v:        View rec information only. Disables any other option.

  -M:        Medion Mode: Multiplexes 4 separate PES-Files into output.
             (With InFile=<name>_video.pes, _audio1, _ttx, _epg are used.)

Examples:
---------
  RecStrip 'RecFile.rec'                     RebuildNav.

  RecStrip -s -e InFile.rec OutFile.rec      Strip recording.

  RecStrip -n -i -o2 InFile.ts OutFile.rec   Convert TS to Topfield rec.

  RecStrip -r -s -e -o1 InRec.rec OutMpg.ts  Strip & cut rec and convert to TS.
