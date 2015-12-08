BASE = $(shell cd /tapapi/TMS; pwd)
CUR_DIR = $(shell pwd)

# include default settings
include ${BASE}/include/tool.mk

# output object directory
SRC_DIR = ${CUR_DIR}

CFLAGS   ?= -g -O2 -MD -W -Wall -DLINUX -D_REENTRANT -mips32 -static
CXXFLAGS ?= -s -O2 -MD -W -Wall -DLINUX -D_REENTRANT -mips32 -static -Woverloaded-virtual -Wno-parentheses

OBJS = RecStrip.o

DEFINES += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

all: RecStrip

# Implicit rules:

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $< -o $@

# Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

# The main program:

RecStrip: $(OBJS)
	$(CXX) $(CXXFLAGS) -rdynamic $(OBJS) -o RecStrip

# Housekeeping:

clean:
	-rm -f $(OBJS) $(DEPFILE) RecStrip core* *~
CLEAN: clean

