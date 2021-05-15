SELF_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

include $(SELF_DIR)colors.make

CXXFLAGS    :=-std=gnu++20 -ggdb3 -O0 -fPIC -Wall
CXXFLAGS    +=-fno-omit-frame-pointer
CXXFLAGS    +=$(PUDDLES_CXXFLAGS) #-fsanitize=address -fsanitize=leak
CXXFLAGS    +=$(FLUSH_INST_FLAG)
LDFLAGS     :=$(EXTRA_LDFLAGS)
LINKFLAGS   :=$(EXTRA_LINKFLAGS)

IVY_CC   =$(QUIET_CC)$(CC)
IVY_CXX  =$(QUIET_CC)$(CXX)
IVY_LN   =$(QUIET_LINK)ln
IVY_MAKE =+$(QUIET_MAKE)
IVY_AR   =+$(QUIET_AR)ar

ifndef V
QUIET_CC      = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_CXX     = @printf '   %b %b\n'  $(CCCOLOR)CXX$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_LINK    = @printf '  %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_MAKE    = @printf '  %b %b\n'   $(CCCOLOR)MAKE$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;$(MAKE) -s
QUIET_AR      = @printf '    %b %b\n'   $(ARCOLOR)AR$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
else
QUIET_MAKE    = $(MAKE)
endif


%.o: %.cc $(DEPENDS)
	$(IVY_CXX) $(INCLUDE) -c $(CXXFLAGS) $< -o $@

%_clean:
	$(IVY_MAKE) -C $(subst _clean,,$@) clean
