BASE = $(shell cd /tapapi/TMS; pwd)
CUR_DIR = $(shell pwd)

# include default settings
include ${BASE}/include/tool.mk

# output object directory
SRC_DIR = ${CUR_DIR}

CFLAGS   ?= -g -O2 -MD -W -Wall -DLINUX -D_REENTRANT -mips32 -std=c99 -static -fno-strict-aliasing -ffunction-sections -fdata-sections
CXXFLAGS ?= -s -O2 -MD -W -Wall -DLINUX -D_REENTRANT -mips32 -static -Woverloaded-virtual -Wno-parentheses

OBJS = RecStrip.o InfProcessor.o NavProcessor.o CutProcessor.o NALUDump.o RebuildInf.o TtxProcessor.o PESProcessor.o PESFileLoader.o HumaxImport.o EycosImport.o H264.o StrToUTF8.o

#DEFINES += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

all: RecStrip

# Implicit rules:

%.o: %.c
	$(CC) $(CFLAGS) -c $(DEFINES) $(INCLUDES) $< -o $@

# Dependencies:

MAKEDEP = $(CC) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

# The main program:

RecStrip: $(OBJS)
	$(CC) $(CFLAGS) -rdynamic $(OBJS) -o RecStrip

# Housekeeping:

clean:
	-rm -f $(OBJS) $(DEPFILE) RecStrip core* *~
CLEAN: clean

