#include <linux/kernel.h>               /* Basic Linux module headers */
#include <linux/module.h>
#include <generated/asm-offsets.h>      /* __NR_syscall_max */
#include <linux/batch.h>                /* struct syscall, __NR_batch */
#include <linux/uaccess.h>              /* copy_from_user put_user */
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
#	include <linux/signal.h>
#else
#	include <linux/sched/signal.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0) /* < 3,19,0 needs READ_ONCE */
#	include <linux/sched/task_stack.h>     /* task_pt_regs */
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,16,0)
#	include <linux/nospec.h>         /* array_index_nospec */
#else
#	define array_index_nospec(i, n) (i) /* No such mitigation here */
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(5,14,0)
#  define __NR_syscall_max (NR_syscalls - 1)
#endif
#ifndef GFP_KERNEL_ACCOUNT
#  define GFP_KERNEL_ACCOUNT GFP_KERNEL
#endif

MODULE_DESCRIPTION("Generic batch system call API");
MODULE_AUTHOR("Charles Blake <charles.l.blake2@gmail.com>");
MODULE_LICENSE("GPL v2");

static inline long segv(void const *Addr)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	struct siginfo info;
#else
	struct kernel_siginfo info;
#endif
	memset(&info, 0, sizeof info);
	info.si_signo = SIGSEGV;
	info.si_addr = (void *)Addr;
	return send_sig_info(SIGSEGV, &info, current);
}

typedef asmlinkage long (*F0_t)(void);
typedef asmlinkage long (*F1_t)(long);
typedef asmlinkage long (*F2_t)(long, long);
typedef asmlinkage long (*F3_t)(long, long, long);
typedef asmlinkage long (*F4_t)(long, long, long, long);
typedef asmlinkage long (*F5_t)(long, long, long, long, long);
typedef asmlinkage long (*F6_t)(long, long, long, long, long, long);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
typedef long (*sc_t)(const struct pt_regs *);   /* sys_call_ptr_t */
static inline long indirect_call(void *f, int argc, long *a)
{	/* x64 syscall calling convention changed @4.17 to use struct pt_regs */
	struct pt_regs regs;
	memset(&regs, 0, sizeof regs);
	switch (argc) {
		case 6: regs.r9  = a[5]; /* Falls through. */
		case 5: regs.r8  = a[4]; /* Falls through. */
		case 4: regs.r10 = a[3]; /* Falls through. */
		case 3: regs.dx  = a[2]; /* Falls through. */
		case 2: regs.si  = a[1]; /* Falls through. */
		case 1: regs.di  = a[0]; /* Falls through. */
	} /* sc_t supports kernels built w/CONFIG_CFI_CLANG(kCFI) & trap on. */
	return ((sc_t)f)(&regs);
}
#else
static inline long indirect_call(void *f, int argc, long *a)
{
	switch (argc) {
		case 0: return ((F0_t)f)();
		case 1: return ((F1_t)f)(a[0]);
		case 2: return ((F2_t)f)(a[0], a[1]);
		case 3: return ((F3_t)f)(a[0], a[1], a[2]);
		case 4: return ((F4_t)f)(a[0], a[1], a[2], a[3]);
		case 5: return ((F5_t)f)(a[0], a[1], a[2], a[3], a[4]);
		case 6: return ((F6_t)f)(a[0], a[1], a[2], a[3], a[4], a[5]);
	}
	return -ENOSYS;
}
#endif

#include <linux/kprobes.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
#endif
static void **scTab = 0;
static char deny[1024] = { 0 }; /*XXX Pass list rather than a deny list? */
static void (*run_task_work)(void); /* Not EXPORTed to modules: use kallsyms */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_batch(const struct pt_regs *regs); /* prototype to silence warning */
asmlinkage long sys_batch(const struct pt_regs *regs)
{
	unsigned long ur0   = regs->di;
	unsigned long uc0   = regs->si;
	unsigned long ncall = regs->dx;
	unsigned long flags = regs->r10;
	unsigned long size  = regs->r8;
#else
asmlinkage long sys_batch(unsigned long ur0, unsigned long uc0,
			  unsigned long ncall, long flags, long size)
{
#endif
	long          r = 0, off = 0, *krv = NULL;
	unsigned long i = 0, nr, urEnd = ur0 + ncall * sizeof(long),
	              aEnd = uc0 + ncall * sizeof(syscall_t), kr0, kc0;
	syscall_t    *calls = (syscall_t *)uc0, *buf = NULL;
	gfp_t         gfp_flags = GFP_KERNEL_ACCOUNT|__GFP_ZERO|__GFP_NOWARN;
	char          d;
	if (flags | size) return -EINVAL; /* ABI evolution placeholders */
	if (ncall == 0)
		return 0;
	if (ncall > 4096) /* sizeof(syscall_t)=64=>288 KiB (w/512 KiB block) */
		return -EINVAL;
	if (!(buf = kmalloc(ncall * (sizeof *krv + sizeof *calls), gfp_flags)))
		return -ENOMEM;
	krv = (long *)(kr0 = (unsigned long)buf);
	kc0 = kr0 + ncall * sizeof(long);
	if (unlikely(copy_from_user((void *)kc0, calls, ncall * sizeof *calls)))
		{ kfree(buf); return -EFAULT; }
	for (/**/; i < ncall; i += 1 + off) {
		void      *f;
		syscall_t *c = (syscall_t *)(kc0 + i * sizeof *c);
		if (c->nr == __NR_wdcpy) {
			unsigned long tmp;               // Do *dst = *src
			unsigned long src = (unsigned long)c->arg[1];
			unsigned long dst = (unsigned long)c->arg[0];
			if (ur0 <= src && src <= urEnd - sizeof(long))
				tmp = *(unsigned long *)(kr0 + (src - ur0));
			else if (unlikely(get_user(tmp, (long *)c->arg[1])))
				{ krv[i] = -EFAULT; goto errRet; }
			if (uc0 <= dst && dst <= aEnd - sizeof(long))
				*(unsigned long *)(kc0 + (dst - uc0)) = tmp;
			else if (unlikely(put_user(tmp, (long *)c->arg[0])))
				{ krv[i] = -EFAULT; goto errRet; }
			off = 0;
			continue; //NOTE: AnyBogus!=__NR_wdcpy encodes fwd jump
		}
		nr = array_index_nospec((unsigned)c->nr, __NR_syscall_max + 1);
		r = krv[i] = unlikely(c->nr < 0 || c->nr > __NR_syscall_max ||
		         (unsigned)c->argc > 6 || !(f = scTab[nr])) ? -ENOSYS
		  : !(d = deny[nr]) ? indirect_call(f, c->argc, c->arg)
		  : d == 1 ? -ENOSYS :
		    ({ long x = indirect_call(f, c->argc, c->arg);
		       if (!x) goto execOk;   /* old mm gone: no copy_to_user */
		       x; });                 /* failed exec: normal -errno */
		if ((unsigned long)r > 18446744073709547520ULL) { //-4096<r<0
			if (r <= -ERESTARTSYS && r >= -ERESTART_RESTARTBLOCK)
				krv[i] = -EINTR; // ENOIOCTLCMD swept in but..
			off = c->jumpFail;       //..never esc sys ret anyway.
		} else if (r > 0)
			off = c->jumpPos;
		else
			off = c->jump0;
		if (off < 0) break;
		if (unlikely(READ_ONCE(current->task_works))) // dup2 fput defer
			run_task_work();                      // run it now
		if (signal_pending(current)) /* Should go @top & do unambig ..*/
			break;               /*..0-progress repr w/C wrapper..*/
	}                                    /*..translating signal-halted. */
	if (i >= ncall) i = ncall - 1;
errRet: if (unlikely(copy_to_user((void *)ur0, krv, (i + 1) * sizeof(long))))
		segv((void *)ur0); // Cannot store EFAULT
	kfree(buf);
	return i;
execOk: kfree(buf);
	return 0;
}

static void *ni;                /* What the kernel runs for unimplemented nrs */
static int ni_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *user = task_pt_regs(current);
	if (user->orig_ax != __NR_batch)        /* Another unimplemented nr */
		return 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
	if (regs->di != (unsigned long)user)    /* Not syscall entry's call */
		return 0;
#endif
	regs->ip = (unsigned long)sys_batch;    /* Same args & return address */
	return 1;                               /* ip changed: no singlestep */
}
/* Not run, but presence stops kprobe jump-optim, which would ignore ip chg. */
static void ni_post(struct kprobe *p, struct pt_regs *regs, unsigned long fl) {}
static struct kprobe ni_kp = { .pre_handler = ni_pre, .post_handler = ni_post };

static int __init mod_init(void) {      /* Hook the unimplemented function */
	int i, err;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
	typedef unsigned long (*fp_t)(const char *name);
	int rkp_ret;
	if ((rkp_ret = register_kprobe(&kp)) < 0) {
		printk(KERN_ERR "batch: No CONFIG_KPROBES,_KALLSYMS,_KALLSYMS_ALL\n");
		return rkp_ret;
	}
	fp_t kallsyms_lookup_name = (fp_t) kp.addr; unregister_kprobe(&kp);
#endif
	if (!(scTab = (void **)kallsyms_lookup_name("sys_call_table"))) {
		printk(KERN_ERR "batch: No CONFIG_KALLSYMS,_KALLSYMS_ALL\n");
		return -ENOSYS;
	}
	run_task_work = (void (*)(void))kallsyms_lookup_name("task_work_run");
	if (!run_task_work) {
		printk(KERN_ERR "batch: cannot find task_work_run\n");
		return -ENOSYS;
	}
	ni = scTab[__NR_batch];                /* Unused slot: ni function */
	memset(&deny[0], 0, sizeof deny);      /* maybe redundant */
	BUILD_BUG_ON(__NR_syscall_max + 1 > sizeof deny);
	deny[__NR_batch]=deny[__NR_clone]=deny[__NR_vfork]=deny[__NR_fork]=
	deny[__NR_exit]=deny[__NR_exit_group]=deny[__NR_rt_sigreturn]=
	deny[__NR_kexec_load]=1; deny[__NR_execve]=2;
#ifdef __NR_clone3
	deny[__NR_clone3] = 1;
#endif /* __NR_clone3 */
#ifdef __NR_execveat	/* Not much danger in uretprobe, reboot, ..*/
	deny[__NR_execveat] = 2;
#endif /* __NR_execveat */
#ifdef __NR_kexec_file_load
	deny[__NR_kexec_file_load] = 1;
#endif /* __NR_kexec_file_load */
	for (i = 0; i <= __NR_syscall_max; i++) /* Subcalls to unimplemented */
		if (scTab[i] == ni)             /*.. nrs: -ENOSYS, skipping  */
			deny[i] = 1;            /*.. kprobe (needed < 4.17)  */
	ni_kp.addr = (kprobe_opcode_t *)ni;
	if ((err = register_kprobe(&ni_kp))) {
		printk(KERN_ERR "batch: No CONFIG_KPROBES\n");
		return err;
	}
	printk(KERN_INFO "batch: installed as %d\n", __NR_batch);
	return 0;
}
static void __exit mod_cleanup(void) {        /* Unsafe to unload while any */
	unregister_kprobe(&ni_kp);            /*..batch is in progress, BUT */
	printk(KERN_INFO "batch: removed\n"); /*..should be a real syscall  */
}                                             /*..long-term anyway.         */
module_init(mod_init);
module_exit(mod_cleanup);
MODULE_LICENSE("GPL");
