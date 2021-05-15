.PHONY: libivy workloads

include common.make

all: workloads

workloads: libivy
	$(IVY_MAKE) -C workloads

libivy:
	$(IVY_MAKE) -C libivy

clean: workloads_clean libivy_clean

