MODULE = mod_convert
TARGET = .libs/$(MODULE).so

C_SRC = $(MODULE).cpp ahtse_util.cpp JPEG_codec.cpp PNG_codec.cpp Packer_RLE.cpp
HEADERS = ahtse_util.h BitMask2D.h

OBJECTS = $(C_SRC:.cpp=.lo)

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE

LIBS = -ljpeg -lpng -lstdc++

FILES = $(C_SRC) $(HEADERS)

# Create a file Makefile.lcl. It should define INCLUDES and MOD_PATH
# For example on how to create Makefile.lcl see Makefile.example.lcl

include Makefile.lcl

default	: $(TARGET)

# Can't use apxs to build c++ modules
# The options used here might depend on the system
#
$(TARGET)	:	$(OBJECTS)
	$(LIBTOOL) --mode=link g++ -o $(MODULE).la -rpath $(DEST) -module -avoid-version $^ $(LIBS)

%.lo	:	%.cpp
	$(LIBTOOL) --mode=compile g++ -std=c++0x -prefer-pic -O2 -Wall $(DEFINES) $(INCLUDES) -pthread -c -o $@ $< && touch $(@:.lo=.slo)

install	:	$(TARGET)
	$(SUDO)	cp $(TARGET)	$(DEST)

clean	:
	rm -rf .libs *.o *.lo *.slo *.la

