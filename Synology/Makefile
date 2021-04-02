## You can use CC CFLAGS LD LDFLAGS CXX CXXFLAGS AR RANLIB READELF STRIP after include env.mak
include /env.mak

EXEC = RecStrip
OBJS = RecStrip.o InfProcessor.o NavProcessor.o CutProcessor.o NALUDump.o RebuildInf.o TtxProcessor.o PESProcessor.o PESFileLoader.o HumaxImport.o EycosImport.o H264.o StrToUTF8.o

CFLAGS2   ?= -O2 -MD -W -Wall -static -std=c99 -fno-strict-aliasing -funsigned-char -D_FORTIFY_SOURCE=1 -Wstack-protector
LDFLAGS   += -Wl,--gc-sections

#DEFINES += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

all: $(EXEC)

# Implicit rules:

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS2) -c $(DEFINES) $(INCLUDES) $< -o $@

# The main program:

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -rdynamic $(OBJS) -o RecStrip $(LDFLAGS)

install: $(EXEC)
	mkdir -p $(DESTDIR)/usr/bin/
	install $< $(DESTDIR)/usr/bin/

# Housekeeping:

clean:
	-rm -f $(OBJS) $(DEPFILE) $(EXEC) core* *~
CLEAN: clean
