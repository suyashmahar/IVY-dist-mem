RPCLIB_INCLUDE := ../../vendor/rpclib/include
RPCLIB_DIR := ../../vendor/rpclib/build

SOURCES := $(wildcard *.cc)
OBJECTS := $(patsubst %.cc, %.o, $(SOURCES))
INCLUDE :=-I../../vendor/headers/ -I../$(RPCLIB_INCLUDE)
EXTRA_LDFLAGS :=-L$(RPCLIB_DIR)
EXTRA_LINKFLAGS :=  -Wl,--whole-archive \
			-Wl,--no-whole-archive
EXTRA_CXXFLAGS := #-fPIC

DEPENDS := $(wildcard ./*.hh)

include ../../common.make

all:
	$(IVY_MAKE) library

library: libivy.so

libivy.so: $(OBJECTS) $(DEPENDS)
	$(IVY_CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -shared -o $@ \
		$(OBJECTS) $(LINKFLAGS)

clean: default_clean_instance
