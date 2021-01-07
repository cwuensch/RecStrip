## You can use CC CFLAGS LD LDFLAGS CXX CXXFLAGS AR RANLIB READELF STRIP after include env.mak
include /env.mak

EXEC = RecStrip
OBJS = RecStrip.o InfProcessor.o NavProcessor.o CutProcessor.o NALUDump.o RebuildInf.o TtxProcessor.o PESProcessor.o PESFileLoader.o HumaxImport.o EycosImport.o H264.o StrToUTF8.o

CFLAGS2   ?= -g -O2 -MD -W -Wall -std=c99 -static -fno-strict-aliasing -ffunction-sections -fdata-sections

#DEFINES += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

all: $(EXEC)

# Implicit rules:

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS2) -c $(DEFINES) $(INCLUDES) $< -o $@ $(LDFLAGS)

# Dependencies:

MAKEDEP = $(CC) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

# The main program:

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -rdynamic $(OBJS) -o RecStrip

install: $(EXEC)
	mkdir -p $(DESTDIR)/usr/bin/
	install $< $(DESTDIR)/usr/bin/

# Housekeeping:

clean:
	-rm -f $(OBJS) $(DEPFILE) $(EXEC) core* *~
CLEAN: clean

