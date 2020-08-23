obj-m := batch.o
EXTRA_CFLAGS := -I$M/../include
batch.o : scTab.h
scTab.h:
	grep -w sys_call_table /boot/System.map-`uname -r` | \
	  sed 's/\([^ ]*\) .*/#define scTab ((void **)0x\1UL)/' > scTab.h
