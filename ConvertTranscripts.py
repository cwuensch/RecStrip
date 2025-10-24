# ConvertTranscripts
# Konvertiert TurboScribe Transkriptionen in srt mit Sprechern
# Variante 1: csv nach srt konvertieren
#
# (c) 2025 Christian Wünsch
#

import sys, csv

# Farb-Array für die Sprecher, beginnend ab Speaker 2
color_array = [
  "#ffff00",  # gelb
  "#00ffff",  # cyan
  "#80ff00",  # grün
  "#ff00ff",  # magenta
  "#ff0000",  # rot
  "#0080ff",  # blau
  "#ff8000",  # orange
  "#cccccc"   # grau
]

def colorize(text, speaker):
  """Stellt einen Text in der Farbe des Sprechers dar."""
  if (speaker and speaker.startswith("Sprecher ")):
    speaker = int(speaker[9:])
    if (speaker >= 2):
      color = color_array[(speaker-2) % len(color_array)]
      text = f'<font color="{color}">{text}</font>'
  return text      

def ms_to_timestr(ms):
  """Konvertiert Millisekunden in das Format hh:mm:ss,xxx."""
  hours, remainder = divmod(ms // 1000, 3600)
  minutes, seconds = divmod(remainder, 60)
  milliseconds = ms % 1000
  return f"{hours:02}:{minutes:02}:{seconds:02},{milliseconds:03}"

def split_at_space(text, pos):
  """Teilt einen String um die Position pos am nächsten Leerzeichen auf"""
  if (pos >= len(text) or ' ' not in text):
    return (text, "")

  left = text.rfind(' ', 0, pos)
  right = text.find(' ', pos)

  if (left > 0 and right > 0):
    pos = left if (pos-left <= right-pos) else right
  elif (left > 0):
    pos = left
  elif (right > 0):
    pos = right
  else:
    return (text, "")

  return (text[:pos], text[pos+1:])


def csv_to_srt(csv_name, srt_name):
  """
  Konvertiert eine .csv-Datei im TurboScribe-Format in das srt-Format.
  Sehr kurze Captions werden zu einer zweizeiligen zusammengefasst, sehr lange auf mehrere aufgeteilt. 
  """
  n = 1
  with open(csv_name, 'r', encoding='utf-8') as csv_in, open(srt_name, 'w', newline='\r\n', encoding='utf-8') as srt_out:
    csvreader = csv.DictReader(csv_in, delimiter=',')
    last_row = None

    for row in csvreader:
      if (last_row):
        if ( (len(last_row["text"]) <= 25) and (len(row["text"]) <= 25) and (int(row["start"])-int(last_row["end"]) < 1000) and ((int(last_row["end"])-int(last_row["start"]) < 500) or (int(row["end"])-int(row["start"]) < 500)) ):
          start = int(last_row["start"])
          stop = int(row["end"])
          text = colorize(last_row["text"].strip(), last_row["speaker"]) + "\n" + colorize(row["text"].strip(), row["speaker"])
          srt_out.write(f"{n}\n{ms_to_timestr(start)} --> {ms_to_timestr(stop)}\n{text}\n\n")
          n = n + 1
          last_row = None
        else:
          start = int(last_row["start"])
          stop = int(last_row["end"])
          text = last_row["text"].strip()
          nr_parts = len(text) // 80 + 1  # Wenn die 80 um x überschritten wird -> ein Part mehr
          part_time = max((stop - start - nr_parts) // nr_parts, 1)

          while (nr_parts > 1 and text != ""):
            part_len = len(text) // nr_parts
            (part, text) = split_at_space(text, part_len)
            part = colorize(part, last_row["speaker"])
            cur_stop = max(stop, start + 1) if (text == "") else start + part_time
            srt_out.write(f"{n}\n{ms_to_timestr(start)} --> {ms_to_timestr(cur_stop)}\n{part}\n\n")
            start = start + part_time + 1
            nr_parts = nr_parts - 1
            n = n + 1
          stop = max(stop, start + 1)

          if (text != ""):
            text = colorize(text, last_row["speaker"])
            srt_out.write(f"{n}\n{ms_to_timestr(start)} --> {ms_to_timestr(stop)}\n{text}\n\n")
            n = n + 1
          last_row = row
      else:
        last_row = row

    if (last_row):
      start = int(last_row["start"])
      stop = int(last_row["end"])
      text = last_row["text"].strip()
      nr_parts = len(text) // 80 + 1  # Wenn die 80 um x überschritten wird -> ein Part mehr
      part_time = max((stop - start - nr_parts) // nr_parts, 1)

      while (nr_parts > 1 and text != ""):
        part_len = len(text) // nr_parts
        (part, text) = split_at_space(text, part_len)
        part = colorize(part, last_row["speaker"])
        cur_stop = max(stop, start + 1) if (text == "") else start + part_time
        srt_out.write(f"{n}\n{ms_to_timestr(start)} --> {ms_to_timestr(cur_stop)}\n{part}\n\n")
        start = start + part_time + 1
        nr_parts = nr_parts - 1
        n = n + 1
      stop = max(stop, start + 1)

      if (text != ""):
        text = colorize(text, last_row["speaker"])
        srt_out.write(f"{n}\n{ms_to_timestr(start)} --> {ms_to_timestr(stop)}\n{text}\n\n")


# --------------------------------------------
# Main Script
# --------------------------------------------

if (len(sys.argv) >= 3):
  print('Converting: "' + sys.argv[1] + '" -> "' + sys.argv[2] + '"')
  csv_to_srt(sys.argv[1], sys.argv[2])
else:
  print("Aufruf: " + sys.argv[0] + " <input_csv> <output_srt>")

print("")
