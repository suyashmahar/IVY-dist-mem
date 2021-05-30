.PHONY: pdot psort pingpong pagefaultlatency

include ../../common.make

all: psort pdot pingpong pagefaultlatency

pdot psort pingpong pagefaultlatency:
	$(IVY_MAKE) -C $@

clean: psort_clean pdot_clean pingpong_clean pagefaultlatency_clean

