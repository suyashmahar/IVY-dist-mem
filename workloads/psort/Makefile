CXXFLAGS := -std=c++20
LINKFLAGS := -pthread

%.o: %.cc $(DEPENDS)
	$(PUDDLES_CXX) $(INCLUDE) -c $(CXXFLAGS) $< -o $@

psort: psort.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)
