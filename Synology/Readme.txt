Kompilieren für Synology DiskStation
====================================

# Voraussetzungen:
 * Linux PC (oder virtuelle Maschine) mit root-Rechten (Docker funktioniert NICHT!)
 * es soll / darf nicht auf der Synology selbst kompiliert werden(!)
 * Internet-Zugang (zum Laden der Scripts)

# Pakete installieren:
  apt-get install python3 git subversion

# Verzeichnisse erstellen:
  mkdir -p toolkit/source
  cd toolkit

# Synology Package Creation Scripts auschecken:
  git clone https://github.com/SynologyOpenSource/pkgscripts-ng.git

# Synology Minimal Package auschecken (optional):
  cd source
  git clone https://github.com/SynologyOpenSource/minimalPkg.git
  cd ..

# Download toolchain:
  PLATFORM=rtd1296
  VERSION=6.1
  RUN pkgscripts-ng/EnvDeploy -p ${PLATFORM} -v ${VERSION}

# Neuere toolchain (Nutzung unbekannt):
  https://downloads.sourceforge.net/project/dsgpl/DSM%206.2.2%20Tool%20Chains/Realtek%20RTD129x%20Linux%204.4.59/rtd1296-gcc494_glibc220_armv8-GPL.txz?r=https%3A%2F%2Fsourceforge.net%2Fprojects%2Fdsgpl%2Ffiles%2FDSM%25206.2.2%2520Tool%2520Chains%2FRealtek%2520RTD129x%2520Linux%25204.4.59%2Frtd1296-gcc494_glibc220_armv8-GPL.txz%2Fdownload&ts=1583159482

# Source-Code auschecken:
  svn checkout svn://cytec.privatedns.org/TAPs/MovieCutter/NALU source/NALU

# Synology-Konfiguration (siehe RecStrip_SynoPkg) nach toolkit/source/NALU kopieren

# Projekt kompilieren:
  pkgscripts-ng/PkgCreate.py NALU

# Resultat findet sich in:
  toolkit/build_env/ds.rtd1296-6.1/source/NALU/RecStrip

