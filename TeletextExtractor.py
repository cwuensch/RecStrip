import os, sys, glob, csv, re

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


def mytrim2(line):
  # TODO: Wenn ASCII-Art entfernt wurde, dann sollten die normalen str.lstrip und str.rstrip Funktionen ausreichen
  while (len(line) > 0 and not str.isalnum(line[0]) and not line[0] in "[('\"&.-+=*<>"):
    line = line[1:]
  while (len(line) >= 2 and not str.isalnum(line[-1]) and not line[-1] in ",.:;-+=*/%&'\"()[]<>°€$!?" and not str.isalnum(line[-2]) and not line[-2] in ",.:;-+=*/%&'\"()[]<>°€$!?"):
    line = line[:-1]
  return(line)

def mytrim(line):
  line = str.lstrip(line)
  line = str.rstrip(line)
  return(line)

# Funktion zur Text-Extraktion einer Teletext-Seite
def extract_text(page):
  trimmed_page = []
  out_page = []
  line_buf = ""

  # 1. Schritt: Zeilen trimmen, tokenisieren und von Navigations-Zeilen befreien
  for line in page:
    line = mytrim(line)
    line = line.replace("  VPS  ", " VPS ")
#    line = line.replace(" bis ", "  bis ")
    line = line.replace("tag  ", "tag, ")
    line = line.replace("woch  ", "woch, ")

    if (re.search("(\.\.\.)|(>>?) ?\d{3}", line) or re.search("^ *\d{3} ?<<?", line) or ("Übersicht" in line and len(line) <= 13)):
      continue
    elif ("  " in line):
      parts = line.split("  ")
      for part in parts:
        part = mytrim(part)
        if (len(part) > 0):
          trimmed_page.append(part)
    else:
      trimmed_page.append(line)

  # 2. Schritt: Zeilenumbrüche entfernen und getrennte Wörter zusammensetzen
  i = 0
  textstart = False
  while (i < len(trimmed_page)):
    line = trimmed_page[i]
    next_line = trimmed_page[i+1] if (i+1 < len(trimmed_page)) else ""
    getrennt = False

    if (len(line) > 1 and (line != ">> ")):
      if (line[-1] == " "):
        line = line[:-1]
      if (line[-1] == "-" and line[-2] != " "):
        line = line[:-1]
        getrennt = True
      else:
        line = line + " "

      if (len(line_buf) > 1 and not textstart):
        out_page.append("")
        textstart = True
      line_buf = line_buf + line

      if (((len(line) <= 20) or (len(next_line) <= 1) or (len(next_line.split(" ")[0]) < 38-len(line)) or not any(map(str.islower, line)) or "16:9" in next_line) and not getrennt):
        out_page.append(line_buf)
        line_buf = ""
    i = i + 1
  if (len(line_buf) > 0):
    out_page.append(line_buf)

  # 3. Schritt: Besondere Zeilen identifizieren
  datum = ""
  zeit  = ""
  dauer = ""
  titel = ""
  subtitel = ""
  sender = ""
  i = 0
  while (i < len(out_page)):
    line = out_page[i]
    if (line == ""):
      break
    if (re.search("\d{1,2}\. ?(\d{1,2}\.|(Jan|Feb|Mär|Apr|Mai|Jun|Jul|Aug|Sep|Okt|Nov|Dez))", line)):
      if (datum == ""):
        datum = line
      out_page[i] = "[DATUM] " + line
    elif ((re.search("\d{1,2}[\.:]\d{1,2}", line) or "Uhr" in line) and not "16:9" in line):
      if (zeit == ""):
        zeit = line
      out_page[i] = "[ZEIT]  " + line
    elif (re.search("\d{1,3} ?(min|MIN|Minuten)", line) and len(line) < 10):
      if (dauer == ""):
        dauer = line
        out_page[i] = "[DAUER] " + line
    elif ("Fernsehen" in line or "FERNSEHEN" in line or line.startswith("arte") or line.startswith("WDR") or line.startswith("NDR") or line.startswith("SWR") or line.startswith("rbb") or line.startswith("ZDF") or line.startswith("ARD")):
      if (sender == ""):
        sender = line
        out_page[i] = "[SENDR] " + line
    elif ((titel == "" or not any(map(str.islower, line))) and (datum != "" or len(line) >= 15) and len(line) >= 8 and len(set(line)) >= 4 and not line.startswith("1D1") and not line.startswith("VPS")):
      titel = line
      out_page[i] = "[TITEL] " + line
    elif (titel != "" and len(line) >= 8 and not line.startswith("1D1") and not line.startswith("VPS")):
      subtitel = line
      out_page[i] = "[DESC]  " + line
    i = i + 1
  return(out_page)


# Funktion zum (ergänzenden) Einlesen einer Teletext-Datei
def load_teletext(content, file_text):
  new_text = None
  page_start = False
  line_nr = 0

  if (len(file_text) == 0):
    return(content)

  for line in (file_text + '\n----------------------------------------').split("\n"):
    if (page_start and line.startswith("[")):
      page_nr = line[1:4]
      if (page_nr not in content):
        content[page_nr] = {}
      new_text = []
    else:
      page_start = False

    if (line == "----------------------------------------"):
      if (new_text):
        # Fix missing characters / take page with less spaces
        subpage_nr = 0
        if (new_text[1][3] == "|"):
          subpage_nr = int(new_text[1][4:7])
        else:
          for sub, old_text in content[page_nr].items():
            nr_differences = sum( sum( old_text[i][j] != new_text[i][j] for j in range(1, len(new_text[i])) ) for i in range(0, len(new_text)) )
            if (nr_differences <= 40):
              subpage_nr = sub
              break
        if (subpage_nr == 0):
          subpage_nr = len(content[page_nr]) + 1

        if(subpage_nr not in content[page_nr]):
          content[page_nr][subpage_nr] = []
        old_text = content[page_nr][subpage_nr]

        if (old_text):
          nr_differences = sum( sum( old_text[i][j] != new_text[i][j] for j in range(1, len(new_text[i])) ) for i in range(0, len(new_text)) )
          if (nr_differences <= 40):
            for i in range (1, len(new_text)):
              for j in range(0, 40):
                if (old_text[i][j] == '█' and (j == 0 or old_text[i][j-1]==new_text[i][j-1]) and (j == 39 or old_text[i][j+1]==new_text[i][j+1])):
                  old_text[i][j] = new_text[i][j]
                if (new_text[i][j] == '█' and (j == 0 or old_text[i][j-1]==new_text[i][j-1]) and (j == 39 or old_text[i][j+1]==new_text[i][j+1])):
                  new_text[i][j] = old_text[i][j]

        nr_spaces_ref = sum( old_text[i].count(' ') for i in range(1, len(old_text)) )
        nr_spaces_new = sum( new_text[i].count(' ') for i in range(1, len(new_text)) )

        if (len(old_text) == 0 or nr_spaces_new > nr_spaces_ref):
          if (new_text[1][3] != "|"):
            content[page_nr][subpage_nr] = new_text
        else:
          content[page_nr][subpage_nr] = old_text

      new_text = None
      cur_text = None
      page_start = True
    else:
      page_start = False
      if (new_text != None):
        new_text.append(line)
  return(content)

# Funktion zum Anzeigen des Inhalts einer Teletext-Seite
def get_page(content, page_nr):
#  print(f"DEBUG: get_page   (content[{len(content)}], page_nr={page_nr})")
  if (page_nr in content):
    return(content[page_nr])
  else:
    return({})

def get_subpage(page, sub_nr):
#  print(f"DEBUG: get_subpage(page[{len(page)}], sub_nr={sub_nr})")
  if (page != None and len(page) > 0):
    if (sub_nr == 0):
      return (len(page))
    elif (sub_nr > len(page)):    
      return(1)
    else:
      return(sub_nr)
  return(0)

def print_page(content, page_nr, sub_nr, do_extract, searchstr):
#  print(f"DEBUG: print_page (content[{len(content)}], page_nr={page_nr}, sub_nr={sub_nr}, do_extract={do_extract})")
  page = get_page(content, page_nr)

  sub_nr = get_subpage(page, sub_nr)
  if (sub_nr > 0):
    if (do_extract):
      str = "\n".join(extract_text(page[sub_nr]))
    else:
      str = "\n".join(page[sub_nr])
    if (searchstr == ""):
      print(str)
    else:
      print(re.sub(searchstr, "\033[92m" + searchstr + "\033[0m", str, flags=re.IGNORECASE))
  else:
    print (f"no data ({page_nr})")
  return(sub_nr)

# Funktion zur Durchsuchung aller Teletext-Seiten nach gegebenem Suchbegriff
def do_search(content, searchstr):
  searchstr_low = searchstr.lower()
  for page_nr, page in content.items():
    for sub_nr, subpage in page.items():
      page_found = False
      for line_nr, line in enumerate(subpage):
        if (searchstr_low in line.lower()):
          if (not page_found):
            print(subpage[0])
          print(subpage[line_nr-1])
          print(re.sub(searchstr, "\033[92m" + searchstr + "\033[0m", line, flags=re.IGNORECASE))
          print(subpage[line_nr+1])
          page_found = True
      if (page_found):
        print()

# Funktion zum (Zwischen-)Speichern des aktuellen Ergebnisses
def do_save(outlist, first_write):
  with open("output.txt", "a", encoding="utf-8") as out_file:
    writer = csv.writer(out_file, delimiter="\t", quotechar='"', quoting=csv.QUOTE_MINIMAL, lineterminator="\r\n")
    if (first_write):
      writer.writerow(["filename", "page", "subpage", "comment"])
      first_write = False
    writer.writerows(outlist)          
  print ("\n\nSaved to output.txt.")
  outlist = []


# Hauptprogramm mit "GUI" und Bedienungsschleife
def main():
  outlist = []
  inlist = {}

  folder = "."
  if (len(sys.argv) > 1):
    folder = sys.argv[1]

  clear()
  try:
    with open(folder + ".txt", "r", encoding="utf-8") as in_file:
      reader = csv.DictReader(in_file, delimiter="\t", quotechar='"')
      for row in reader:
        for name in row["filename"].split(".txt, "):
          if (not name.endswith(".txt")):
            name = name + ".txt"
          inlist[name] = row
  except FileNotFoundError:
    print("Input page list file '" + folder + ".txt' not found.")

  all_files = os.listdir(folder)
  all_files = list(filter(os.path.isfile, glob.glob(folder + "/*.txt")))
#  all_files.sort(key=lambda x: os.path.getsize(x), reverse=True)
  all_files = [os.path.basename(x) for x in all_files]

  if (all_files[0][-15] == "_"):  # Humax
    all_files.sort(key=lambda x: x.split("_")[0][:-1] + "_" + x.split("_")[1][:6])
  elif (all_files[0].endswith("_video.txt")):  # Medion orig
    all_files.sort(key=lambda x: x.split("]")[0] + "]" + x.split("]")[1].split("-")[0])
  elif (all_files[0].endswith("].txt")):  # Medion rec
    all_files.sort(key=lambda x: x.split("[")[0].split("-")[0] + " [" + x.split("[")[1])
  else:  # other
    all_files.sort(key=lambda x: x)

  first_write = True
  mode = 0
  i = 0

  while (i < len(all_files)):
    content = {}
    cur_name = all_files[i]

    if cur_name.endswith(".txt"):
      file_mask = cur_name
      file_mask2 = ""
      if (re.search("_\d{10}.txt", cur_name)):
        file_mask = cur_name[:-16]
        file_mask2 = cur_name[-15:-8]
      elif (re.search(".+ \[\d{4}-\d{2}-\d{2}\]\.txt", cur_name)):
        file_mask = cur_name[:-17]
        file_mask2 = cur_name[-16:]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]
      elif (re.search("\[\d{4}-\d{2}-\d{2}\] .+_video\.txt", cur_name)):
        file_mask = cur_name[:-10]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]

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

#        print(f'\nDEBUG: Open file "{file_path}"')
        with open(file_path, "r", encoding="utf-8") as file:
          content = load_teletext(content, file.read())

      if (cur_name in inlist):
        page = inlist[cur_name]["page"]
        subpage = int(inlist[cur_name]["subpage"])
        comment = inlist[cur_name]["comment"]
      else:
        page = "100"
        last_page = get_page(content, "333")
        if (last_page != None and len(last_page) > 0 and str.rstrip(last_page[1][1])=="" and str.rstrip(last_page[1][2])=="" and str.rstrip(last_page[1][3])==""):
          page = "333"
        else:
          last_page = get_page(content, "368")  # arte
          if (last_page != None and len(last_page) > 0 and str.rstrip(last_page[1][1])=="" and str.rstrip(last_page[1][2])=="" and str.rstrip(last_page[1][3])==""):
            page = "368"
          else:
            last_page = get_page(content, "300")  # terranova
            if (last_page != None and len(last_page) > 0):
              page = "300"

        if (page == "333" or page == "368"):
          for line in last_page[1]:
            x = re.search("[.> ](\d{3}) ?$", str.rstrip(line))
            if (x):
              page = x.group(1)
              break
        subpage = 1
        comment = ""

      searchstr = ""
      answer = ""

      while (True):
#        clear()
        print("----------------------------------------")
        print("\033[1m" + cur_name + "\033[0m")
        print("----------------------------------------")
        if (answer == b'f'):
          do_search(content, searchstr)
        else:
          subpage = print_page(content, page, subpage, (mode==1), searchstr)
          
        print("----------------------------------------")
        if (comment):
          print("\033[93m > Kommentar: " + comment + "\033[0m")

        print(f"Seitenzahl eingeben, oder <>, [Leer], [Enter], [z]urück / [f]inden / [c]omment / [e]dit / [s]peichern / [q]uit: ", end="", flush=True)

        answer = ""
        newpage = ""
        while (answer=='' or answer in [b'0', b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'\x08']):
          if (answer == b'\x08'):
            newpage = newpage[:-1]
            print("\b", end="", flush=True)
          elif (answer != ""):
            newpage = newpage + str(answer, encoding="utf-8")
            print(str(answer, encoding="utf-8"), end="", flush=True)
          if (len(newpage) >= 3):
            answer = b'\r'
          else:
            answer = getch()

        if (answer == b'\x03'):
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

        elif (answer == b'K'):
          subpage = subpage - 1
        elif (answer == b'M'):
          subpage = subpage + 1

        elif (answer == b'H'):
          page = str(int(page) + 1)
        elif (answer == b'P'):
          page = str(int(page) - 1)

        elif (answer == b'z' and i > 0):
          i = i - 2
          break

        elif (answer == b'\r' and newpage != ""):
          page = newpage if (newpage != "0" and newpage != "000") else "100"
          subpage = 1

        elif (answer == b'\r' or answer == b'n' or answer == b'c'):
          if (answer == b'n'):
            page = 0
            subpage = 0
          outlist.append([file_names, page, subpage, comment])
          break

      if (answer == b'q'):
        break
    i = i + 1

  do_save(outlist, first_write)

if __name__ == "__main__":
  main()
