/*
 * Based on arch/arm/kernel/traps.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bug.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>

#include <asm/atomic.h>
#include <asm/bug.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/insn.h>
#include <asm/traps.h>
#include <asm/stacktrace.h>
#include <asm/exception.h>
#include <asm/system_misc.h>
#include <asm/esr.h>
#include <asm/edac.h>
#ifdef CONFIG_SEC_DEBUG
#include <linux/qcom/sec_debug.h>
#endif
#ifdef CONFIG_SEC_DEBUG_SUMMARY
#include <linux/qcom/sec_debug_summary.h>
#endif

#include <trace/events/exception.h>

#ifdef CONFIG_RKP_CFP_ROPP
#include <linux/rkp_cfp.h>
#endif

static const char *handler[]= {
	"Synchronous Abort",
	"IRQ",
	"FIQ",
	"Error"
};

int show_unhandled_signals = 1;

/*
 * Dump out the contents of some memory nicely...
 */
 static void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top)
{
	unsigned long first;
	mm_segment_t fs;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	printk("%s%s(0x%016lx to 0x%016lx)\n", lvl, str, bottom, top);

	for (first = bottom & ~31; first < top; first += 32) {
		unsigned long p;
		char str[sizeof(" 12345678") * 8 + 1];

		memset(str, ' ', sizeof(str));
		str[sizeof(str) - 1] = '\0';

		for (p = first, i = 0; i < 8 && p < top; i++, p += 4) {
			if (p >= bottom && p < top) {
				unsigned int val;
				if (__get_user(val, (unsigned int *)p) == 0)
					sprintf(str + i * 9, " %08x", val);
				else
					sprintf(str + i * 9, " ????????");
			}
		}
		printk("%s%04lx:%s\n", lvl, first & 0xffff, str);
	}

	set_fs(fs);
}

static void dump_backtrace_entry(unsigned long where, unsigned long stack)
{
	print_ip_sym(where);
	if (in_exception_text(where))
		dump_mem("", "Exception stack", stack,
			 stack + sizeof(struct pt_regs));
}

#ifdef CONFIG_USER_RESET_DEBUG
void sec_debug_backtrace(void)
{
	static int once = 0;
	struct stackframe frame;
	int skip_callstack = 0;
#ifdef CONFIG_RKP_CFP_ROPP
	unsigned long where = 0x0;
#endif

	if (!once++) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)sec_debug_backtrace;

		while (1) {
			int ret;

			ret = unwind_frame(current, &frame);
			if (ret < 0)
				break;

			if (skip_callstack++ > 3) {
#ifdef CONFIG_RKP_CFP_ROPP
				where = frame.pc;
				if (where>>40 != 0xffffff){
					where = ropp_enable_backtrace(where, current);
				}
				_sec_debug_store_backtrace(where);
#else
				_sec_debug_store_backtrace(frame.pc);
#endif
			}
		}
	}
}
EXPORT_SYMBOL(sec_debug_backtrace);
#endif

static void dump_instr(const char *lvl, struct pt_regs *regs)
{
	unsigned long addr = instruction_pointer(regs);
	mm_segment_t fs;
	char str[sizeof("00000000 ") * 5 + 2 + 1], *p = str;
	int i;

	/*
	 * We need to switch to kernel mode so that we can use __get_user
	 * to safely read from kernel space.  Note that we now dump the
	 * code first, just in case the backtrace kills us.
	 */
	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = -4; i < 1; i++) {
		unsigned int val, bad;

		bad = __get_user(val, &((u32 *)addr)[i]);

		if (!bad)
			p += sprintf(p, i == 0 ? "(%08x) " : "%08x ", val);
		else {
			p += sprintf(p, "bad PC value");
			break;
		}
	}
	printk("%sCode: %s\n", lvl, str);

	set_fs(fs);
}

static void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
	unsigned long irq_stack_ptr;
	int skip;
#ifdef CONFIG_RKP_CFP_ROPP
	volatile unsigned long init_pc = 0x0;
#endif

	/*
	 * Switching between stacks is valid when tracing current and in
	 * non-preemptible context.
	 */
	if (tsk == current && !preemptible())
		irq_stack_ptr = IRQ_STACK_PTR(smp_processor_id());
	else
		irq_stack_ptr = 0;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (!tsk)
		tsk = current;

	if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)dump_backtrace;
	} else {
		/*
		 * task blocked in __switch_to
		 */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.pc = thread_saved_pc(tsk);
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = tsk->curr_ret_stack;
#endif

	skip = !!regs;

#ifdef CONFIG_RKP_CFP_ROPP
#ifdef CONFIG_RKP_CFP_TEST
	asm volatile( "mrs %0, "STR(RRMK)"\n\t" : "=r" (init_pc));
	printk("CFP_TEST MK= %lx\n", init_pc);
	printk("CFP_TEST RRK=%lx TK=%lx\n", task_thread_info(tsk)->rrk, task_thread_info(tsk)->rrk ^ init_pc);
#endif
	init_pc = frame.pc;
#endif //CONFIG_RKP_CFP_ROPP
	printk("Call trace:\n");
	while (1) {
		unsigned long where = frame.pc;
		unsigned long stack;
		int ret;

		/* skip until specified stack frame */
		if (!skip) {
			where = frame.pc;
		} else if (frame.fp == regs->regs[29]) {
			skip = 0;
			/*
			 * Mostly, this is the case where this function is
			 * called in panic/abort. As exception handler's
			 * stack frame does not contain the corresponding pc
			 * at which an exception has taken place, use regs->pc
			 * instead.
			 */
			where = regs->pc;
		}

		ret = unwind_frame(tsk, &frame);
		if (ret < 0)
			break;

		stack = frame.sp;
#ifdef CONFIG_RKP_CFP_ROPP
		if (where != init_pc && (where>>40 != 0xffffff)){
			where = ropp_enable_backtrace(where, tsk);
		}
#endif
		if (in_exception_text(where)) {
			/*
			 * If we switched to the irq_stack before calling this
			 * exception handler, then the pt_regs will be on the
			 * task stack. The easiest way to tell is if the large
			 * pt_regs would overlap with the end of the irq_stack.
			 */
			if (stack < irq_stack_ptr &&
			    (stack + sizeof(struct pt_regs)) > irq_stack_ptr)
				stack = IRQ_STACK_TO_TASK_STACK(irq_stack_ptr);

			dump_mem("", "Exception stack", stack,
				 stack + sizeof(struct pt_regs));
		}
		dump_backtrace_entry(where, stack);
	}
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	dump_backtrace(NULL, tsk);
	barrier();
}

#ifdef CONFIG_PREEMPT
#define S_PREEMPT " PREEMPT"
#else
#define S_PREEMPT ""
#endif
#define S_SMP " SMP"

static int __die(const char *str, int err, struct thread_info *thread,
		 struct pt_regs *regs)
{
	struct task_struct *tsk = thread->task;
	static int die_counter;
	int ret;

	pr_emerg("Internal error: %s: %x [#%d]" S_PREEMPT S_SMP "\n",
		 str, err, ++die_counter);

	/* trap and error numbers are mostly meaningless on ARM */
	ret = notify_die(DIE_OOPS, str, regs, err, 0, SIGSEGV);
	if (ret == NOTIFY_STOP)
		return ret;

	print_modules();
	__show_regs(regs);
	pr_emerg("Process %.*s (pid: %d, stack limit = 0x%p)\n",
		 TASK_COMM_LEN, tsk->comm, task_pid_nr(tsk), thread + 1);

	if (!user_mode(regs) || in_interrupt()) {
#ifdef CONFIG_SEC_DEBUG
		if (THREAD_SIZE + (unsigned long)task_stack_page(tsk) - regs->sp
			> THREAD_SIZE) {
			dump_mem(KERN_EMERG, "Stack: ", regs->sp,
					THREAD_SIZE/4 + regs->sp);
		} else {
			dump_mem(KERN_EMERG, "Stack: ", regs->sp,
					THREAD_SIZE + (unsigned long)task_stack_page(tsk));
		}
#endif

		dump_backtrace(regs, tsk);
		dump_instr(KERN_EMERG, regs);
	}

	return ret;
}

static arch_spinlock_t die_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

static unsigned long oops_begin(void)
{
	int cpu;
	unsigned long flags;

	oops_enter();
#ifdef CONFIG_SEC_DEBUG
	secdbg_sched_msg("!!die!!");
#endif

	/* racy, but better than risking deadlock. */
	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	if (!arch_spin_trylock(&die_lock)) {
		if (cpu == die_owner)
			/* nested oops. should stop eventually */;
		else
			arch_spin_lock(&die_lock);
	}
	die_nest_count++;
	die_owner = cpu;
	console_verbose();
	bust_spinlocks(1);
	return flags;
}

static void oops_end(unsigned long flags, struct pt_regs *regs, int notify)
{
	if (regs && kexec_should_crash(current))
		crash_kexec(regs);

	bust_spinlocks(0);
	die_owner = -1;
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	die_nest_count--;
	if (!die_nest_count)
		/* Nest count reaches zero, release the lock. */
		arch_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (notify != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

/*
 * This function is protected against re-entrancy.
 */
void die(const char *str, struct pt_regs *regs, int err)
{
	struct thread_info *thread = current_thread_info();
	enum bug_trap_type bug_type = BUG_TRAP_TYPE_NONE;
	unsigned long flags = oops_begin();
	int ret;

	if (!user_mode(regs))
		bug_type = report_bug(regs->pc, regs);
	if (bug_type != BUG_TRAP_TYPE_NONE)
		str = "Oops - BUG";

#ifdef CONFIG_SEC_DEBUG_SUMMARY
	sec_debug_save_die_info(str, regs);
#endif

	ret = __die(str, err, thread, regs);

	oops_end(flags, regs, ret);
}

void arm64_notify_die(const char *str, struct pt_regs *regs,
		      struct siginfo *info, int err)
{
	if (user_mode(regs)) {
		current->thread.fault_address = 0;
		current->thread.fault_code = err;
		force_sig_info(info->si_signo, info, current);
	} else {
		die(str, regs, err);
	}
}

static LIST_HEAD(undef_hook);
static DEFINE_RAW_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_add(&hook->node, &undef_hook);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_del(&hook->node);
	raw_spin_unlock_irqrestore(&undef_lock, flags);
}

static int call_undef_hook(struct pt_regs *regs)
{
	struct undef_hook *hook;
	unsigned long flags;
	u32 instr;
	int (*fn)(struct pt_regs *regs, u32 instr) = NULL;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!user_mode(regs))
		return 1;

	if (compat_thumb_mode(regs)) {
		/* 16-bit Thumb instruction */
		if (get_user(instr, (u16 __user *)pc))
			goto exit;
		instr = le16_to_cpu(instr);
		if (aarch32_insn_is_wide(instr)) {
			u32 instr2;

			if (get_user(instr2, (u16 __user *)(pc + 2)))
				goto exit;
			instr2 = le16_to_cpu(instr2);
			instr = (instr << 16) | instr2;
		}
	} else {
		/* 32-bit ARM instruction */
		if (get_user(instr, (u32 __user *)pc))
			goto exit;
		instr = le32_to_cpu(instr);
	}

	raw_spin_lock_irqsave(&undef_lock, flags);
	list_for_each_entry(hook, &undef_hook, node)
		if ((instr & hook->instr_mask) == hook->instr_val &&
			(regs->pstate & hook->pstate_mask) == hook->pstate_val)
			fn = hook->fn;

	raw_spin_unlock_irqrestore(&undef_lock, flags);
exit:
	return fn ? fn(regs, instr) : 1;
}

asmlinkage void __exception do_undefinstr(struct pt_regs *regs)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);

	/* check for AArch32 breakpoint instructions */
	if (!aarch32_break_handler(regs))
		return;

	if (call_undef_hook(regs) == 0)
		return;

	trace_undef_instr(regs, (void *)pc);

	if (unhandled_signal(current, SIGILL) && show_unhandled_signals_ratelimited()) {
		pr_info("%s[%d]: undefined instruction: pc=%p\n",
			current->comm, task_pid_nr(current), pc);
		dump_instr(KERN_INFO, regs);
	}

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	arm64_notify_die("Oops - undefined instruction", regs, &info, 0);
}

long compat_arm_syscall(struct pt_regs *regs);

asmlinkage long do_ni_syscall(struct pt_regs *regs)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	if (show_unhandled_signals_ratelimited()) {
		pr_info("%s[%d]: syscall %d\n", current->comm,
			task_pid_nr(current), (int)regs->syscallno);
		dump_instr("", regs);
		if (user_mode(regs))
			__show_regs(regs);
	}

	return sys_ni_syscall();
}

static const char *esr_class_str[] = {
	[0 ... ESR_ELx_EC_MAX]		= "UNRECOGNIZED EC",
	[ESR_ELx_EC_UNKNOWN]		= "Unknown/Uncategorized",
	[ESR_ELx_EC_WFx]		= "WFI/WFE",
	[ESR_ELx_EC_CP15_32]		= "CP15 MCR/MRC",
	[ESR_ELx_EC_CP15_64]		= "CP15 MCRR/MRRC",
	[ESR_ELx_EC_CP14_MR]		= "CP14 MCR/MRC",
	[ESR_ELx_EC_CP14_LS]		= "CP14 LDC/STC",
	[ESR_ELx_EC_FP_ASIMD]		= "ASIMD",
	[ESR_ELx_EC_CP10_ID]		= "CP10 MRC/VMRS",
	[ESR_ELx_EC_CP14_64]		= "CP14 MCRR/MRRC",
	[ESR_ELx_EC_ILL]		= "PSTATE.IL",
	[ESR_ELx_EC_SVC32]		= "SVC (AArch32)",
	[ESR_ELx_EC_HVC32]		= "HVC (AArch32)",
	[ESR_ELx_EC_SMC32]		= "SMC (AArch32)",
	[ESR_ELx_EC_SVC64]		= "SVC (AArch64)",
	[ESR_ELx_EC_HVC64]		= "HVC (AArch64)",
	[ESR_ELx_EC_SMC64]		= "SMC (AArch64)",
	[ESR_ELx_EC_SYS64]		= "MSR/MRS (AArch64)",
	[ESR_ELx_EC_IMP_DEF]		= "EL3 IMP DEF",
	[ESR_ELx_EC_IABT_LOW]		= "IABT (lower EL)",
	[ESR_ELx_EC_IABT_CUR]		= "IABT (current EL)",
	[ESR_ELx_EC_PC_ALIGN]		= "PC Alignment",
	[ESR_ELx_EC_DABT_LOW]		= "DABT (lower EL)",
	[ESR_ELx_EC_DABT_CUR]		= "DABT (current EL)",
	[ESR_ELx_EC_SP_ALIGN]		= "SP Alignment",
	[ESR_ELx_EC_FP_EXC32]		= "FP (AArch32)",
	[ESR_ELx_EC_FP_EXC64]		= "FP (AArch64)",
	[ESR_ELx_EC_SERROR]		= "SError",
	[ESR_ELx_EC_BREAKPT_LOW]	= "Breakpoint (lower EL)",
	[ESR_ELx_EC_BREAKPT_CUR]	= "Breakpoint (current EL)",
	[ESR_ELx_EC_SOFTSTP_LOW]	= "Software Step (lower EL)",
	[ESR_ELx_EC_SOFTSTP_CUR]	= "Software Step (current EL)",
	[ESR_ELx_EC_WATCHPT_LOW]	= "Watchpoint (lower EL)",
	[ESR_ELx_EC_WATCHPT_CUR]	= "Watchpoint (current EL)",
	[ESR_ELx_EC_BKPT32]		= "BKPT (AArch32)",
	[ESR_ELx_EC_VECTOR32]		= "Vector catch (AArch32)",
	[ESR_ELx_EC_BRK64]		= "BRK (AArch64)",
};

const char *esr_get_class_string(u32 esr)
{
	return esr_class_str[esr >> ESR_ELx_EC_SHIFT];
}

/*
 * bad_mode handles the impossible case in the exception vector.
 */
asmlinkage void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	siginfo_t info;
	void __user *pc = (void __user *)instruction_pointer(regs);
	console_verbose();

	sec_debug_save_badmode_info(reason, handler[reason],
			esr, esr_get_class_string(esr));

	pr_crit("Bad mode in %s handler detected, code 0x%08x -- %s\n",
		handler[reason], esr, esr_get_class_string(esr));
	__show_regs(regs);

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code  = ILL_ILLOPC;
	info.si_addr  = pc;

	if (esr >> ESR_ELx_EC_SHIFT == ESR_ELx_EC_SERROR) {
		pr_crit("System error detected. ESR.ISS = %08x\n",
			esr & 0xffffff);
		arm64_check_cache_ecc(NULL);
	}

	arm64_notify_die("Oops - bad mode", regs, &info, 0);
}

void __pte_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pte %016lx.\n", file, line, val);
}

void __pmd_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pmd %016lx.\n", file, line, val);
}

void __pud_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pud %016lx.\n", file, line, val);
}

void __pgd_error(const char *file, int line, unsigned long val)
{
	pr_err("%s:%d: bad pgd %016lx.\n", file, line, val);
}

/* GENERIC_BUG traps */

int is_valid_bugaddr(unsigned long addr)
{
	/*
	 * bug_handler() only called for BRK #BUG_BRK_IMM.
	 * So the answer is trivial -- any spurious instances with no
	 * bug table entry will be rejected by report_bug() and passed
	 * back to the debug-monitors code and handled as a fatal
	 * unexpected debug exception.
	 */
	return 1;
}

static int bug_handler(struct pt_regs *regs, unsigned int esr)
{
	if (user_mode(regs))
		return DBG_HOOK_ERROR;

	switch (report_bug(regs->pc, regs)) {
	case BUG_TRAP_TYPE_BUG:
		die("Oops - BUG", regs, 0);
		break;

	case BUG_TRAP_TYPE_WARN:
		/* Ideally, report_bug() should backtrace for us... but no. */
		dump_backtrace(regs, NULL);
		break;

	default:
		/* unknown/unrecognised bug trap type */
		return DBG_HOOK_ERROR;
	}

	/* If thread survives, skip over the BUG instruction and continue: */
	regs->pc += AARCH64_INSN_SIZE;	/* skip BRK and resume */
	return DBG_HOOK_HANDLED;
}

static struct break_hook bug_break_hook = {
	.esr_val = 0xf2000000 | BUG_BRK_IMM,
	.esr_mask = 0xffffffff,
	.fn = bug_handler,
};

/*
 * Initial handler for AArch64 BRK exceptions
 * This handler only used until debug_traps_init().
 */
int __init early_brk64(unsigned long addr, unsigned int esr,
		struct pt_regs *regs)
{
	return bug_handler(regs, esr) != DBG_HOOK_HANDLED;
}

/* This registration must happen early, before debug_traps_init(). */
void __init trap_init(void)
{
	register_break_hook(&bug_break_hook);
}
