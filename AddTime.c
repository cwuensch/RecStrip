static dword AddTime(dword pvrDate, int addMinutes);
dword AddTime(dword pvrDate, int addMinutes)  //add minutes to the day
{
  word                  day;
  short                 hour, min;

  day = (pvrDate >> 16) & 0xffff;
  hour= (pvrDate >> 8) & 0xff;
  min = (pvrDate) & 0xff;

  min += addMinutes % 60;
  if(min < 0)
  {
    hour-=1;
    min+=60;
  }
  else if(min > 59)
  {
    hour+=1;
    min-=60;
  }

  hour += addMinutes / 60;

  if(hour < 0)
  {
    day-=1;
    hour+=24;
  }
  else
  {
    while(hour > 23)
    {
      day+=1;
      hour-=24;
    }
  }

  return ((day<<16)|(hour<<8)|min);
}
