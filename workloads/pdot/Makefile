CXXFLAGS := -std=c++20
EXTRA_LINKFLAGS := -pthread

include ../../common.make

pdot: pdot.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)

clean: default_clean_instance
