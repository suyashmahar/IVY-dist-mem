CXXFLAGS := -std=c++20
LINKFLAGS := -pthread

%.o: %.cc $(DEPENDS)
	$(PUDDLES_CXX) $(INCLUDE) -c $(CXXFLAGS) $< -o $@

pdot: pdot.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LINKFLAGS)
