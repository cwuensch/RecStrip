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


def mytrim(line):
  # TODO: Wenn ASCII-Art entfernt wurde, dann sollten die normalen str.lstrip und str.rstrip Funktionen ausreichen
  while (len(line) > 0 and not str.isalnum(line[0]) and not line[0] in "[('\"&.-+=*<>"):
    line = line[1:]
  while (len(line) >= 2 and not str.isalnum(line[-1]) and not line[-1] in ",.:;-+=*/%&'\"()[]<>°€$!?" and not str.isalnum(line[-2]) and not line[-2] in ",.:;-+=*/%&'\"()[]<>°€$!?"):
    line = line[:-1]
  return(line)

# Funktion zur Text-Extraktion einer Teletext-Seite
def extract_text(page):
  trimmed_page = []
  out_page = []
  line_buf = ""

  # 1. Schritt: Zeilen trimmen, tokenisieren und von "komischen" Zeichen befreien
  for line in page:
    line = mytrim(line)
    line = line.replace("  VPS  ", " VPS ")
    line = line.replace("tag  ", "tag, ")
    line = line.replace("woch  ", "woch, ")

    if (re.search("(\.\.\.)|(>>?) ?\d{3}", line) or re.search("^ *\d{3} ?<<?", line) or ("Übersicht" in line and len(line) <= 13)):
      continue
#    if (line=="xß " or line=="pp0p5p " or line.startswith("ööööööööööööööö") or line.startswith("sssssssssssssss")):
#      continue  # TODO: unnötig, wenn ASCII-Art entfernt wurde
    elif ("  " in line):
      parts = line.split("  ")
      for part in parts:
        part = mytrim(part)
        # TODO: Die folgenden 6 Zeilen können entfernt werden und in der 7.ten "> 0", wenn ASCII-Art entfernt wurde
#        if ("up5up5" in part or "up0up0" in part or "ö4ö4ö4" in part or "5555" in part or ".!+." in part or ((part.startswith("öl") or part.startswith("(ö")) and "ö" in part[2:])):
#          continue
#        for c in part:
#          if (ord(c) >= 127 and ord(c) <= 159):
#            part = ""
#            break
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
    elif (re.search("\d{1,2}[\.:]\d{1,2}", line) and not "16:9" in line):
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


# Funktion zum Lesen und Anzeigen des Inhalts einer Teletext-Seite
last_nr = 0
last_page = {}

def get_page(content, page_nr):
  global last_nr
  global last_page
  cur_page = "0"
  cur_subpage = 0
#  print(f"DEBUG: get_page   (content[{len(content)}], page_nr={page_nr})")

  if (last_nr != page_nr):
    last_nr = 0
    page_start = False
    if (len(content) == 0):
      return({})

    for line in content.split("\n"):
      if (page_start and line.startswith("[")):
        cur_page = line[1:4]
        cur_subpage = int(line[7]) if (len(line) > 5) else 1

        # beim ersten Fund der richtigen Seite
        if ((cur_page == page_nr) and (page_nr != last_nr)):
          last_nr = page_nr
          last_page = {}
        elif (cur_page > page_nr):
          break
        last_page[cur_subpage] = []
      else:
        page_start = False

      if (line == "----------------------------------------"):
        page_start = True
      else:
        page_start = False
        if (cur_subpage > 0):
          last_page[cur_subpage].append(line)

  if (last_nr == page_nr and last_page != None):
    return last_page
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

def print_page(content, page_nr, sub_nr, do_extract):
#  print(f"DEBUG: print_page (content[{len(content)}], page_nr={page_nr}, sub_nr={sub_nr}, do_extract={do_extract})")
  page = get_page(content, page_nr)

  sub_nr = get_subpage(page, sub_nr)
  if (sub_nr > 0):
    if (do_extract):
      print("\n".join(extract_text(page[sub_nr])))
    else:
      print("\n".join(page[sub_nr]))
  else:
    print (f"no data ({page_nr})")
  return(sub_nr)


# Funktion zur Durchsuchung aller Teletext-Seiten nach gegebenem Suchbegriff
def do_search(content, searchstr):
  page_start = False
  page_line = ""
  searchstr = searchstr.lower()

  for line in content.split("\n"):
    if (page_start):
      page_line = line

    if (searchstr in line.lower()):  # TODO: case-insensitiv suchen!
      if (page_line):
        print(page_line)
        page_line = ""
      print(line)

    if (line == "----------------------------------------"):
      if (page_line == ""):
        print()
      page_start = True
    else:
      page_start = False


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
  global last_nr
  global last_page

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
  all_files.sort(key=lambda x: os.path.getsize(x), reverse=True)
  all_files = [os.path.basename(x) for x in all_files]

  if (all_files[0][-15] == "_"):
#    print("Humax")
    all_files.sort(key=lambda x: x.split("_")[0][:-1] + "_" + x.split("_")[1][:6])
  elif (all_files[0].endswith("_video.txt")):
#    print("Medion A")
    all_files.sort(key=lambda x: x.split("]")[0] + "]" + x.split("]")[1].split("-")[0])
  elif (all_files[0].endswith("].txt")):
#    print("Medion B")
    all_files.sort(key=lambda x: x.split("[")[0].split("-")[0] + " [" + x.split("[")[1])
  else:
#    print("other")
    all_files.sort(key=lambda x: x)

  first_write = True
  mode = 0
  i = 0

  while (i < len(all_files)):
    file_name = all_files[i]

    if file_name.endswith(".txt"):
      file_mask = file_name
      file_mask2 = ""
      if (re.search("_\d{10}.txt", file_name)):
        file_mask = file_name[:-16]
        file_mask2 = file_name[-15:-8]
      elif (re.search(".+ \[\d{4}-\d{2}-\d{2}\]\.txt", file_name)):
        file_mask = file_name[:-17]
        file_mask2 = file_name[-16:]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]
      elif (re.search("\[\d{4}-\d{2}-\d{2}\] .+_video\.txt", file_name)):
        file_mask = file_name[:-10]
        while (file_mask.endswith("-2") or file_mask.endswith("-1")):
          file_mask = file_mask[:-2]

      j = i
      while (j > 0 and all_files[j-1].startswith(file_mask) and file_mask2 in all_files[j-1]):
        j = j - 1
      while (i < len(all_files)-1 and all_files[i+1].startswith(file_mask) and file_mask2 in all_files[i+1]):
        i = i + 1

      list_name = all_files[j]
      if (j != i):
#        file_path = os.path.join(folder, all_files[j+1])
        file_path = os.path.join(folder, all_files[j])
        file_name = ""
        while (j <= i):
          file_name = file_name + (", " if (file_name != "") else "") + all_files[j]
          j = j + 1
      else:
        file_path = os.path.join(folder, file_name)

#      print(f'\nDEBUG: Open file "{file_path}"')
      with open(file_path, "r", encoding="utf-8") as file:
        content = file.read()
#      print(f"DEBUG: {len(content)} bytes read from file.")  

      if (list_name in inlist):
#        print(inlist[list_name])
        page = inlist[list_name]["page"]
        subpage = int(inlist[list_name]["subpage"])
        comment = inlist[list_name]["comment"]
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

      last_nr = 0
      searchstr = ""
      answer = ""

      while (True):
#        clear()
        print("----------------------------------------")
        print(file_path)
        print("----------------------------------------")
        if (searchstr != ""):
          do_search(content, searchstr)
          searchstr = ""
        else:
          subpage = print_page(content, page, subpage, (mode==1))
          
        print("----------------------------------------")
        if (comment):
          print("> Kommentar: " + comment)

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
        clear()

        if (answer == b'\x03'):
          return

        elif (answer == b'q'):
          break

        elif (answer == b'z' and i > 0):
          i = i - 2
          break

        elif (answer == b's'):
          do_save(outlist, first_write)

        elif (answer == b'K'):
          subpage = subpage - 1
        elif (answer == b'M'):
          subpage = subpage + 1

        elif (answer == b'H'):
          page = str(int(page) + 1)
        elif (answer == b'P'):
          page = str(int(page) - 1)

        elif (answer == b' '):
          mode = 1 if (mode==0) else 0

        elif (answer == b'f'):
          searchstr = input("\nSuche nach: ")

        elif (answer == b'c'):
          comment = input("\nKommentar eingeben: ")

        elif (answer == b'e'):
          os.system("notepad output.txt")

        if (answer == b'\r' and newpage != ""):
          page = newpage if (newpage != "0" and newpage != "000") else "100"
          subpage = 1

        elif (answer == b'\r' or answer == b'n' or answer == b'c'):
          if (answer == b'n'):
            page = 0
            subpage = 0
          outlist.append([file_name, page, subpage, comment])
          break

      if (answer == b'q'):
        break
    i = i + 1

  do_save(outlist, first_write)

if __name__ == "__main__":
  main()
