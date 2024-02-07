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
array slots in the array of system calls trivalently on returns `{ < 0, == 0, >
0 }`.  Said returns conventionally mean usually { error, success/done/EOF, and
more-work-or answer} conditions.

Not all system call targets are allowed since call-return protocols vary from
the usual, e.g.  `fork`, `exec`, or `batch` itself (to prevent loops spelled as
recursion).  Blocked system calls get `-ENOSYS` in the return value slot.

One "fake" syscall is implemented in-line in the sub-call dispatch loop: word
copy to allow chaining outputs of one call to inputs of subsequent calls.  Any
unimplemented/always failing call can skip blocks meant as an error/alternate
paths.  This allows representing a great many multi-call-programs.

This kind of interface is easy to "emulate" in pure user-space code when a
deployment system has no `sys_batch` available.  `include/linux/batch.h` has
such an emulator activated by `BATCH_EMUL` being set.  Such emulation is also
useful to benchmark improvement due to the system call.

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

Oh, and, as set up right now, it only works on Linux x86\_64 for kernels in the
late 4.* to present 6.* version ranges.  It might work on earlier 3.x versions,
but I haven't tested it on such.  For my development convenience, I hacked it up
as a module hijacking the `afs_syscall` slot.  Usage should be as easy as:

```
git clone https://github.com/c-blake/batch $HOME/s/bat
cd $HOME/s/bat/module; ./build
as-root insmod batch.ko
cd ../examples; make
./mdu
BATCH_EMUL=1 ./mdu
du -sbl
```
You can e.g. run `strace ./mdu` to see if `afs_syscall` is being used.  You may
need to set `CONFIG_RANDOMIZE_BASE=n` in your kernel config or at least reboot
with `nokaslr=1` on the kernel command line to get the module inserted.

At present, I would not recommend deploying this on a system with untrusted
user code.  The deny list hasn't been vetted for security implications or
interactions with syscall auditing.  It seemed worth sharing/getting feedback
upon.
