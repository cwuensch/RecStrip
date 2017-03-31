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


extern int              OutCutVersion;
extern bool             WriteCutInf;

void GetCutNameFromRec(const char *RecFileName, char *const OutCutFileName);
bool CutProcessor_Init(void);
bool CutFileLoad(const char *AbsCutName);
bool CutFileSave(const char* AbsCutName);
void CutProcessor_Free(void);

#endif
