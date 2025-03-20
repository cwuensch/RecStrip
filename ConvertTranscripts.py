import os
import csv

# Farb-Array für die Sprecher, beginnend ab Speaker 2
color_array = [
  "#ffff00",  # gelb
  "#00ffff",  # cyan
  "#80ff00",  # grün
  "#ff00ff",  # magenta
  "#ff0000"   # rot
  "#0080ff",  # blau
  "#ff8000",  # orange
  "#ffffff"   # weiß
]

def ms_to_srt_time(ms):
  """Konvertiert Millisekunden in das Format hh:mm:ss,xxx."""
  hours, remainder = divmod(ms // 1000, 3600)
  minutes, seconds = divmod(remainder, 60)
  milliseconds = ms % 1000
  return f"{hours:02}:{minutes:02}:{seconds:02},{milliseconds:03}"

def txt_to_srt(file_path):
  """Konvertiert eine .txt-Datei im CSV-Format in das SRT-Format und speichert sie mit "_auto" im Namen."""
  srt_content = []
  with open(file_path, 'r', encoding='utf-8') as file:
    reader = csv.reader(file)
    for i, row in enumerate(reader):
      start, stop, text, speaker = row
      start_time = ms_to_srt_time(int(start))
      stop_time = ms_to_srt_time(int(stop))

      # Text einfärben, wenn Speaker > 1
      if speaker.strip().lower() != "speaker 1":
        speaker_index = int(speaker.split()[-1]) - 2  # Speaker-Index für color_array
        color = color_array[speaker_index % len(color_array)]
        text = f'<font color="{color}">{text}</font>'

      # Zusammenstellen des SRT-Formats für diesen Eintrag
      srt_content.append(f"{i + 1}\r\n{start_time} --> {stop_time}\r\n{text}\r\n")

  # Erstellen des neuen Dateipfads mit "_auto" und .srt-Erweiterung
  base_name = os.path.splitext(file_path)[0]
  srt_output_path = f"{base_name}_auto.srt"
  with open(srt_output_path, "w", encoding="utf-8") as srt_file:
    srt_file.write("\r\n".join(srt_content))

def process_directory(directory):
  """Durchsucht einen Ordner rekursiv und konvertiert alle .txt-Dateien."""
  for root, _, files in os.walk(directory):
    for file in files:
      if file.endswith(".txt"):
        txt_file_path = os.path.join(root, file)
        txt_to_srt(txt_file_path)
        print(f"Konvertiert: {txt_file_path}")

# Verzeichnis angeben, das durchsucht werden soll
directory = "/Pfad/zum/Ordner"
process_directory(directory)
