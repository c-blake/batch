obj-m := batch.o
EXTRA_CFLAGS := -I$M/../include
batch.o : scTab.h

ifndef V
  override V = $(shell uname -r)
endif

scTab.h:
	(grep -w sys_call_table /boot/System.map-$V | \
	  sed 's/\([^ ]*\) .*/#define smSCTab ((char *)0x\1UL)/'; \
	 grep -w system_wq      /boot/System.map-$V | \
	  sed 's/\([^ ]*\) .*/#define smSysWQ ((char *)0x\1UL)/') > scTab.h
