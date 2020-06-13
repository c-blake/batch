.PHONY: all clean

ALL :=	t0call t1call tgetuid tbad1 tbad2 lstat-root fstat-0 getuids closes \
	unimpl lstat-argvT lstat-argv total statx-argvT statx-argv lnct mdu \
	dispOvrHd

all: $(ALL)
clean: ; $(RM) $(ALL)

CC := gcc
#CC := musl-gcc
W := -Wall -W -Wno-missing-braces -Wno-unused-parameter -Wno-missing-field-initializers
C := $(CC) -O3 $W -ggdb -g3 -I../include

% : %.c
	$C $< -o $@

lstat-argvT : lstat-argv.c; $C -DTEST    $< -o $@
statx-argvT : statx-argv.c; $C -DTEST    $< -o $@

dispOvrHd : dispOvrHd.c; $C -fno-inline-functions $< external.s -o $@
