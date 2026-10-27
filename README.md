# batch: Generic Linux System Call Batching
Kernel <-> user crossings are expensive.  Across any such boundary (IPC / net
messages are other obvious cases), it makes sense to do as much work per
crossing as possible.  Batching is one approach.  In the Linux user-kernel
setting, privilege checking and such is already done inside system call code.
So, security implications should be minimal and there should be no need to
restrict code uploaders to be privileged or verify code like eBPF.  Rather than
a general in-kernel execution environment, one can get much mileage out of a
deliberately bounded data/control-flow model.  All we need is a convenient API
to interpret a bounded array of system-call instructions.  That's what this
package does.  Instead of adding yet another subsystem specific batch API like
`readv`, the problem can maybe be solved once & for all.[^1]

## Basic API
Only minimal in-kernel control flow is given.  This makes work trivially
loop-free (guaranteed to halt), bounded by batch size.  It also affords a fast
enough virtual machine (only 36 lines of C!) to be worthwhile.  Specifically,
a batch can only jump forward by 1 or more array slots in the array of system
calls trivalently on returns `{ < 0, == 0, > 0 }`.  Said returns conventionally
mean usually { error, success/done/EOF, and more-work-or answer } conditions.

Not all system call targets are allowed since call-return protocols vary from
the usual, e.g.  `fork`,  or `batch` itself (to prevent loops "spelled"
as recursion).  Denied system calls get `-ENOSYS` in the return value slot.

One "fake" syscall is implemented in-line in the sub-call dispatch loop: word
copy.  This allows chaining outputs of one call to inputs of subsequent calls
(such as using a file descriptor return in a subsequent call).  Any
unimplemented/always failing call can skip blocks meant as an error/alternate
paths.  This allows representing a great many multi-call-mini-programs.

## Driving the Impl
This kind of interface is easy to "emulate" in pure user-space code when a
deployment system has no `sys_batch` available.  `include/linux/batch.h` has
such an emulator activated by `BATCH_EMUL` being set.  Such emulation is also
useful to benchmark improvement due to the system call.

## "Simple" Examples with links to C source
That is all fairly abstract.  A demo [`total.c`](examples/total.c) may help.
Another example is file tree walking (ftw) when user code needs file metadata
(sizes, times, owners, ..).  `getdents64` is already a batch interface, but the
stat's are not.  `du` is a classic example here.  In personal timings, I see
~1.3x speed-ups for [`mdu.c`](examples/mdu.c) over `BATCH_EMUL=1 mdu` (and ~1.7x
speed-ups vs GNU `du` since the latter probably uses an `ftw` more expensive
than [`ftw.c`](examples/ftw.c) to do tree depths unbounded by open fd limits).

Another natural example is "path search" wherein user programs attempt
several-to-many easily pre-computed paths, stopping at the first one which
succeeds/has a syscall return 0.  This would be a `syscall_t` array with many
`scall3(open, 0, -1, -1, pathX, flags, 0)` entries.  In reality, examples are
limited only by one's imagination, but be forewarned that much system work
interacting with real devices is dominated by much larger times & overheads.

## Extensions / Future Directions
It's not hard to imagine a C/other compiler detecting batchable situations
automatically and even auto-converting from free form mode to various little
batches.  On the source side this is not so different from auto-vectorization.
The target language is also not so far from an assembly language with no
backward jumps.  Cassyopia[^1] took steps in this direction, but with a less
expressive notion of batching limiting mini-programs to "flat batches".  The
idea of having the presently unused `flags` parameter take a `BATCH_ATOMIC` to
auto-acquire & release an in-kernel lock is another easily imagined direction.

## Limitations & Usage
As set up right now, it only works on Linux x86\_64 for kernels in the late 4.*
to present 7.* version ranges.  It might work on earlier 3.x versions, but I
haven't tested it on such.  For my development convenience and ease of others
trying it without doing a full kernel build, I hacked it up as a module
hijacking the `afs_syscall` slot.  Usage should be as easy as[^3]:
```
git clone https://github.com/c-blake/batch $HOME/s/bat
cd $HOME/s/bat/module; ./build
as-root insmod batch.ko     # or modprobe batch
cd ../examples; make getuids
BATCH_VERBOSE=1 ./getuids   # should print "sys_batch Real\n" to stderr
BATCH_EMUL=1 ./getuids
```
## Checking if it works
You can also run `strace ./getuids` to see if `afs_syscall` is being used.  You
may need to set `CONFIG_RANDOMIZE_BASE=n` in your kernel config or reboot with
`nokaslr=1` on the kernel command line to get a 3rd party module inserted.  You
also (since Linux-5.7) need `CONFIG_FUNCTION_TRACER=y CONFIG_DYNAMIC_FTRACE=y
CONFIG_DYNAMIC_FTRACE_WITH_REGS=y` & `sysctl kernel.ftrace_enabled=1`.[^3]

## A Simple Benchmark, More Carefully Evaluated
We can get a sense of the best possible overhead reduction on your CPU by using
the Nim program [`fitl`](https://github.com/c-blake/fitl) and this repos
[`examples/getuids.c`](examples/getuids.c):
```
$ BATCH_EMUL=1 ./getuids|awk '{print $3,$2,$1*$2}'|fitl -s0 -c,=,n,b -b100 1 2 3
$1= -23.41 *$2 + 147.132 *$3

bootstrap-stderr-corr matrix
     6.577    -0.5422
               0.6431

$ ./getuids|awk '{print $3,$2,$1*$2}'|fitl -s0 -c,=,n,b -b100 1 2 3 
$1= 349.83 *$2 + 12.950 *$3

bootstrap-stderr-corr matrix
     1.126    -0.8054
               0.1614
```
The way to read this is: Doing a user-space loop, the time equation is (-23.4 +-
6.6) + (147.13 +- 0.64)\*numSysCalls nanoseconds while via `sys_batch` it takes
(349.8 +- 1.1) + (12.95 +- 0.16)\*numSysCalls.  The per-call overhead ratio is
thus (147.13 +- 0.64)/(12.95 +- 0.16).  Using [error
propagation](https://en.wikipedia.org/wiki/Propagation_of_uncertainty) this
ratio becomes (11.36 +- 0.15).

Since errors are ~100x smaller than values, this is statistically significant
and the propagation is approximately correct for Gaussian errors (which these
are *not*, though, but the leptokurtosis here is not enough to invalidate the
main result and people care more about the leading ratio than its distribution).
Since the ratio is >11X faster, it is also a substantial improvement.[^4]

Of course, just returning uids is about the least work kernels do.  Almost all
other calls do much more work on the kernel-side which amortizes entry overhead
to be a small cost in context.  For example, the speed-up is much lower for the
work of `mdu.c` mentioned above.  Low hanging fruit optimizations here are
things like shells doing many `sigaction` calls as user-space loops over signals
where the kernel-side work is minimal but necessary.  Another end-to-end example
is https://github.com/c-blake/bu/blob/main/mk1.nim freshness checking 40,400 hot
cache files relative to the same number of outputs ~2.2x faster w/`sys_batch`.

An entirely different approach to analyze performance here might be to measure
the `batch.h` VM in user space or profile it in-kernel to study behavior.  I did
that back around year 2000, but did not for this round.  In any event, that kind
of analysis, the above `getuid` microbenchmark, and end-to-end application perf
will all be very sensitive to both CPU & kernel spectre mitigations.  I'd be
interested in anyone doing such work & happy to post it in this repository upon
request.

## Final Caveats
This is presently at a proof-of-concept level to study optimization opportunity.
It may not be advisable to deploy this on a system with untrusted user code.
E.g., the `deny` list hasn't been vetted for security implications or
interactions with syscall auditing.  It seemed worth sharing/getting feedback
upon.

[^1]: A full related work / comparison section seems maybe too distracting, but
this work is a unique/distinct approach from quite a few problem adjacent ones
to merit some notes.  CompositeCalls (Ballesteros et al., 2000) is by far the
closest work, but it allows less safe unbounded programs and is also slow (7X
per-call overhead relative to the 2X here).  Trading generality for safety and
getting perf seems win-win here.  Cosy (Purohit et al., 2003) is also very close
and does chain return values, but allows loops and in-kernel user functions,
needing time limits and segmentation for safety.  Cassyopia[^2] did more
automatic discovery (like Cosy) but used a less expressive VM limiting
opportunities.  eBPF allows more general programs but doesn't re-use syscalls
through the table.  Reaching `statx`, `mmap`, etc. means more code for kernel
side helpers than the whole batch facility.  Unprivileged eBPF is also off by
default on many systems.  io_uring (also oft off lately) has a similar "few
blessed subcalls" aspect & e.g. w/statx uses 2.5x more total CPU.  Xen's
multi-call for hypercalls does not do chained / conditional and does not reach
death-by-1000-syscall-cuts complaints against the Unix API.  NFSv4 COMPOUND &
SMB2 ("current filehandle" ideas) are similarly confined to preliminary steps of
protocol-level ident propagation, not conditional branching or ret-val-to-arg
propagation.  The MIT exokernel xok had a "flat batch" idea with a beautiful
comment by Mazieres about batching and had a few other "mini assembly
languages", but not this one.  This extension to easily bounded conditional
chains was inspired by that work, not Ballesteros et al.  (which this work
notionally pre-dates).  Ballesteros&Cassyopia&Cosy (at least!) are strong prior
art for the general claims of US9038075, maybe worth mention if one doubts the
Red Hat Patent Promise.  A not wholly unfair summary might be low-level API
focus of the exokernel & CompositeCalls had a baby that realizes broader safety
by limited control flow and broader applicability via chaining with less
generality than eBPF/Cosy, but also without associated costs.

[^2]: Rajagopalan, M., Debray, S. K., Hiltunen, M. A., & Schlichting, R. D.
(2003). Cassyopia: Compiler assisted system optimization. In 9th Workshop on Hot
Topics in Operating Systems (HotOS IX). USENIX Association.

[^3]: At a low, but annoying cost of one fd per process, this idea could be done
with a device driver or a VFS (e.g. write(2)ing batch structs to run), but that
may have different performance characteristics.  Long-term, this idea fits most
naturally a "meta-system-call".  So, an impl showing this makes the most sense.
So, this should evolve into a patch set against mainline Linux.  Then it will
need building your own whole kernel just to try out.  Since almost all interest
so far in this has been "fly by" interest, time pressure for that is not great.

[^4]: These times are for an Intel i7-1370P running Linux-7.2.8 with no window
system and only `sshd` running as competing work.  While this is a big-little
arch, the minimum loop in the benchmark should filter to only times for P-cores.
One can of course use `taskset` & `chrt` to try to coax Linux schedulers to do
better, but we did get numbers with seemingly small errors.
