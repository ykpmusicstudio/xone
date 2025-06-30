KVERSION := $(shell uname -r)
KDIR := /lib/modules/${KVERSION}/build
MAKEFLAGS+="-j $(shell nproc)"

default: clean
	$(MAKE) -C $(KDIR) M=$$PWD

debug: clean
	$(MAKE) -C $(KDIR) M=$$PWD EXTRA_CFLAGS="-O0 -g3 -DDEBUG"

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

unload:
	./modules_load.sh unload

load: unload
	./modules_load.sh

