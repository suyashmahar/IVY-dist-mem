SOURCES := $(wildcard *.cc)

OBJECTS := $(patsubst %.cc, %.o, $(SOURCES))

LIB_DIR :=

INCLUDE :=-I../vendor/headers/

DEPENDS := $(wildcard ./*.hh)

include ../common.make

all:
	$(IVY_MAKE) library

library: libivy.so

libivy.so: $(OBJECTS) $(DEPENDS)
	$(IVY_CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -shared -o $@ \
		$(OBJECTS) $(LINKFLAGS)

clean: default_clean_instance
