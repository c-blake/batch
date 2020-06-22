# batch: Generic Linux System Call Batching

Kernel <-> user crossings are expensive.  Across any such boundary (IPC or
network messages are other obvious cases), it makes sense to get as much work
done per crossing as possible.  Batching is one approach.  In the user-kernel
setting, privilege checking and such is already done inside system call code.
So, security implications should be minimal and there should be no need to
restrict code uploaders to be privileged or verify code like EBPF.  All we
need to do to add this to Linux is decide on a convenient API and then loop
over a buffer of system calls.  That's what this package does.  It may not be
completely right "as is" or as fast as possible.  I'm open to suggestions if
you have them.

Safety flows from allowing only minimal control flow to occur in-kernel that
is trivially loop-free/bounded compute time.  Specifically, it allows
jumping forward by more than one slot in the array of system calls on a
trivalent condition of a sub-call return `{ -4096 < r < 0, == 0, or > 0 }`
(conventionally usually meaning { error, success/done/EOF, and more-work-or
answer} conditions).  Not all system call targets are accepted because the
call-return protocol varies from the usual, e.g. `fork`, `exec`, or `batch`
itself to prevent loops.  Using blocked system calls results in an -ENOSYS
in the return value slot for such a call.  Beyond that there are just two
fake syscalls implemented in-line in the dispatch loop: word copy and an
unconditional jump forward.  That's all that is really needed to construct
fairly complex mini-programs that should be safe by design.

That is all fairly abstract.  An example program (`examples/total.c`) makes
this explicit.  Another example use case is file tree walking (ftw) when
user code needs file metadata like sizes, times, owners, etc.  Following on
from the already-a-batch-interface `getdents64` works nicely here.  `du` is
a classic example here.  In personal timings, I see end-to-end speed-ups of
1.3x on `mdu` vs the non-kernel based/user-space loop `BATCH_EMUL=1 mdu`
(and end-to-end speed-ups of 1.7x because GNU `du` uses an expensive ftw).
Another natural example is "path search" wherein a user program attempts
several-to-many easily pre-computed paths, stopping at the first one which
succeeds/has a syscall return 0.  This would be a `syscall_t` with many
`scall3(open, 0, -1, -1, pathX, flags, 0)` entries.  In reality, examples
are likely limited only by one's imagination.

It's also not hard to imagine compilers to detect batchable situations
automatically and even auto-convert.  On the source side this is not so
different from auto-vectorization.  The target language is also not so far
from an assembly language with no backward jumping.  This kind of interface
is easy to "emulate" in pure user-space code when the deployment system has
no `sys_batch` available.  `include/linux/batch.h` has such an emulator
activated by `BATCH_EMUL` being set.

Oh, and, as set up right now, it only works on Linux x86\_64 for kernels in
the late 4.* to present 5.* version ranges.  It might work on earlier 3.x
versions, but I haven't tested it on such.  I hacked it up as a module that
hijacks a syscall slot purely for my development convenience.  Usage should
be as easy as:
```
cd module
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
