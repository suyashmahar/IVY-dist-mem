CXXFLAGS := -std=c++20
LINKFLAGS := -pthread

include ../../common.make

psort: psort.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)
