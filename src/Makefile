MODULE = mod_convert
TARGET = .libs/$(MODULE).so

C_SRC = $(MODULE).cpp ahtse_util.cpp JPEG_codec.cpp PNG_codec.cpp Packer_RLE.cpp JPEG8.cpp JPEG12.cpp
HEADERS = ahtse_util.h BitMask2D.h

OBJECTS = $(C_SRC:.cpp=.lo) libjpeg12.a

CXXFLAGS = -prefer-pic -O2 -Wall

DEFINES = -DLINUX -D_REENTRANT -D_GNU_SOURCE $(DEBUG)

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
	$(LIBTOOL) --mode=compile g++ -std=c++0x $(CXXFLAGS) $(DEFINES) $(INCLUDES) -pthread -c -o $@ $< && touch $(@:.lo=.slo)

libjpeg12.a	:	jpeg12-6b/libjpeg12.a
	cp	$^ $@

jpeg12-6b/libjpeg12.a	:
	$(MAKE) -C jpeg12-6b

install	:	$(TARGET)
	$(SUDO)	cp $(TARGET)	$(DEST)

clean	:
	rm -rf $(TARGET) $(OBJECTS) .libs *.o *.lo *.slo *.la jpeg12-6b/*.o jpeg12-6b/libjpeg12.a
