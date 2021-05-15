CXXFLAGS := -std=c++20
LINKFLAGS := -pthread

include ../../common.make

pdot: pdot.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)
