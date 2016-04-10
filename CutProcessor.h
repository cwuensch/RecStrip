#ifndef __CUTPROCESSORH__
#define __CUTPROCESSORH__


typedef struct
{
  byte                  Version;
  unsigned long long    RecFileSize;
  dword                 NrSegmentMarker;
  dword                 ActiveSegment;
}__attribute__((packed)) tCutHeader1;

typedef struct
{
  word                  Version;
  unsigned long long    RecFileSize;
  word                  NrSegmentMarker;
  word                  ActiveSegment;
  word                  Padding;
}__attribute__((packed)) tCutHeader2;


void GetCutNameFromRec(const char *RecFileName, char *const OutCutFileName);
bool CutFileLoad(const char *AbsCutName);
bool CutFileClose(const char* AbsCutName, bool Save);

#endif
