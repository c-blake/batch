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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#include <linux/kprobes.h>
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
	unsigned long i = 0, urEnd = ur0 + ncall * sizeof(long),
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
		} // SpectreV1? array_index_nospec(c->nr, __NR_syscall_max + 1)
		r = krv[i] = unlikely(c->nr < 0 || c->nr > __NR_syscall_max ||
		         (unsigned)c->argc > 6 || !(f = scTab[c->nr])) ? -ENOSYS
		  : !(d = deny[c->nr]) ? indirect_call(f, c->argc, c->arg)
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,9,0) /*backports=>be more precise!*/
#include <linux/ftrace.h>
static unsigned long hook_addr;

static long batch_ni_syscall(const struct pt_regs *regs)
{ /* TODO try_module_get(THIS_MODULE)/module_put pair for rmmod fail w/EBUSY */
	if (regs->orig_ax == __NR_batch)      /* syscall_get_nr() on x86 */
		return sys_batch(regs);
	return -ENOSYS;                       /* All that ni_syscall does */
} /* Runs in place of __x64_sys_ni_syscall(regs): same args & return addr. */

static void notrace hook_cb(unsigned long ip, unsigned long parent_ip,
                            struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs); /* !0 given SAVE_REGS */
	if (regs)
		regs->ip = (unsigned long)batch_ni_syscall;
}

static struct ftrace_ops hook_ops = {
	.func  = hook_cb,
	.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION |
	         FTRACE_OPS_FL_IPMODIFY,
};

static int hook_install(unsigned long addr)
{
	int e = ftrace_set_filter_ip(&hook_ops, addr, 0, 0);
	if (e) {
		pr_err("batch: ftrace_set_filter_ip: %d; See CONFIG Notes\n",e);
		return e;
	}
	if ((e = register_ftrace_function(&hook_ops))) {
		ftrace_set_filter_ip(&hook_ops, addr, 1, 0);
		pr_err("batch: register_ftrace_function: %d\n", e);
	}
	return e;
}

static void hook_remove(unsigned long addr)
{
	unregister_ftrace_function(&hook_ops);
	ftrace_set_filter_ip(&hook_ops, addr, 1, 0);
}
#endif

extern unsigned long __force_order __weak;
#define store_cr0(x) asm volatile("mov %0,%%cr0" : "+r"(x), "+m"(__force_order))
static void allow_writes(void) {
	unsigned long cr0 = read_cr0(); clear_bit(16, &cr0); store_cr0(cr0);
}
static void disallow_writes(void) {
	unsigned long cr0 = read_cr0(); set_bit(16, &cr0); store_cr0(cr0);
}
void *sys_oldcall0;         /* Likely sys_ni_syscall, but save/restore anyway */
static int __init mod_init(void) {  /* sys_call_table[__NR_batch] = sys_batch */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
	typedef unsigned long (*fp_t)(const char *name);
	int rkp_ret;
	if ((rkp_ret = register_kprobe(&kp)) < 0) {
		printk(KERN_ERR "batch: cannot register_kprobe; Need "
		       "CONFIG_KPROBES, _KALLSYMS, & _KALLSYMS_ALL\n");
		return rkp_ret;
	}
	fp_t kallsyms_lookup_name = (fp_t) kp.addr; unregister_kprobe(&kp);
#endif
	if (!(scTab = (void **)kallsyms_lookup_name("sys_call_table"))) {
		printk(KERN_ERR "batch: cannot find sys_call_table; "
		       "Need CONFIG_KALLSYMS & CONFIG_KALLSYMS_ALL\n");
		return -ENOSYS;
	}
	run_task_work = (void (*)(void))kallsyms_lookup_name("task_work_run");
	if (!run_task_work) {
		printk(KERN_ERR "batch: cannot find task_work_run\n");
		return -ENOSYS;
	}
	sys_oldcall0 = scTab[__NR_batch];      /* save syscall */
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,9,0) /*backports=>be more precise!*/
	int err;
	if (!(hook_addr = kallsyms_lookup_name("__x64_sys_ni_syscall"))) {
		pr_err("batch: cannot find __x64_sys_ni_syscall\n");
		return -ENOENT;
	}
	if ((err = hook_install(hook_addr)))
		return err;
#endif
	allow_writes(); scTab[__NR_batch] = sys_batch; disallow_writes();
	printk(KERN_INFO "batch: installed as %d\n", __NR_batch);
	return 0;
}
static void __exit mod_cleanup(void) {  /* restore sys_call_table[__NR_batch] */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,9,0) /*backports=>be more precise!*/
	hook_remove(hook_addr);
#endif
	allow_writes(); scTab[__NR_batch] = sys_oldcall0; disallow_writes();
	/* Presently unsafe to unload while any sys_batch is in progress. */
	printk(KERN_INFO "batch: removed\n");
}
module_init(mod_init);
module_exit(mod_cleanup);
MODULE_LICENSE("GPL");
