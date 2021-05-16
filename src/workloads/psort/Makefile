DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

CXXFLAGS := -std=c++20
EXTRA_LINKFLAGS := -pthread -livy
EXTRA_LDFLAGS := -L../../libivy -Wl,-rpath=$(DIR)/../../libivy/
EXTRA_CXXFLAGS := -I../../libivy/ -I../../vendor/headers/

include ../../../common.make

psort: psort.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)

clean: default_clean_instance
