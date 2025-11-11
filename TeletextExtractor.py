# TeletextExtractor
# Displays all extracted teletext files in a folder, page-wise with color and mosaic arts.
# Use with argument -q to make a tsv of the selected description page for each rec.
# python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-1 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-2 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-3 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-4 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-5 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Humax-6 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Medion-1 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Medion-2 & python C:\Topfield\RecStrip_28\TeletextExtractor.py -q Medion-3
#
# (c) 2024-2025 Christian Wünsch
#
#

import os, sys, glob, csv, re, gzip

try:
    # windows
    import msvcrt
except ImportError:
    msvcrt = None
    import errno
    import fcntl
    import os
    import sys
    import termios
    import tty

colors = {
  '●':30, '◐':31, '◑':32, '◒':33, '◓':34, '◔':35, '◕':36, '○':37,
  '▣':40, '▤':41, '▥':42, '▦':43, '▧':44, '▨':45, '▩':46, '□':47
}

def getch():
  """
  Read a key press and return the result. Nothing is echoed to the console.
  Note that on Windows a special function key press will return its keycode. `Control-C` cannot be read there.
  On Unices pressing a special key will return its escape sequence.
  """
  if msvcrt:
    char = msvcrt.getch()
    if char in (b'\000', b'\xe0'):
      # special key -> need a second call to get keycode
      char = msvcrt.getch()
    return char
  else:
    stdin = sys.stdin.fileno()
    old = termios.tcgetattr(stdin)
    try:
      tty.setraw(stdin)
      char = sys.stdin.read(1)
    finally:
      termios.tcsetattr(stdin, termios.TCSADRAIN, old)
      old = fcntl.fcntl(stdin, fcntl.F_GETFL)
    try:
      fcntl.fcntl(stdin, fcntl.F_SETFL, old | os.O_NONBLOCK)
      try:
        char += sys.stdin.read()
      except [IOError, e]:
        if e.errno == errno.EAGAIN:
          pass  # there was no more than one char to read
        else:
          raise
    finally:
      fcntl.fcntl(stdin, fcntl.F_SETFL, old)
    return char

# define our clear function
def clear():
  if os.name == "nt":
    _ = os.system("cls")    # for windows
  else:
    _ = os.system("clear")  # for mac and linux (os.name is "posix")


# opens a text file either directly or via unzip
def opener(file_path, open_mode, new_line):
  if open_mode == 'r':
    if (not os.path.isfile(file_path) and not os.path.isfile(file_path + '.gz')):
      raise IOError("Error, cannot locate file: " + file_path)
  if file_path.endswith('.ttx.gz') or file_path.endswith('.txt.gz'):
    return gzip.open(file_path, open_mode + 't', encoding="utf-8")
  elif ((file_path.endswith('.ttx') or file_path.endswith('.txt')) and os.path.isfile(file_path)):
    return open(file_path, open_mode, encoding="utf-8")
  elif ((file_path.endswith('.ttx') or file_path.endswith('.txt')) and os.path.isfile(file_path + '.gz')):
    return gzip.open(file_path + '.gz', open_mode + 't', encoding="utf-8")
  else:
    raise IOError("Error, unknown file extension: " + file_path)


def mytrim2(line):
  # TODO: Wenn ASCII-Art entfernt wurde, dann sollten die normalen str.lstrip und str.rstrip Funktionen ausreichen
  while (len(line) > 0 and not str.isalnum(line[0]) and not line[0] in "[('\"&.-+=*<>"):
    line = line[1:]
  while (len(line) >= 2 and not str.isalnum(line[-1]) and not line[-1] in ",.:;-+=*/%&'\"()[]<>°€$!?" and not str.isalnum(line[-2]) and not line[-2] in ",.:;-+=*/%&'\"()[]<>°€$!?"):
    line = line[:-1]
  return(line)

def mytrim(line):
  line = list(line)
  for i, c in enumerate(line):
    if (c < ' ' or c >= '⍰'):
      line[i] = ' '
  line = "".join(line)

  while (len(line) > 0 and line[0] in "⍰ "):
    line = line[1:]
  while (len(line) >= 1 and line[-1] in "⍰ "):
    line = line[:-1]
#  line = str.lstrip(line)
#  line = str.rstrip(line)
  return(line)

def replace_colors(line, colorize_output):
  hold_mosaic = False
  last_c = ' '

  if (colorize_output):
    for old, new in colors.items():
      line = line.replace(old, "%s\033[%dm%s" % ((old if (new<40) else ''), new, (old if (new>=40) else ''))) + "\033[0m"

  line = list(line)
  for i, c in enumerate(line):
    if (c == '◆'):
      hold_mosaic = True
    elif (c == '◇'):
      hold_mosaic = False

    if (c in colors.keys() or c in "◆◇"):
      line[i] = last_c if (hold_mosaic) else ' '
    elif (ord(c) >= 0x2588):
#      c = ' '        # CW: hier wird die Mosaic ASCII-Art deaktiviert
#      line[i] = ' '  # CW: hier wird die Mosaic ASCII-Art deaktiviert
      last_c = c
    elif (c.isalpha() and c != 'm'):
      last_c = ' '

  return("".join(line))

# Funktion zur Text-Extraktion einer Teletext-Seite
def extract_text(page, filename, page_nr, sub_nr, comment):
  trimmed_page = []
  out_page = []
  result = {}
  line_buf = ""

  # 1. Schritt: Zeilen trimmen, tokenisieren und von Navigations-Zeilen befreien
  for i, line in enumerate(page):
    line = line.replace("  VPS  ", " VPS ")
#    line = line.replace(" bis ", "  bis ")
    line = line.replace("tag  ", "tag, ")
    line = line.replace("woch  ", "woch, ")

    if (i >= 2 and ("   " in line or "●" in line or "◐" in line or "◑" in line or "◒" in line or "◓" in line or "◔" in line or "◕" in line or "○" in line)):
      parts = re.split("   |●|◐|◑|◒|◓|◔|◕|○", line)
      for nr, part in enumerate(parts):
        part = mytrim(part)
        if (len(part) > 0):
          if (not re.search("((\.\.\.)|(>>?)) ?\d{3}", part) and not re.search("^ *(▧○)?\d{3} ?<<?", line) and not ("VPS" in part and len(part) <= 10) and not ("Übersicht" in line and len(line) <= 13)):
            trimmed_page.append(("!" if (nr % 2 > 0) else "") + part)
    else:
      trimmed_page.append(mytrim(line))

  # 2. Schritt: Zeilenumbrüche entfernen und getrennte Wörter zusammensetzen
  i = 0
  textstart = False
  while (i < len(trimmed_page)):
    line = trimmed_page[i]
    next_line = trimmed_page[i+1] if (i+1 < len(trimmed_page)) else ""
    getrennt = False
    same_col = True

    if (len(line) > 1 and (line != ">> ")):
      if (line[-1] == " "):
        line = line[:-1]
      if (line[-1] == "-" and line[-2] != " " and (not next_line[0].isupper() or (next_line[0] == "!" and not next_line[1].isupper()))):
        line = line[:-1]
        getrennt = True
      else:
        line = line + " "

      if (len(line_buf) > 1 and not textstart):
        out_page.append("")
        textstart = True

      if (line and line_buf and (line[0] == "!") and (line_buf[0] == "!")):
        line = line[1:]
      line_buf = line_buf + line

      if (line_buf and next_line):
        same_col = ((line_buf[0] == "!" and next_line[0] == "!") or (line_buf[0] != "!" and next_line[0] != "!"))

      if (not getrennt and (i <= 2 or not same_col or (len(line) <= 20) or (len(next_line) <= 1) or (len(next_line.split(" ")[0]) < 38-len(line)) or not any(map(str.islower, line)) or "16:9" in next_line)):
        out_page.append(str.rstrip(line_buf))
        line_buf = ""
    i = i + 1
  if (len(line_buf) > 0):
    out_page.append(str.rstrip(line_buf))

  # 3. Schritt: Besondere Zeilen identifizieren
  result["filename"] = filename
  result["page"] = page_nr
  result["subpage"] = sub_nr
  result["comment"] = comment

  result["dauer"] = ""
  result["titel"] = ""
  result["subtitel"] = ""
  result["descs"] = []
  result["text"] = []

  result["datum"] = ""  # mytrim(out_page[1][-18:-9])
  result["zeit"] = ""   # mytrim(out_page[1][-9:])

  result["now"] = mytrim(out_page[1][-18:-9]) + ", " + mytrim(out_page[1][-9:])
  out_page[0] = "[NOW]   " + result["now"]

  result["sender"] = mytrim(out_page[1][12:-18])
  out_page[1] = "[SENDR] " + result["sender"]

  i = 2
  breakfound = False
  while (i < len(out_page)):
    line = out_page[i]
    if (line == ""):
      breakfound = True
    if (breakfound and result["titel"] != ""):
      break
    if (re.search("\d{1,2}\. ?(\d{1,2}\.|(Jan|Feb|Mär|Apr|Mai|Jun|Jul|Aug|Sep|Okt|Nov|Dez))", line)):
      if (result["datum"] == ""):
        result["datum"] = line
      out_page[i] = "[DATUM] " + line
    elif ((re.search("\d{1,2}[\.:]\d{1,2}", line) or "Uhr" in line) and not "16:9" in line):
      if (result["zeit"] == ""):
        result["zeit"] = line
      out_page[i] = "[ZEIT]  " + line
    elif (re.search("\d{1,3} ?(min|MIN|Minuten)", line) and len(line) < 10):
      if (result["dauer"] == ""):
        result["dauer"] = line
        out_page[i] = "[DAUER] " + line
    elif ("Fernsehen" in line or "FERNSEHEN" in line or line.startswith("arte") or line.startswith("WDR") or line.startswith("NDR") or line.startswith("SWR") or line.startswith("rbb") or line.startswith("ZDF") or line.startswith("ARD")):
      if (result["sender"] == ""):
        result["sender"] = line
        out_page[i] = "[SENDR] " + line
    elif ((result["titel"] == "" or not any(map(str.islower, line))) and (result["datum"] != "" or result["zeit"] != "" or len(line) > 15) and len(line) >= 8 and len(set(line)) >= 4 and not line.startswith("!1D1") and not line.startswith("1D1") and not line.startswith("VPS")):
      result["titel"] = line
      out_page[i] = "[TITEL] " + line
    elif (result["titel"] != "" and len(line) >= 8 and not line.startswith("1D1") and not line.startswith("VPS")):
      if (result["subtitel"] == ""):
        result["subtitel"] = line
      else:
        result["descs"].append(line)
      out_page[i] = "[DESC]  " + line
    i = i + 1

  while (i < len(out_page)):
    line = out_page[i]
    if (line != ""):
      result["text"].append(line)
    i = i + 1

  result["text"] = "\r".join(result["text"])
  return(result, out_page)


# Funktion zur Verarbeitung einer eingelesenen Text-Seite
def process_page(all_pages, new_text):
  page_nr = new_text[0][1:4]
  if (page_nr not in all_pages):
    all_pages[page_nr] = {}

  subpage_nr = 0
  if (new_text[1] and new_text[1][3] == "|"):
    if (new_text[1][4:7].isnumeric()):
      subpage_nr = int(new_text[1][4:7])

  # feste Unterseite -> ist noch leer
  if ((subpage_nr > 0) and (subpage_nr not in all_pages[page_nr] or (not all_pages[page_nr][subpage_nr]) or len(all_pages[page_nr][subpage_nr]) == 0)):
    all_pages[page_nr][subpage_nr] = new_text
    return

  # vorhandene Subpage oder es gibt schon eine Hauptseite
  if ((subpage_nr > 0) or (0 in all_pages[page_nr])):
    for sub, old_text in (all_pages[page_nr].items() if subpage_nr>0 else enumerate(all_pages[page_nr][0])):

      # falls feste Unterseite -> Schleifeninhalt nur 1x ausführen
      if (subpage_nr > 0):
        old_text = all_pages[page_nr][subpage_nr]

      nr_diff = sum( sum( (old_text[i][j] != new_text[i][j] and old_text[i][j] > ' ' and new_text[i][j] > ' ') for j in range(0, min(len(old_text[i]), len(new_text[i]))) ) for i in range(2, len(old_text)) )
      nr_same = sum( sum( (old_text[i][j] == new_text[i][j]) for j in range(0, min(len(old_text[i]), len(new_text[i]))) ) for i in range(1, len(old_text)) )
      nr_missing_ref = sum( old_text[i].count('⍰') for i in range(1, len(old_text)) )
      nr_missing_new = sum( new_text[i].count('⍰') for i in range(1, len(new_text)) )
      nr_unique_ref  = sum( sum( (old_text[i][j] > ' ' and new_text[i][j] <= ' ') for j in range(0, min(len(old_text[i]), len(new_text[i]))) ) + max(len(old_text[i])-len(new_text[i]), 0) for i in range(2, len(old_text)) )
      nr_unique_new  = sum( sum( (new_text[i][j] > ' ' and old_text[i][j] <= ' ') for j in range(0, min(len(old_text[i]), len(new_text[i]))) ) + max(len(new_text[i])-len(old_text[i]), 0) for i in range(2, len(new_text)) )


      # Wenn ref = new oder ref Teilmenge von new oder umgekehrt
      if (nr_same > 40 and nr_diff < 40 and not (nr_unique_ref > 20 and nr_unique_new > 20)):
        # Fix missing chars if page is duplicate
        if ((nr_missing_ref > 0 or nr_missing_new > 0) and nr_diff > 0):
          for i in range(0, len(old_text)):
            old = list(old_text[i])
            neu = list(new_text[i])
            for j in range(0, min(len(old), len(neu))):
              if (  old[j] == '⍰' and (j==0 or old[j-1] == neu[j-1]) and (j==39 or old[j+1] == neu[j+1]) ):
                old[j] = neu[j]
              elif( neu[j] == '⍰' and (j==0 or old[j-1] == neu[j-1]) and (j==39 or old[j+1] == neu[j+1]) ):
                neu[j] = old[j]
            old_text[i] = "".join(old)
            new_text[i] = "".join(neu)

        # ref und new stimmen überein
        if ((nr_unique_new > nr_unique_ref + 40) or (nr_missing_new < nr_missing_ref and nr_unique_new + 10 >= nr_unique_ref) or (nr_unique_new > nr_unique_ref and nr_missing_new == nr_missing_ref)):
          if (subpage_nr == 0):
            subpage_nr = sub
            all_pages[page_nr][subpage_nr] = new_text
        else:
          if (subpage_nr == 0):
            subpage_nr = sub
            all_pages[page_nr][subpage_nr] = old_text
        new_text = None
        break

      # feste Unterseite oder wenig Differenz
      if (subpage_nr > 0 or nr_diff <= 40):
        if (subpage_nr == 0):
          all_pages[page_nr][0][sub] = new_text
        else:
          all_pages[page_nr][subpage_nr] = new_text
        new_text = None
        break

  if (new_text != None):
    # Falls Hauptseite und keine ähnliche gefunden -> neu anhängen
    if (subpage_nr == 0):
      if (0 not in all_pages[page_nr] or (not isinstance(all_pages[page_nr][0], list))):
        all_pages[page_nr][0] = []
      all_pages[page_nr][0].append(new_text)


# Funktion zum (ergänzenden) Einlesen einer Teletext-Datei
def load_teletext(all_pages, file_text):
  new_text = None
  page_start = False
  line_nr = 0

  if (len(file_text) == 0):
    return(all_pages)

  if (file_text[-1] == '\n'):
    file_text = file_text[:-1]

  for line in (file_text + '\n----------------------------------------\n[000]').split("\n"):
    if (page_start and line.startswith("[")):
      if (new_text and len(new_text) > 1):
        process_page(all_pages, new_text)
      new_text = []

    elif (page_start and new_text != None):
      new_text.append("")
    page_start = False

    if (line == "----------------------------------------"):
      page_start = True
    else:
      page_start = False
      if (new_text != None):
        new_text.append(line)

  for i, p in all_pages.items():
    new_page = {}
    if (0 in p and len(p[0]) > 0):
      new_page = { ((j/10) if j>0 else 0):s for j,s in enumerate(p[0]) }
    for j, s in sorted(p.items()):
      if (j > 0):
        new_page[j] = s

#    if (0 in p and len(p[0]) > 1):
#      for j, s in new_page.items():
#        s[0] = "%s (+%d)" % (s[0][:11], len(p[0]))
    all_pages[i] = new_page

  return(all_pages)

# Funktion zum Anzeigen des Inhalts einer Teletext-Seite
def get_page(all_pages, page_nr):
#  print(f"DEBUG: get_page   (all_pages[{len(all_pages)}], page_nr={page_nr})")
  if (page_nr in all_pages):
    return(all_pages[page_nr])
  else:
    return({})

def get_subpage(page, sub_nr):
#  print(f"DEBUG: get_subpage(page[{len(page)}], sub_nr={sub_nr})")
  if (page != None and len(page) > 0):
    if (sub_nr == -1):
      return (len(page) - 1)
    elif (sub_nr >= len(page)):
      return(0)
    else:
      return(sub_nr)
  return(-2)

def print_page(all_pages, page_nr, sub_nr, do_extract, searchstr, file_names, comment):
#  print(f"DEBUG: print_page (all_pages[{len(all_pages)}], page_nr={page_nr}, sub_nr={sub_nr}, do_extract={do_extract})")
  page = get_page(all_pages, page_nr)
  subpages = list(page.keys())
  result = None

  sub_nr = get_subpage(page, sub_nr)
  if (sub_nr >= 0):
    if (do_extract):
      result, out_page = extract_text(page[subpages[sub_nr]], file_names, page_nr, sub_nr, comment)
      str = "\n".join(out_page)
    else:
      str = map(lambda x: replace_colors(x, True), page[subpages[sub_nr]])
      str = "\n".join(str)
    if (searchstr == ""):
      print(str)
      if (do_extract):
        print("")
    else:
      print(re.sub(searchstr, "\033[92m" + searchstr + "\033[0m", replace_colors(str, False), flags=re.IGNORECASE))
  else:
    print (f"no data ({page_nr})")
  return(sub_nr, result)

# Funktion zur Durchsuchung aller Teletext-Seiten nach gegebenem Suchbegriff
def do_search(all_pages, searchstr):
  searchstr_low = searchstr.lower()
  for page_nr, page in all_pages.items():
    for sub_nr, subpage in page.items():
      page_found = False
      for line_nr, line in enumerate(subpage):
        if (searchstr_low in replace_colors(line.lower(), False)):
          if (not page_found):
            print(subpage[0])
          print(replace_colors(subpage[line_nr-1], False))
          print(re.sub(searchstr, "\033[92m" + searchstr + "\033[0m", replace_colors(line, False), flags=re.IGNORECASE))
          if (line_nr+1 < len(subpage)):
            print(replace_colors(subpage[line_nr+1], False))
          page_found = True
      if (page_found):
        print()

# Funktion zum (Zwischen-)Speichern des aktuellen Ergebnisses
def do_save(outlist, first_write):
  with open("output.txt", "a", newline="", encoding="utf-8") as out_file:
    writer = csv.writer(out_file, delimiter='\t', quotechar='"', quoting=csv.QUOTE_MINIMAL, lineterminator='\r\n')
    if (first_write):
      writer.writerow(["filename", "page", "subpage", "comment"])
      first_write = False
    writer.writerows(outlist)
  print ("\n\nSaved to output.txt.")
  outlist = []

# Funktion zum Speichern der vollständigen Extraktions-Tabelle
def save_table(outtable):
  with open("output2.tsv", "a", newline="", encoding="utf-8") as out_file:
    writer = csv.DictWriter(out_file, delimiter='\t', quotechar='"', quoting=csv.QUOTE_MINIMAL, lineterminator='\r\n', fieldnames=["filename", "page", "subpage", "now", "sender", "datum", "zeit", "dauer", "titel", "subtitel", "desc1", "desc2", "desc3", "desc4", "desc5", "desc6", "desc7", "desc8", "desc9", "text", "comment"], extrasaction='ignore')
    writer.writeheader()
    for row in outtable:
      if (row):
        for i in range(1, len(row["descs"])):
          row["desc" + str(i)] = row["descs"][i-1]
        writer.writerow(row)
  print ("\n\nSaved to output2.csv.")


# Hauptprogramm mit "GUI" und Bedienungsschleife
def main():
  outlist = []
  outtable = []
  inlist = {}

  folder = "."
  quietmode = 0
  if (len(sys.argv) > 1):
    if (sys.argv[1] == "-q"):
      quietmode = 1
      if (len(sys.argv) > 2):
        argument = sys.argv[2]
    else:
      argument = sys.argv[1]
    if (argument.endswith(".ttx") or argument.endswith(".txt") or argument.endswith(".ttx.gz") or argument.endswith(".txt.gz")):
      if ("/" in argument):
        folder = argument.split("/")[0]
      elif (os.sep in argument):
        folder = argument.split(os.sep)[0]
    else:
      folder = argument

  clear()
  try:
    with open(folder + ".txt", "r", encoding="utf-8") as in_file:
      reader = csv.DictReader(in_file, delimiter="\t", quotechar='"')
      for row in reader:
        for name in row["filename"].split(".ttx, "):
#          if (name.endswith(".ttx")):
#            name = name[:-4]
          if (not name.endswith(".ttx")):
            name = name + ".ttx"
          inlist[name] = row
  except FileNotFoundError:
    print("Input page list file '" + folder + ".txt' not found.")


  # Get file list
  all_files = []
  if (argument.endswith(".ttx") or argument.endswith(".txt") or argument.endswith(".ttx.gz") or argument.endswith(".txt.gz")):
    if ("/" in argument or os.sep in argument):
      argument.removeprefix(folder + "/")
      argument.removeprefix(folder + os.sep)
      argument.removeprefix("rec/")
      argument.removeprefix("rec" + os.sep)
    all_files = [argument]
  else:
    all_files = glob.glob("*.ttx*", root_dir=folder + "/rec")
    if (len(all_files) == 0):
      all_files = glob.glob("*.ttx*", root_dir=folder)
      all_files.extend(glob.glob("*/*.ttx*", root_dir=folder))
#    all_files = glob.glob("**/*.ttx*", root_dir=folder, recursive=False)
    else:
      folder = folder + "/rec"

#  all_files.sort(key=lambda x: os.path.getsize(x), reverse=True)
  all_files = [x.removesuffix(".gz") if x.endswith(".gz") else x for x in all_files]

  if (len(all_files[0]) > 15 and all_files[0][-15] == "_"):  # Humax
    all_files.sort(key=lambda x: x.split("_")[0][:-1] + "_" + x.split("_")[1][:6])
  elif (all_files[0].endswith("_video.ttx")):  # Medion orig
    all_files.sort(key=lambda x: x.split("]")[0] + "]" + x.split("]")[1].split("-")[0])
  elif (all_files[0].endswith("].ttx")):  # Medion rec
    all_files.sort(key=lambda x: x.split("[")[0].split("-")[0] + " [" + x.split("[")[1])
  else:  # other
    all_files.sort(key=lambda x: x)


  # Go through all teletext files in folder
  first_write = True
  mode = quietmode
  i = 0

  while (i < len(all_files)):
    all_pages = {}
    cur_name = all_files[i]

    if cur_name.endswith(".ttx"):
      file_mask = cur_name
      file_mask2 = ""
      if (re.search("_\d{10}.ttx", cur_name)):
        file_mask = cur_name[:-16]
        file_mask2 = cur_name[-15:-8]
      elif (re.search(".+ \[\d{4}-\d{2}-\d{2}\]\.ttx", cur_name)):
        file_mask = cur_name[:-17]
        file_mask2 = cur_name[-16:]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]
      elif (re.search("\[\d{4}-\d{2}-\d{2}\] .+_video\.ttx", cur_name)):
        file_mask = cur_name[:-10]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]
#      elif (re.search(".+ \[\d{2}\.\d{2}\.\d{4}\]\.ttx", cur_name)):
#        file_mask = cur_name[:-17]
#        file_mask2 = cur_name[-16:]
      elif (re.search(".+\(Cut-\d{1,2}\)\.ttx", cur_name)):
        file_mask = cur_name[:-12]
        while (file_mask.endswith(" ")):
          file_mask = file_mask[:-1]

      j = i
      while (j > 0 and all_files[j-1].startswith(file_mask) and file_mask2 in all_files[j-1]):
        j = j - 1
      while (i < len(all_files)-1 and all_files[i+1].startswith(file_mask) and file_mask2 in all_files[i+1]):
        i = i + 1

      file_names = ""
      cur_name = all_files[j]

      for k in range(j, i+1):
        name = all_files[k+1 if (k < i) else j]
        file_names = file_names + (", " if (file_names != "") else "") + name
        file_path = os.path.join(folder, name)

        print(f'DEBUG: Open file "{file_path}"')
        with opener(file_path, "r", "\r\n") as file:
          all_pages = load_teletext(all_pages, file.read())

      # Sort subpages
#      for page_nr, page in all_pages.items():
#        all_pages[page_nr] = sorted(page.items())

      # Read previously generated page associations
      if (cur_name in inlist):
        page = inlist[cur_name]["page"]
        subpage = int(inlist[cur_name]["subpage"])
        comment = inlist[cur_name]["comment"]
      else:
        page = "100"
        last_page = get_page(all_pages, "333")

        if (last_page != None and (1 in last_page) and str.rstrip(last_page[1][2])=="" and str.rstrip(last_page[1][3])=="" and str.rstrip(last_page[1][4])==""):
          page = "333"
        else:
          last_page = get_page(all_pages, "368")  # arte
          if (last_page != None and (1 in last_page) and str.rstrip(last_page[1][2])=="" and str.rstrip(last_page[1][3])=="" and str.rstrip(last_page[1][4])==""):
            page = "368"
          else:
            last_page = get_page(all_pages, "300")  # terranova
            if (last_page != None and len(last_page) > 0):
              page = "300"

        if (page == "333" or page == "368"):
          for line in last_page[1]:
            x = re.search("[.> ](\d{3}) ?$", str.rstrip(line))
            if (x):
              page = x.group(1)
              break
        subpage = 0
        comment = ""

      # Convert subpage number to index
      subpages = []
      cur_page = get_page(all_pages, page)
      if (cur_page != None and len(cur_page) > 0):
        subpages = list(cur_page.keys())
        subpage = next((key for key, value in enumerate(subpages) if value == subpage), 0)

      searchstr = ""
      answer = ""

      # GUI
      while (True):
#        clear()
        print("----------------------------------------")
        print("\033[1m" + cur_name + "\033[0m")
        print("----------------------------------------")
        if (answer == b'f'):
          do_search(all_pages, searchstr)
        else:
          subpage, result = print_page(all_pages, page, subpage, (mode==1), searchstr, file_names, comment)
          if (quietmode):
            outtable.append(result)

        print("----------------------------------------")
        if (comment):
          print("\033[43;30m > Kommentar: " + comment + "\033[0m")

        print(f"Seitenzahl eingeben, oder <>, [Leer], [Enter], [z]urück / [f]inden / [c]omment / [e]dit / [s]peichern / [q]uit: ", end="", flush=True)

        answer = b'\r' if (quietmode) else ""
        newpage = ""
        searchstr = ""
        while (answer=='' or answer in [b'0', b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'\x08']):
          if (answer == b'\x08'):  # Backspace
            newpage = newpage[:-1]
            print("\b", end="", flush=True)
          elif (answer != ""):
            newpage = newpage + str(answer, encoding="utf-8")
            print(str(answer, encoding="utf-8"), end="", flush=True)
          if (len(newpage) >= 3):
            answer = b'\r'
          else:
            answer = getch()

        if (answer == b'\x03'):  # Strg + C
          return

        elif (answer == b'q'):
          break

        elif (answer == b'f'):
          searchstr = input("\nSuche nach: ")

        elif (answer == b'c'):
          comment = input("\nKommentar eingeben: ")

        elif (answer == b'e'):
          os.system("notepad output.txt")

        elif (answer == b's'):
          do_save(outlist, first_write)

        clear()

        if (answer == b' '):
          mode = 1 if (mode==0) else 0

        elif (answer == b'K'):  # Arrow left
          subpage = subpage - 1
        elif (answer == b'M'):  # Arrow right
          subpage = subpage + 1

        elif (answer == b'H'):  # Arrow up
          if (int(page) < 999):
            page = str(int(page) + 1)
          if (int(page) < 100):
            page = "100"
          subpage = 0
        elif (answer == b'P'):  # Arrow down
          if (int(page) >= 100):
            page = str(int(page) - 1)
          if (int(page) < 100):
            page = "0"
          subpage = 0

        elif (answer == b'z' and i > 0):
          i = i - 2
          break

        elif (answer == b'\r' and newpage != ""):
          page = newpage if (newpage != "0" and newpage != "000") else "100"
          subpage = 0

        elif (answer == b'\r' or answer == b'n' or answer == b'c'):
          if (answer == b'n'):
            page = "0"
            subpage = 0
          if (page != "0" and page in all_pages):
            subpages = list(all_pages[page].keys())
          outlist.append([file_names, page, subpages[subpage] if (page != "0" and subpage >= 0) else 0, comment])
          break

      if (answer == b'q'):
        break
    i = i + 1

  if (quietmode):
    save_table(outtable)
  else:
    do_save(outlist, first_write)

if __name__ == "__main__":
  main()
