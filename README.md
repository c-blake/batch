# batch: Generic Linux System Call Batching

Kernel <-> user crossings are expensive.  Across any such boundary (IPC or
network messages are other obvious cases), it makes sense to do as much work per
crossing as possible.  Batching is one approach.  In the Linux user-kernel
setting, privilege checking and such is already done inside system call code.
So, security implications should be minimal and there should be no need to
restrict code uploaders to be privileged or verify code like EBPF.  All we need
to do to add this to Linux is decide on a convenient API to over an array of
system calls storing into an array of return values.  That's what this package
does.

Only minimal control flow in-kernel is given so work is trivially loop-free &
bounded by batch size.  Specifically, a batch can only jump forward by 1 or more
array slots in the array of system calls either unconditionally or trivalently
on returns `{ -4096 < r < 0, == 0, or > 0 }`.  Said returns conventionally mean
usually { error, success/done/EOF, and more-work-or answer} conditions.

Not all system call targets are allowed since call-return protocols vary from
the usual, e.g.  `fork`, `exec`, or `batch` itself (to prevent loops spelled as
recursion).  Blocked system calls get `-ENOSYS` in the return value slot.

Two "fake" syscalls are implemented in-line in the sub-call dispatch loop: word
copy and unconditional forward jumps.  The copy lets you chain outputs of one
call to inputs of subsequent calls.  The unconditional jump can skip blocks
meant as an error/alternate path earlier (and strictly is not needed since it is
equivalent to any unimplemented call).  This allows representing a great many
mini-call-programs.

This kind of interface is easy to "emulate" in pure user-space code when the
deployment system has no `sys_batch` available.  `include/linux/batch.h` has
such an emulator activated by `BATCH_EMUL` being set.

That is fairly abstract.  A demo [`total.c`](examples/total.c) may help.
Another example is file tree walking (ftw) when user code needs file metadata
(sizes, times, owners, ..).  `getdents64` is already a batch interface, but the
stat's are not.  `du` is a classic example here.  In personal timings, I see
~1.3x speed-ups for [`mdu.c`](examples/mdu.c) over `BATCH_EMUL=1 mdu` (and ~1.7x
speed-ups vs GNU `du` since the latter probably uses an `ftw` more expensive
than [`ftw.c`](examples/ftw.c) to do tree depths unbounded by open fd limits).

Another natural example is "path search" wherein a user program attempts
several-to-many easily pre-computed paths, stopping at the first one which
succeeds/has a syscall return 0.  This would be a `syscall_t` array with many
`scall3(open, 0, -1, -1, pathX, flags, 0)` entries.  In reality, examples are
limited only by one's imagination, but be forewarned that much system work
interacting with real devices is dominated by much larger times & overheads.

It's also not hard to imagine compilers to detect batchable situations
automatically and even auto-convert.  On the source side this is not so
different from auto-vectorization.  The target language is also not so far
from an assembly language with no backward jumping.

Oh, and, as set up right now, it only works on Linux x86\_64 for kernels in
the late 4.* to present 6.* version ranges.  It might work on earlier 3.x
versions, but I haven't tested it on such.  I hacked it up as a module that
hijacks a syscall slot purely for my development convenience.  Usage should
be as easy as:
```
mkdir -p $HOME/s/bat
git clone https://github.com/c-blake/batch $HOME/s/bat
cd $HOME/s/bat/module
./build
as-root insmod batch.ko
cd ../examples; make
./mdu
BATCH_EMUL=1 ./mdu
du -sbl
```
At present, I would not recommend deploying this on a system with untrusted
user code.  The block list is obviously incomplete, and it hasn't been very
vetted for security implications.  It just seemed worth sharing/getting
feedback upon (though I expect to hear only crickets).
