/*
 * sec_debug.c
 *
 * driver supporting debug functions for Samsung device
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2011 All Right Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/sysrq.h>
#include <asm/cacheflush.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/qcom/sec_debug.h>
#include <linux/qcom/sec_debug_summary.h>
//#include <mach/msm_iomap.h>
#include <linux/of_address.h>
#ifdef CONFIG_SEC_DEBUG_LOW_LOG
#include <linux/seq_file.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#endif
#include <linux/debugfs.h>
//#include <asm/system_info.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mount.h>
#include <linux/utsname.h>
#include <linux/seq_file.h>
#include <linux/nmi.h>
#include <soc/qcom/smem.h>

#ifdef CONFIG_HOTPLUG_CPU
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/preempt.h>
#endif

#include <soc/qcom/scm.h>

#if defined(CONFIG_ARCH_MSM8974) || defined(CONFIG_ARCH_MSM8226)
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#endif

#ifdef CONFIG_PROC_AVC
#include <linux/proc_avc.h>
#endif

#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/compiler.h>
#include <linux/init.h>

#include <linux/qcom/sec_debug_partition.h>
#ifdef CONFIG_USER_RESET_DEBUG
#include <linux/sec_param.h>
#endif

#define _TZ_SVC_DUMP 3 
#define _TZ_OWNER_SIP 2 
#define _TZ_SYSCALL_CREATE_SMC_ID(o, s, f) ((uint32_t)((((o & 0x3f) << 24) | (s & 0xff) << 8) | (f & 0xff))) 
#define _TZ_DUMP_SECURITY_ALLOWS_MEM_DUMP_ID _TZ_SYSCALL_CREATE_SMC_ID(_TZ_OWNER_SIP, _TZ_SVC_DUMP, 0x10) 
#define _TZ_DUMP_SECURITY_ALLOWS_MEM_DUMP_ID_PARAM_ID 0 

enum sec_debug_upload_cause_t {
	UPLOAD_CAUSE_INIT = 0xCAFEBABE,
	UPLOAD_CAUSE_KERNEL_PANIC = 0x000000C8,
	UPLOAD_CAUSE_POWER_LONG_PRESS = 0x00000085,
	UPLOAD_CAUSE_FORCED_UPLOAD = 0x00000022,
	UPLOAD_CAUSE_CP_ERROR_FATAL = 0x000000CC,
	UPLOAD_CAUSE_MDM_ERROR_FATAL = 0x000000EE,
	UPLOAD_CAUSE_USER_FAULT = 0x0000002F,
	UPLOAD_CAUSE_HSIC_DISCONNECTED = 0x000000DD,
	UPLOAD_CAUSE_MODEM_RST_ERR = 0x000000FC,
	UPLOAD_CAUSE_RIVA_RST_ERR = 0x000000FB,
	UPLOAD_CAUSE_LPASS_RST_ERR = 0x000000FA,
	UPLOAD_CAUSE_DSPS_RST_ERR = 0x000000FD,
	UPLOAD_CAUSE_PERIPHERAL_ERR = 0x000000FF,
	UPLOAD_CAUSE_NON_SECURE_WDOG_BARK = 0x00000DBA,
	UPLOAD_CAUSE_NON_SECURE_WDOG_BITE = 0x00000DBE,
    UPLOAD_CAUSE_POWER_THERMAL_RESET = 0x00000075,
	UPLOAD_CAUSE_SECURE_WDOG_BITE = 0x00005DBE,
	UPLOAD_CAUSE_BUS_HANG = 0x000000B5,
#if defined(CONFIG_SEC_NAD)
	UPLOAD_CAUSE_NAD_SRTEST = 0x00000A3E,
	UPLOAD_CAUSE_NAD_QMVSDDR = 0x00000A29,
	UPLOAD_CAUSE_NAD_QMVSCACHE = 0x00000AED,
	UPLOAD_CAUSE_NAD_PMIC = 0x00000AB8,
	UPLOAD_CAUSE_NAD_FAIL = 0x00000A65,
#endif
};


struct sec_debug_log *secdbg_log;

struct debug_reset_header *summary_info = NULL;
char *summary_buf = NULL;
struct debug_reset_header *klog_info = NULL;
struct debug_reset_header *tzlog_info = NULL;
char *klog_read_buf = NULL;
char *klog_buf = NULL;
char *tzlog_buf = NULL;
uint32_t klog_size = 0;

/* enable sec_debug feature */
static unsigned enable = 1;
static unsigned enable_user = 1;
static unsigned reset_reason = 0xFFEEFFEE;
static int reset_write_cnt = -1;
uint64_t secdbg_paddr;
unsigned int secdbg_size;
#ifdef CONFIG_SEC_SSR_DEBUG_LEVEL_CHK
static unsigned enable_cp_debug = 1;
#endif
#ifdef CONFIG_ARCH_MSM8974PRO
static unsigned pmc8974_rev = 0;
#else
static unsigned pm8941_rev = 0;
static unsigned pm8841_rev = 0;
#endif
unsigned int sec_dbg_level;
static unsigned secure_dump = 0;
static unsigned rp_enabled = 0;

uint runtime_debug_val;
module_param_named(enable, enable, uint, 0644);
module_param_named(enable_user, enable_user, uint, 0644);
module_param_named(runtime_debug_val, runtime_debug_val, uint, 0644);
module_param_named(rp_enabled, rp_enabled, uint, 0644);
#ifdef CONFIG_SEC_SSR_DEBUG_LEVEL_CHK
module_param_named(enable_cp_debug, enable_cp_debug, uint, 0644);
#endif

module_param_named(pm8941_rev, pm8941_rev, uint, 0644);
module_param_named(pm8841_rev, pm8841_rev, uint, 0644);
static int force_error(const char *val, struct kernel_param *kp);
module_param_call(force_error, force_error, NULL, NULL, 0644);

static int sec_alloc_virtual_mem(const char *val, struct kernel_param *kp);
module_param_call(alloc_virtual_mem, sec_alloc_virtual_mem, NULL, NULL, 0644);

static int sec_free_virtual_mem(const char *val, struct kernel_param *kp);
module_param_call(free_virtual_mem, sec_free_virtual_mem, NULL, NULL, 0644);

static int sec_alloc_physical_mem(const char *val, struct kernel_param *kp);
module_param_call(alloc_physical_mem, sec_alloc_physical_mem, NULL, NULL, 0644);

static int sec_free_physical_mem(const char *val, struct kernel_param *kp);
module_param_call(free_physical_mem, sec_free_physical_mem, NULL, NULL, 0644);

static int dbg_set_cpu_affinity(const char *val, struct kernel_param *kp);
module_param_call(setcpuaff, dbg_set_cpu_affinity, NULL, NULL, 0644);

/* klaatu - schedule log */
void __iomem *restart_reason=NULL;  /* This is shared with msm-power off  module. */
static void __iomem *upload_cause=NULL;

DEFINE_PER_CPU(struct sec_debug_core_t, sec_debug_core_reg);
DEFINE_PER_CPU(struct sec_debug_mmu_reg_t, sec_debug_mmu_reg);
DEFINE_PER_CPU(enum sec_debug_upload_cause_t, sec_debug_upload_cause);


/* save last_pet and last_ns with these nice functions */
void sec_debug_save_last_pet(unsigned long long last_pet)
{
	if(secdbg_log)
		secdbg_log->last_pet = last_pet;
}

void sec_debug_save_last_ns(unsigned long long last_ns)
{
	if(secdbg_log)
		atomic64_set(&(secdbg_log->last_ns), last_ns);
}
EXPORT_SYMBOL(sec_debug_save_last_pet);
EXPORT_SYMBOL(sec_debug_save_last_ns);

#ifdef CONFIG_HOTPLUG_CPU
static void pull_down_other_cpus(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		if(cpu == 0)
			continue;
		cpu_down(cpu);
	}
}
#else
static void pull_down_other_cpus(void)
{
}
#endif

/* timeout for dog bark/bite */
#define DELAY_TIME 20000

static void simulate_apps_wdog_bark(void)
{
	pr_emerg("Simulating apps watch dog bark\n");
	preempt_disable();
	mdelay(DELAY_TIME);
	preempt_enable();
	/* if we reach here, simulation failed */
	pr_emerg("Simulation of apps watch dog bark failed\n");
}

static void simulate_apps_wdog_bite(void)
{
	pull_down_other_cpus();
	pr_emerg("Simulating apps watch dog bite\n");
	local_irq_disable();
	mdelay(DELAY_TIME);
	local_irq_enable();
	/* if we reach here, simulation had failed */
	pr_emerg("Simualtion of apps watch dog bite failed\n");
}

#ifdef CONFIG_SEC_DEBUG_SEC_WDOG_BITE

#define SCM_SVC_SEC_WDOG_TRIG	0x8

static int simulate_secure_wdog_bite(void)
{	int ret;
	struct scm_desc desc = {
		.args[0] = 0,
		.arginfo = SCM_ARGS(1),
	};
	pr_emerg("simulating secure watch dog bite\n");
	if (!is_scm_armv8())
		ret = scm_call_atomic2(SCM_SVC_BOOT,
					       SCM_SVC_SEC_WDOG_TRIG, 0, 0);
	else
		ret = scm_call2(SCM_SIP_FNID(SCM_SVC_BOOT,
						  SCM_SVC_SEC_WDOG_TRIG), &desc);
	/* if we hit, scm_call has failed */
	pr_emerg("simulation of secure watch dog bite failed\n");
	return ret;
}
#else
int simulate_secure_wdog_bite(void)
{
	return 0;
}
#endif

#if defined(CONFIG_ARCH_MSM8226) || defined(CONFIG_ARCH_MSM8974)
/*
 * Misc data structures needed for simulating bus timeout in
 * camera
 */

#define HANG_ADDRESS 0xfda10000

struct clk_pair {
	const char *dev;
	const char *clk;
};

static struct clk_pair bus_timeout_camera_clocks_on[] = {
	/*
	 * gcc_mmss_noc_cfg_ahb_clk should be on but right
	 * now this clock is on by default and not accessable.
	 * Update this table if gcc_mmss_noc_cfg_ahb_clk is
	 * ever not enabled by default!
	 */
	{
		.dev = "fda0c000.qcom,cci",
		.clk = "camss_top_ahb_clk",
	},
	{
		.dev = "fda10000.qcom,vfe",
		.clk = "iface_clk",
	},
};

static struct clk_pair bus_timeout_camera_clocks_off[] = {
	{
		.dev = "fda10000.qcom,vfe",
		.clk = "camss_vfe_vfe_clk",
	}
};

static void bus_timeout_clk_access(struct clk_pair bus_timeout_clocks_off[],
				struct clk_pair bus_timeout_clocks_on[],
				int off_size, int on_size)
{
	int i;

	/*
	 * Yes, none of this cleans up properly but the goal here
	 * is to trigger a hang which is going to kill the rest of
	 * the system anyway
	 */

	for (i = 0; i < on_size; i++) {
		struct clk *this_clock;

		this_clock = clk_get_sys(bus_timeout_clocks_on[i].dev,
					bus_timeout_clocks_on[i].clk);
		if (!IS_ERR(this_clock))
			if (clk_prepare_enable(this_clock))
				pr_warn("Device %s: Clock %s not enabled",
					bus_timeout_clocks_on[i].clk,
					bus_timeout_clocks_on[i].dev);
	}

	for (i = 0; i < off_size; i++) {
		struct clk *this_clock;

		this_clock = clk_get_sys(bus_timeout_clocks_off[i].dev,
					 bus_timeout_clocks_off[i].clk);
		if (!IS_ERR(this_clock))
			clk_disable_unprepare(this_clock);
	}
}

static void simulate_bus_hang(void)
{
	/* This simulates bus timeout on camera */
	int ret = 0;
	uint32_t dummy_value = 0;
	uint32_t address = HANG_ADDRESS;
	void *hang_address = NULL;
	struct regulator *r = NULL;

	/* simulate */
	hang_address = ioremap(address, SZ_4K);
	r = regulator_get(NULL, "gdsc_vfe");
	ret = IS_ERR(r);
	if(!ret)
		regulator_enable(r);
	else
		pr_emerg("Unable to get regulator reference\n");

	bus_timeout_clk_access(bus_timeout_camera_clocks_off,
				bus_timeout_camera_clocks_on,
				ARRAY_SIZE(bus_timeout_camera_clocks_off),
				ARRAY_SIZE(bus_timeout_camera_clocks_on));

	dummy_value = readl_relaxed(hang_address);
	mdelay(DELAY_TIME);
	/* if we hit here, test had failed */
	pr_emerg("Bus timeout test failed...0x%x\n", dummy_value);
	iounmap(hang_address);
}
#else
static void simulate_bus_hang(void)
{
	void __iomem *p;
	pr_emerg("Generating Bus Hang!\n");
	p = ioremap_nocache(0xFC4B8000, 32);
	*(unsigned int *)p = *(unsigned int *)p;
	mb();
	pr_info("*p = %x\n", *(unsigned int *)p);
	pr_emerg("Clk may be enabled.Try again if it reaches here!\n");
}
#endif

static int force_error(const char *val, struct kernel_param *kp)
{
	pr_emerg("!!!WARN forced error : %s\n", val);

	if (!strncmp(val, "appdogbark", 10)) {
		pr_emerg("Generating an apps wdog bark!\n");
		simulate_apps_wdog_bark();
	} else if (!strncmp(val, "appdogbite", 10)) {
		pr_emerg("Generating an apps wdog bite!\n");
		simulate_apps_wdog_bite();
	} else if (!strncmp(val, "dabort", 6)) {
		pr_emerg("Generating a data abort exception!\n");
		*(unsigned int *)0x0 = 0x0;
	} else if (!strncmp(val, "pabort", 6)) {
		pr_emerg("Generating a prefetch abort exception!\n");
		((void (*)(void))0x0)();
	} else if (!strncmp(val, "undef", 5)) {
		pr_emerg("Generating a undefined instruction exception!\n");
		BUG();
	} else if (!strncmp(val, "bushang", 7)) {
		pr_emerg("Generating a Bus Hang!\n");
		simulate_bus_hang();
	} else if (!strncmp(val, "dblfree", 7)) {
		void *p = kmalloc(sizeof(int), GFP_KERNEL);
		kfree(p);
		msleep(1000);
		kfree(p);
	} else if (!strncmp(val, "danglingref", 11)) {
		unsigned int *p = kmalloc(sizeof(int), GFP_KERNEL);
		kfree(p);
		*p = 0x1234;
	} else if (!strncmp(val, "lowmem", 6)) {
		int i = 0;
		pr_emerg("Allocating memory until failure!\n");
		while (kmalloc(128*1024, GFP_KERNEL))
			i++;
		pr_emerg("Allocated %d KB!\n", i*128);

	} else if (!strncmp(val, "memcorrupt", 10)) {
		int *ptr = kmalloc(sizeof(int), GFP_KERNEL);
		*ptr++ = 4;
		*ptr = 2;
		panic("MEMORY CORRUPTION");
#ifdef CONFIG_SEC_DEBUG_SEC_WDOG_BITE
	}else if (!strncmp(val, "secdogbite", 10)) {
		simulate_secure_wdog_bite();
#endif
#ifdef CONFIG_USER_RESET_DEBUG_TEST
	} else if (!strncmp(val, "TP", 2)) {
		force_thermal_reset();
	} else if (!strncmp(val, "KP", 2)) {
		pr_emerg("Generating a data abort exception!\n");
		*(unsigned int *)0x0 = 0x0;
	} else if (!strncmp(val, "DP", 2)) {
		force_watchdog_bark();
	} else if (!strncmp(val, "WP", 2)) {
		simulate_secure_wdog_bite();
#endif
#ifdef CONFIG_FREE_PAGES_RDONLY
	}else if (!strncmp(val, "pageRDcheck", 11)) {
		struct page *page = alloc_pages(GFP_ATOMIC, 0);
		unsigned int *ptr = (unsigned int *)page_address(page);
		pr_emerg("Test with RD page configue");
		__free_pages(page,0);
		*ptr = 0x12345678;
#endif
	} else {
		pr_emerg("No such error defined for now!\n");
	}

	return 0;
}

static long * g_allocated_phys_mem = NULL;
static long * g_allocated_virt_mem = NULL;

static int sec_alloc_virtual_mem(const char *val, struct kernel_param *kp)
{
	long * mem;
	char * str = (char *) val;
	unsigned size = (unsigned) memparse(str, &str);
	if(size)
	{
		mem = vmalloc(size);
		if(mem)
		{
			pr_info("%s: Allocated virtual memory of size: 0x%X bytes\n", __func__, size);
			*mem = (long) g_allocated_virt_mem;
			g_allocated_virt_mem = mem;
			return 0;
		}
		else
		{
			pr_info("%s: Failed to allocate virtual memory of size: 0x%X bytes\n", __func__, size);
		}
	}

	pr_info("%s: Invalid size: %s bytes\n", __func__, val);

	return -EAGAIN;
}

static int sec_free_virtual_mem(const char *val, struct kernel_param *kp)
{
	long * mem;
	char * str = (char *) val;
        unsigned free_count = (unsigned) memparse(str, &str);

	if(!free_count)
	{
		if(strncmp(val, "all", 4))
		{
			free_count = 10;
		}
		else
		{
			pr_info("%s: Invalid free count: %s\n", __func__, val);
			return -EAGAIN;
		}
	}

	if(free_count>10)
		free_count = 10;

        if(!g_allocated_virt_mem)
        {
                pr_info("%s: No virtual memory chunk to free.\n", __func__);
                return 0;
        }

	while(g_allocated_virt_mem && free_count--)
	{
		mem = (long *) *g_allocated_virt_mem;
		vfree(g_allocated_virt_mem);
		g_allocated_virt_mem = mem;
	}

	pr_info("%s: Freed previously allocated virtual memory chunks.\n", __func__);

	if(g_allocated_virt_mem)
		pr_info("%s: Still, some virtual memory chunks are not freed. Try again.\n", __func__);

	return 0;
}

static int sec_alloc_physical_mem(const char *val, struct kernel_param *kp)
{
        long * mem;
	char * str = (char *) val;
        unsigned size = (unsigned) memparse(str, &str);
        if(size)
        {
                mem = kmalloc(size, GFP_KERNEL);
                if(mem)
                {
			pr_info("%s: Allocated physical memory of size: 0x%X bytes\n", __func__, size);
                        *mem = (long) g_allocated_phys_mem;
                        g_allocated_phys_mem = mem;
			return 0;
                }
		else
		{
			pr_info("%s: Failed to allocate physical memory of size: 0x%X bytes\n", __func__, size);
		}
        }

	pr_info("%s: Invalid size: %s bytes\n", __func__, val);

        return -EAGAIN;
}

static int sec_free_physical_mem(const char *val, struct kernel_param *kp)
{
        long * mem;
        char * str = (char *) val;
        unsigned free_count = (unsigned) memparse(str, &str);

        if(!free_count)
        {
                if(strncmp(val, "all", 4))
                {
                        free_count = 10;
                }
                else
                {
                        pr_info("%s: Invalid free count: %s\n", __func__, val);
                        return -EAGAIN;
                }
        }

        if(free_count>10)
                free_count = 10;

	if(!g_allocated_phys_mem)
	{
		pr_info("%s: No physical memory chunk to free.\n", __func__);
		return 0;
	}

        while(g_allocated_phys_mem && free_count--)
        {
                mem = (long *) *g_allocated_phys_mem;
                kfree(g_allocated_phys_mem);
                g_allocated_phys_mem = mem;
        }

	pr_info("%s: Freed previously allocated physical memory chunks.\n", __func__);

	if(g_allocated_phys_mem)
                pr_info("%s: Still, some physical memory chunks are not freed. Try again.\n", __func__);

	return 0;
}

static int dbg_set_cpu_affinity(const char *val, struct kernel_param *kp)
{
	char *endptr;
	pid_t pid;
	int cpu;
	struct cpumask mask;
	long ret;
	pid = (pid_t)memparse(val, &endptr);
	if (*endptr != '@') {
		pr_info("%s: invalid input strin: %s\n", __func__, val);
		return -EINVAL;
	}
	cpu = memparse(++endptr, &endptr);
	cpumask_clear(&mask);
	cpumask_set_cpu(cpu, &mask);
	pr_info("%s: Setting %d cpu affinity to cpu%d\n",
		__func__, pid, cpu);
	ret = sched_setaffinity(pid, &mask);
	pr_info("%s: sched_setaffinity returned %ld\n", __func__, ret);
	return 0;
}

/* for sec debug level */
static int __init sec_debug_level(char *str)
{
	get_option(&str, &sec_dbg_level);
	return 0;
}
early_param("level", sec_debug_level);



extern void set_dload_mode(int on);
static void sec_debug_set_qc_dload_magic(int on)
{
	pr_info("%s: on=%d\n", __func__, on);
	set_dload_mode(on);
}

static void sec_debug_set_upload_magic(unsigned magic)
{
	pr_err("(%s) %x\n", __func__, magic);

	if (magic)
		sec_debug_set_qc_dload_magic(1);
	__raw_writel(magic, restart_reason);

	flush_cache_all();
#ifndef CONFIG_ARM64	
	outer_flush_all();
#endif
}

static int sec_debug_normal_reboot_handler(struct notifier_block *nb,
		unsigned long l, void *p)
{
	pr_err("sec_debug_normal_reboot_handler.\n");

	sec_debug_set_upload_magic(0x0);
	return 0;
}

static void sec_debug_set_upload_cause(enum sec_debug_upload_cause_t type)
{

	if (!upload_cause) {
		pr_err("upload cause address unmapped.\n");
		return;
	}
	pr_err( "sec_debug_set_upload_cause : %0x\n", type);

	per_cpu(sec_debug_upload_cause, smp_processor_id()) = type;
	__raw_writel(type, upload_cause);

	pr_emerg("(%s) %x\n", __func__, type);
}

extern struct uts_namespace init_uts_ns;
void sec_debug_hw_reset(void)
{
	pr_emerg("(%s) %s %s\n", __func__, init_uts_ns.name.release,
						init_uts_ns.name.version);
	pr_emerg("(%s) rebooting...\n", __func__);
	flush_cache_all();
#ifndef CONFIG_ARM64
	outer_flush_all();
#endif
	do_msm_restart(0, "sec_debug_hw_reset");

	while (1)
		;
}
EXPORT_SYMBOL(sec_debug_hw_reset);

#ifdef CONFIG_SEC_PERIPHERAL_SECURE_CHK
void sec_peripheral_secure_check_fail(void)
{
	//sec_debug_set_upload_magic(0x77665507);
	sec_debug_set_qc_dload_magic(0);
	pr_emerg("(%s) %s %s\n", __func__, init_uts_ns.name.release,
						init_uts_ns.name.version);
	pr_emerg("(%s) rebooting...\n", __func__);
	flush_cache_all();
#ifndef CONFIG_ARM64
	outer_flush_all();
#endif
	do_msm_restart(0, "peripheral_hw_reset");

	while (1)
		;
}
EXPORT_SYMBOL(sec_peripheral_secure_check_fail);
#endif

void sec_debug_set_thermal_upload(void)
{
	pr_emerg("(%s) set thermal upload cause\n", __func__);
	sec_debug_set_upload_magic(0x776655ee);                      
	sec_debug_set_upload_cause(UPLOAD_CAUSE_POWER_THERMAL_RESET);
}
EXPORT_SYMBOL(sec_debug_set_thermal_upload);

unsigned sec_debug_get_reset_reason(void)
{
	return reset_reason;
}
EXPORT_SYMBOL(sec_debug_get_reset_reason);

int sec_debug_get_reset_write_cnt(void)
{
	return reset_write_cnt;
}
EXPORT_SYMBOL(sec_debug_get_reset_write_cnt);

#ifdef CONFIG_USER_RESET_DEBUG
extern void sec_debug_backtrace(void);

void _sec_debug_store_backtrace(unsigned long where)
{
	static int offset = 0;
	unsigned int max_size = 0;
	_kern_ex_info_t *p_ex_info;

	if (sec_debug_reset_ex_info) {
		p_ex_info = &sec_debug_reset_ex_info->kern_ex_info.info;
		max_size = (unsigned long long int)&sec_debug_reset_ex_info->rpm_ex_info.info
			- (unsigned long long int)p_ex_info->backtrace;

		if (max_size <= offset)
			return;

		if (offset)
			offset += snprintf(p_ex_info->backtrace+offset,
					 max_size-offset, " > ");

		offset += snprintf(p_ex_info->backtrace+offset, max_size-offset,
					"%pS", (void *)where);
	}
}

static inline void sec_debug_store_backtrace(void)
{
	unsigned long flags;

	local_irq_save(flags);
	sec_debug_backtrace();
	local_irq_restore(flags);
}
#endif


static int sec_debug_panic_handler(struct notifier_block *nb,
		unsigned long l, void *buf)
{
	unsigned int len, i;
	emerg_pet_watchdog();//CTC-should be modify

#ifdef CONFIG_USER_RESET_DEBUG
	sec_debug_store_backtrace();
#endif
	sec_debug_set_upload_magic(0x776655ee);
	
	pr_err( "sec_debug_panic_handler : %s\n", (char*)buf);

	len = strnlen(buf, 80);
	if (!strncmp(buf, "User Fault", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_USER_FAULT);
	else if (!strncmp(buf, "Crash Key", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_FORCED_UPLOAD);
	else if (!strncmp(buf, "CP Crash", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_CP_ERROR_FATAL);
	else if (!strncmp(buf, "MDM Crash", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_MDM_ERROR_FATAL);
	else if (strnstr(buf, "external_modem", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_MDM_ERROR_FATAL);
	else if (strnstr(buf, "esoc0 crashed", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_MDM_ERROR_FATAL);
	else if (strnstr(buf, "modem", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_MODEM_RST_ERR);
	else if (strnstr(buf, "riva", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_RIVA_RST_ERR);
	else if (strnstr(buf, "lpass", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_LPASS_RST_ERR);
	else if (strnstr(buf, "dsps", len) != NULL)
		sec_debug_set_upload_cause(UPLOAD_CAUSE_DSPS_RST_ERR);
	else if (!strnicmp(buf, "subsys", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_PERIPHERAL_ERR);
#if defined(CONFIG_SEC_NAD)
	else if (!strnicmp(buf, "nad_srtest", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_NAD_SRTEST);
	else if (!strnicmp(buf, "nad_qmesaddr", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_NAD_QMVSDDR);
	else if (!strnicmp(buf, "nad_qmesacache", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_NAD_QMVSCACHE);
	else if (!strnicmp(buf, "nad_pmic", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_NAD_PMIC);
	else if (!strnicmp(buf, "nad_fail", len))
		sec_debug_set_upload_cause(UPLOAD_CAUSE_NAD_FAIL);
#endif
	else
		sec_debug_set_upload_cause(UPLOAD_CAUSE_KERNEL_PANIC);

	if (!enable) {
#ifdef CONFIG_SEC_DEBUG_LOW_LOG
		sec_debug_hw_reset();
#endif
		/* SEC will get reset_summary.html in debug low.
		 * reset_summary.html need more information about abnormal reset or kernel panic.
		 * So we skip as below*/
		//return -EPERM;
	}	

/* enable after SSR feature
	ssr_panic_handler_for_sec_dbg();
*/
	for (i = 0; i < 10; i++) 
	{
	   touch_nmi_watchdog();
	   mdelay(100);
	}

	/* save context here so that function call after this point doesn't
	 * corrupt stacks below the saved sp */ 
	sec_debug_save_context();

	sec_debug_hw_reset();
	return 0;
}

void sec_debug_prepare_for_wdog_bark_reset(void)
{
#ifdef CONFIG_SEC_ISDBT_FORCE_OFF
	if (isdbt_force_off_callback)
		isdbt_force_off_callback();
#endif
	sec_debug_set_upload_magic(0x776655ee);
	sec_debug_set_upload_cause(UPLOAD_CAUSE_NON_SECURE_WDOG_BARK);
}

#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
struct tsp_dump_callbacks dump_callbacks;
#endif

void sec_debug_check_crash_key(unsigned int code, int value)
{
	static enum { NONE, STEP1, STEP2, STEP3} state = NONE;
#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
	static enum { NO, T1, T2, T3} state_tsp = NO;
#endif

	printk(KERN_ERR "%s code %d value %d state %d enable %d\n", __func__, code, value, state, enable);

	if (code == KEY_POWER) {
		if (value)
			sec_debug_set_upload_cause(UPLOAD_CAUSE_POWER_LONG_PRESS);
		else
			sec_debug_set_upload_cause(UPLOAD_CAUSE_INIT);
	}

	if (!enable)
		return;


#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
	if(code == KEY_VOLUMEUP && !value){
		 state_tsp = NO;
	} else {

		switch (state_tsp) {
		case NO:
			if (code == KEY_VOLUMEUP && value)
				state_tsp = T1;
			else
				state_tsp = NO;
			break;
		case T1:
			if (((code == KEY_HOMEPAGE) || (code == KEY_WINK)) && value)
				state_tsp = T2;
			else
				state_tsp = NO;
			break;
		case T2:
			if (((code == KEY_HOMEPAGE) || (code == KEY_WINK)) && !value)
				state_tsp = T3;
			else
				state_tsp = NO;
			break;
		case T3:
			if (((code == KEY_HOMEPAGE) || (code == KEY_WINK)) && value) {
				pr_info("[sec_input] dump_tsp_log : %d\n", __LINE__ );
				if (dump_callbacks.inform_dump)
					dump_callbacks.inform_dump();
			}
			break;
		}
	}
	
#endif

	switch (state) {
	case NONE:
		if (code == KEY_VOLUMEDOWN && value)
			state = STEP1;
		else
			state = NONE;
		break;
	case STEP1:
		if (code == KEY_POWER && value)
			state = STEP2;
		else
			state = NONE;
		break;
	case STEP2:
		if (code == KEY_POWER && !value)
			state = STEP3;
		else
			state = NONE;
		break;
	case STEP3:
		if (code == KEY_POWER && value) {
			emerg_pet_watchdog();
			dump_all_task_info();
			dump_cpu_stat();
			panic("Crash Key");
		} else {
			state = NONE;
		}
		break;
	}
}

static struct notifier_block nb_reboot_block = {
	.notifier_call = sec_debug_normal_reboot_handler
};

static struct notifier_block nb_panic_block = {
	.notifier_call = sec_debug_panic_handler,
	.priority = -1,
};


#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
static int __init __init_sec_debug_log(void)
{
	int i;
	struct sec_debug_log *vaddr;
	int size;

	if (secdbg_paddr == 0 || secdbg_size == 0) {
		pr_info("%s: sec debug buffer not provided. Using kmalloc..\n",
			__func__);
		size = sizeof(struct sec_debug_log);
		vaddr = kmalloc(size, GFP_KERNEL);
	} else {
		size = secdbg_size;
		vaddr = ioremap_nocache(secdbg_paddr, secdbg_size);
	}

	pr_info("%s: vaddr=0x%lx paddr=0x%llx size=0x%x "\
		"sizeof(struct sec_debug_log)=0x%lx\n", __func__,
		(unsigned long)vaddr, secdbg_paddr, secdbg_size,
		sizeof(struct sec_debug_log));

	if ((vaddr == NULL) || (sizeof(struct sec_debug_log) > size)) {
		pr_info("%s: ERROR! init failed!\n", __func__);
		return -EFAULT;
	}

	memset_io(vaddr->sched, 0x0, sizeof(vaddr->sched));
	memset_io(vaddr->irq, 0x0, sizeof(vaddr->irq));
	memset_io(vaddr->irq_exit, 0x0, sizeof(vaddr->irq_exit));
	memset_io(vaddr->timer_log, 0x0, sizeof(vaddr->timer_log));
	memset_io(vaddr->secure, 0x0, sizeof(vaddr->secure));
#ifdef CONFIG_SEC_DEBUG_MSGLOG
	memset_io(vaddr->secmsg, 0x0, sizeof(vaddr->secmsg));
#endif
#ifdef CONFIG_SEC_DEBUG_AVC_LOG
	memset_io(vaddr->secavc, 0x0, sizeof(vaddr->secavc));
#endif

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		atomic_set(&(vaddr->idx_sched[i]), -1);
		atomic_set(&(vaddr->idx_irq[i]), -1);
		atomic_set(&(vaddr->idx_secure[i]), -1);
		atomic_set(&(vaddr->idx_irq_exit[i]), -1);
		atomic_set(&(vaddr->idx_timer[i]), -1);

#ifdef CONFIG_SEC_DEBUG_MSG_LOG
		atomic_set(&(vaddr->idx_secmsg[i]), -1);
#endif
#ifdef CONFIG_SEC_DEBUG_AVC_LOG
		atomic_set(&(vaddr->idx_secavc[i]), -1);
#endif
	}
#ifdef CONFIG_SEC_DEBUG_DCVS_LOG
	for (i = 0; i < CONFIG_NR_CPUS; i++)
		atomic_set(&(vaddr->dcvs_log_idx[i]), -1);
#endif
#ifdef CONFIG_SEC_DEBUG_FUELGAUGE_LOG
		atomic_set(&(vaddr->fg_log_idx), -1);
#endif

	secdbg_log = vaddr;

	pr_info("%s: init done\n", __func__);

	return 0;
}
#endif

static int __init sec_dt_addr_init(void)
{
	struct device_node *np;

	/* Using bottom of sec_dbg DDR address range for writing restart reason */
	np = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-restart_reason");
	if (!np) {
		pr_err("unable to find DT imem restart reason node\n");
		return -ENODEV;
	}

	restart_reason = of_iomap(np, 0);
	if (!restart_reason) {
		pr_err("unable to map imem restart reason offset\n");
		return -ENODEV;
	}


	/* check restart_reason address here */
	pr_emerg("%s: restart_reason addr : 0x%lx(0x%x)\n", __func__,
		(unsigned long)restart_reason,(unsigned int)virt_to_phys(restart_reason));


	/* Using bottom of sec_dbg DDR address range for writing upload_cause */
	np = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-upload_cause");
	if (!np) {
		pr_err("unable to find DT imem upload cause node\n");
		return -ENODEV;
	}

	upload_cause = of_iomap(np, 0);
	if (!upload_cause) {
		pr_err("unable to map imem restart reason offset\n");
		return -ENODEV;
	}

	/* check upload_cause here */
	pr_emerg("%s: upload_cause addr : 0x%lx(0x%x)\n", __func__,
		(unsigned long)upload_cause,(unsigned int)virt_to_phys(upload_cause));

	return 0;
}

#define SCM_WDOG_DEBUG_BOOT_PART 0x9
void sec_do_bypass_sdi_execution_in_low(void)
{
	int ret;
	struct scm_desc desc = {
		.args[0] = 1,
		.args[1] = 0,
		.arginfo = SCM_ARGS(2),
	};

	/* Needed to bypass debug image on some chips */
	if (!is_scm_armv8())
		ret = scm_call_atomic2(SCM_SVC_BOOT,
				SCM_WDOG_DEBUG_BOOT_PART, 1, 0);
	else
		ret = scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT,
					SCM_WDOG_DEBUG_BOOT_PART), &desc);
	if (ret)
		pr_err("Failed to disable wdog debug: %d\n", ret);

}

#ifdef CONFIG_USER_RESET_DEBUG
static char rr_str[][3] = {
	[USER_UPLOAD_CAUSE_SMPL] = "SP",
	[USER_UPLOAD_CAUSE_WTSR] = "WP",
	[USER_UPLOAD_CAUSE_WATCHDOG] = "DP",
	[USER_UPLOAD_CAUSE_PANIC] = "KP",
	[USER_UPLOAD_CAUSE_MANUAL_RESET] = "MP",
	[USER_UPLOAD_CAUSE_POWER_RESET] = "PP",
	[USER_UPLOAD_CAUSE_REBOOT] = "RP",
	[USER_UPLOAD_CAUSE_BOOTLOADER_REBOOT] = "BP",
	[USER_UPLOAD_CAUSE_POWER_ON] = "NP",
	[USER_UPLOAD_CAUSE_THERMAL] = "TP",
	[USER_UPLOAD_CAUSE_UNKNOWN] = "NP",
};

char * sec_debug_get_reset_reason_str(unsigned int reason)
{
	if (reason >= USER_UPLOAD_CAUSE_MIN &&
		reason <= USER_UPLOAD_CAUSE_MAX) {
		return rr_str[reason];
	} else {
		return rr_str[USER_UPLOAD_CAUSE_UNKNOWN];
	}
}
EXPORT_SYMBOL(sec_debug_get_reset_reason_str);

void sec_debug_update_reset_reason(uint32_t debug_partition_rr)
{
	static char updated = 0;

	if (!updated) {
		reset_reason = debug_partition_rr;
		updated = 1;
		pr_info("%s : partition[%d] result[%s]\n", __func__, 
				debug_partition_rr, sec_debug_get_reset_reason_str(reset_reason));
	}

}

static void reset_reason_update_and_clear(void)
{
	ap_health_t *p_health;
	uint32_t rr_data;

	p_health = ap_health_data_read();
	if (p_health == NULL) {
		pr_err("%s : p_health is NULL\n", __func__);
		return;
	}

	pr_info("%s : done\n", __func__);
	rr_data = sec_debug_get_reset_reason();

	if (rr_data == USER_UPLOAD_CAUSE_SMPL) {
		p_health->daily_rr.sp++;
		p_health->rr.sp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_WTSR) {
		p_health->daily_rr.wp++;
		p_health->rr.wp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_WATCHDOG) {
		p_health->daily_rr.dp++;
		p_health->rr.dp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_PANIC) {
		p_health->daily_rr.kp++;
		p_health->rr.kp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_MANUAL_RESET) {
		p_health->daily_rr.mp++;
		p_health->rr.mp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_POWER_RESET) {
		p_health->daily_rr.pp++;
		p_health->rr.pp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_REBOOT) {
		p_health->daily_rr.rp++;
		p_health->rr.rp++;
	} else if (rr_data == USER_UPLOAD_CAUSE_THERMAL) {
		p_health->daily_rr.tp++;
		p_health->rr.tp++;
	} else {
		p_health->daily_rr.np++;
		p_health->rr.np++;
	}

	p_health->last_rst_reason = 0;
	ap_health_data_write(p_health);
}

static int set_reset_reason_proc_show(struct seq_file *m, void *v)
{
	uint32_t rr_data = sec_debug_get_reset_reason();
	static uint32_t rr_cnt_update = 1;

	seq_printf(m, "%sON\n", sec_debug_get_reset_reason_str(rr_data));
	if (rr_cnt_update) {
		reset_reason_update_and_clear();
		rr_cnt_update = 0;
	}

	return 0;
}

static int sec_reset_reason_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, set_reset_reason_proc_show, NULL);
}

static const struct file_operations sec_reset_reason_proc_fops = {
	.open = sec_reset_reason_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static phys_addr_t sec_debug_reset_ex_info_paddr;
static unsigned sec_debug_reset_ex_info_size;
rst_exinfo_t *sec_debug_reset_ex_info;
ex_info_fault_t ex_info_fault[NR_CPUS];

void sec_debug_store_bug_string(const char *fmt, ...)
{
	va_list args;
	_kern_ex_info_t *p_ex_info;

	if (sec_debug_reset_ex_info) {
		p_ex_info = &sec_debug_reset_ex_info->kern_ex_info.info;
		va_start(args, fmt);
		vsnprintf(p_ex_info->bug_buf,
			sizeof(p_ex_info->bug_buf), fmt, args);
		va_end(args);
	}
}
EXPORT_SYMBOL(sec_debug_store_bug_string);

void sec_debug_store_additional_dbg(enum extra_info_dbg_type type, unsigned int value, const char *fmt, ...)
{
	va_list args;
	_kern_ex_info_t *p_ex_info;

	if (sec_debug_reset_ex_info) {
		p_ex_info = &sec_debug_reset_ex_info->kern_ex_info.info;
		switch (type) {
			case DBG_0_GLAD_ERR:
				va_start(args, fmt);
				vsnprintf(p_ex_info->dbg0,
						sizeof(p_ex_info->dbg0), fmt, args);
				va_end(args);
				break;
			case DBG_1_UFS_ERR:
				va_start(args, fmt);
				vsnprintf(p_ex_info->ufs_err,
						sizeof(p_ex_info->ufs_err), fmt, args);
				va_end(args);
				break;
			case DBG_2_RESERVED ... DBG_5_RESERVED:
				break;
			default:
				break;
		}
	}
}
EXPORT_SYMBOL(sec_debug_store_additional_dbg);

static void sec_debug_init_panic_extra_info(void)
{
	_kern_ex_info_t *p_ex_info;

	if (sec_debug_reset_ex_info) {
		p_ex_info = &sec_debug_reset_ex_info->kern_ex_info.info;
		memset((void *)&sec_debug_reset_ex_info->kern_ex_info, 0,
			sizeof(sec_debug_reset_ex_info->kern_ex_info));
		p_ex_info->cpu = -1;
		pr_info("%s: ex_info memory initialized size[%ld]\n",
			__func__, sizeof(kern_exinfo_t));
	}
}

static int __init sec_debug_ex_info_setup(char *str)
{
	unsigned size = memparse(str, &str);
	int ret;

	if (size && (*str == '@')) {
		unsigned long long base = 0;

		ret = kstrtoull(++str, 0, &base);

		sec_debug_reset_ex_info_paddr = base;
		sec_debug_reset_ex_info_size = (size + 0x1000 - 1) & ~(0x1000 -1);

		pr_err("%s: ex info phy=0x%llx, size=0x%x\n",
			__func__, sec_debug_reset_ex_info_paddr, sec_debug_reset_ex_info_size);
	}
	return 1;
}
__setup("sec_dbg_ex_info=", sec_debug_ex_info_setup);

static int __init sec_debug_get_extra_info_region(void)
{
	if (!sec_debug_reset_ex_info_paddr || !sec_debug_reset_ex_info_size)
		return -1;

	sec_debug_reset_ex_info = ioremap_cache(sec_debug_reset_ex_info_paddr, sec_debug_reset_ex_info_size);
		
	if (!sec_debug_reset_ex_info) {
		pr_err("%s: Failed to remap nocache ex info region\n", __func__);
		return -1;
	}

	sec_debug_init_panic_extra_info();
	return 0;
}

struct debug_reset_header *get_debug_reset_header(void)
{
	struct debug_reset_header *header = NULL;
	static int get_state = DRH_STATE_INIT;

	if (get_state == DRH_STATE_INVALID)
		return NULL;

	header = kmalloc(sizeof(struct debug_reset_header), GFP_KERNEL);
	if (!header) {
		pr_err("%s : fail - kmalloc for debug_reset_header\n", __func__);
		return NULL;
	}
	if (!read_debug_partition(debug_index_reset_summary_info, header)) {
		pr_err("%s : fail - get param!! debug_reset_header\n", __func__);
		kfree(header);
		header = NULL;
		return NULL;
	}

	if (get_state != DRH_STATE_VALID) {
		if (header->write_times == header->read_times) {
			pr_err("%s : untrustworthy debug_reset_header\n", __func__);
			get_state = DRH_STATE_INVALID;
			kfree(header);
			header = NULL;
			return NULL;
		} else {
			reset_write_cnt = header->write_times;
			get_state = DRH_STATE_VALID;
		}
	}

	return header;
}

static int set_debug_reset_header(struct debug_reset_header *header)
{
	int ret = 0;
	static int set_state = DRH_STATE_INIT;

	if (set_state == DRH_STATE_VALID) {
		pr_info("%s : debug_reset_header working well\n", __func__);
		return ret;
	}

	if ((header->write_times - 1) == header->read_times) {
		pr_info("%s : debug_reset_header working well\n", __func__);
		header->read_times++;
	} else {
		pr_info("%s : debug_reset_header read[%d] and write[%d] work sync error.\n",
				__func__, header->read_times, header->write_times);
		header->read_times = header->write_times;
	}

	if (!write_debug_partition(debug_index_reset_summary_info, header)) {
		pr_err("%s : fail - set param!! debug_reset_header\n", __func__);
		ret = -ENOENT;
	} else {
		set_state = DRH_STATE_VALID;
	}

	return ret;
}

static int sec_reset_summary_info_init(void)
{
	int ret = 0;

	if (summary_buf != NULL)
		return true;

	if (summary_info != NULL) {
		pr_err("%s : already memory alloc for summary_info\n", __func__);
		return -EINVAL;
	}

	summary_info = get_debug_reset_header();
	if (summary_info == NULL)
		return -EINVAL;

	if (summary_info->summary_size > SEC_DEBUG_RESET_SUMMARY_SIZE) {
		pr_err("%s : summary_size has problem.\n", __func__);
		ret = -EINVAL;
		goto error_summary_info;
	}

	summary_buf = vmalloc(SEC_DEBUG_RESET_SUMMARY_SIZE);
	if (!summary_buf) {
		pr_err("%s : fail - kmalloc for summary_buf\n", __func__);
		ret = -ENOMEM;
		goto error_summary_info;
	}
	if (!read_debug_partition(debug_index_reset_summary, summary_buf)) {
		pr_err("%s : fail - get param!! summary data\n", __func__);
		ret = -ENOENT;
		goto error_summary_buf;
	}

	pr_info("%s : w[%d] r[%d] idx[%d] size[%d]\n",
			__func__, summary_info->write_times, summary_info->read_times, summary_info-> ap_klog_idx, summary_info->summary_size);

	return ret;

error_summary_buf:
	vfree(summary_buf);
error_summary_info:
	kfree(summary_info);

	return ret;
}

static int sec_reset_summary_completed(void)
{
	int ret = 0;

	ret = set_debug_reset_header(summary_info);

	vfree(summary_buf);
	kfree(summary_info);

	summary_info = NULL;
	summary_buf = NULL;
	pr_info("%s finish\n", __func__);
	return ret;
}

static ssize_t sec_reset_summary_info_proc_read(struct file *file, char __user *buf,
		size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (sec_reset_summary_info_init() < 0)
		return -ENOENT;

	if ((pos >= summary_info->summary_size) || (pos >= SEC_DEBUG_RESET_SUMMARY_SIZE)) {
		pr_info("%s : pos %lld, size %d\n", __func__, pos, summary_info->summary_size);
		sec_reset_summary_completed();
		return 0;
	}

	count = min(len, (size_t)(summary_info->summary_size - pos));
	if (copy_to_user(buf, summary_buf + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations sec_reset_summary_info_proc_fops = {
	.owner = THIS_MODULE,
	.read = sec_reset_summary_info_proc_read,
};

static int sec_reset_klog_init(void)
{
	int ret = 0;

	if ((klog_read_buf != NULL) && (klog_buf != NULL))
		return true;

	if (klog_info != NULL) {
		pr_err("%s : already memory alloc for klog_info\n", __func__);
		return -EINVAL;
	}

	klog_info = get_debug_reset_header();
	if (klog_info == NULL)
		return -EINVAL;

	klog_read_buf = vmalloc(SEC_DEBUG_RESET_KLOG_SIZE);
	if (!klog_read_buf) {
		pr_err("%s : fail - vmalloc for klog_read_buf\n", __func__);
		ret = -ENOMEM;
		goto error_klog_info;
	}
	if (!read_debug_partition(debug_index_reset_klog, klog_read_buf)) {
		pr_err("%s : fail - get param!! summary data\n", __func__);
		ret = -ENOENT;
		goto error_klog_read_buf;
	}

	pr_info("%s : idx[%d]\n", __func__, klog_info->ap_klog_idx);

	klog_size = min((size_t)SEC_DEBUG_RESET_KLOG_SIZE, (size_t)klog_info->ap_klog_idx);
	    
	klog_buf = vmalloc(klog_size);
	if (!klog_buf) {
		pr_err("%s : fail - vmalloc for klog_buf\n", __func__);
		ret = -ENOMEM;
		goto error_klog_read_buf;
	}
	
	if (klog_size && klog_buf && klog_read_buf) {
		unsigned int i;
		for (i = 0; i < klog_size; i++)
			klog_buf[i] = klog_read_buf[(klog_info->ap_klog_idx - klog_size + i) % SEC_DEBUG_RESET_KLOG_SIZE];
	}

	return ret;

error_klog_read_buf:
	vfree(klog_read_buf);
error_klog_info:
	kfree(klog_info);

	return ret;
}

static void sec_reset_klog_completed(void)
{
	set_debug_reset_header(klog_info);

	vfree(klog_buf);
	vfree(klog_read_buf);
	kfree(klog_info);

	klog_info = NULL;
	klog_buf = NULL;
	klog_read_buf = NULL;
	klog_size = 0;

	pr_info("%s finish\n", __func__);
}

static ssize_t sec_reset_klog_proc_read(struct file *file, char __user *buf,
		size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (sec_reset_klog_init() < 0)
		return -ENOENT;

	if (pos >= klog_size) {
		pr_info("%s : pos %lld, size %d\n", __func__, pos, klog_size);
		sec_reset_klog_completed();
		return 0;
	}

	count = min(len, (size_t)(klog_size - pos));
	if (copy_to_user(buf, klog_buf + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations sec_reset_klog_proc_fops = {
	.owner = THIS_MODULE,
	.read = sec_reset_klog_proc_read,
};

static int sec_reset_tzlog_init(void)
{
	int ret = 0;

	if (tzlog_buf != NULL)
		return true;

	if (tzlog_info != NULL) {
		pr_err("%s : already memory alloc for tzlog_info\n", __func__);
		return -EINVAL;
	}

	tzlog_info = get_debug_reset_header();
	if (tzlog_info == NULL)
		return -EINVAL;

	if (tzlog_info->stored_tzlog == 0) {
		pr_err("%s : The target didn't run SDI operation\n", __func__);
		ret = -EINVAL;
		goto error_tzlog_info;
	}

	tzlog_buf = vmalloc(SEC_DEBUG_RESET_TZLOG_SIZE);
	if (!tzlog_buf) {
		pr_err("%s : fail - vmalloc for tzlog_read_buf\n", __func__);
		ret = -ENOMEM;
		goto error_tzlog_info;
	}
	if (!read_debug_partition(debug_index_reset_tzlog, tzlog_buf)) {
		pr_err("%s : fail - get param!! tzlog data\n", __func__);
		ret = -ENOENT;
		goto error_tzlog_buf;
	}

	return ret;

error_tzlog_buf:
	vfree(tzlog_buf);
error_tzlog_info:
	kfree(tzlog_info);

	return ret;
}

static void sec_reset_tzlog_completed(void)
{
	set_debug_reset_header(tzlog_info);

	vfree(tzlog_buf);
	kfree(tzlog_info);

	tzlog_info = NULL;
	tzlog_buf = NULL;

	pr_info("%s finish\n", __func__);
}

static ssize_t sec_reset_tzlog_proc_read(struct file *file, char __user *buf,
		size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (sec_reset_tzlog_init() < 0)
		return -ENOENT;

	if (pos >= SEC_DEBUG_RESET_TZLOG_SIZE) {
		pr_info("%s : pos %lld, size %d\n", __func__, pos, SEC_DEBUG_RESET_TZLOG_SIZE);
		sec_reset_tzlog_completed();
		return 0;
	}

	count = min(len, (size_t)(SEC_DEBUG_RESET_TZLOG_SIZE - pos));
	if (copy_to_user(buf, tzlog_buf + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations sec_reset_tzlog_proc_fops = {
	.owner = THIS_MODULE,
	.read = sec_reset_tzlog_proc_read,
};

static int set_store_lastkmsg_proc_show(struct seq_file *m, void *v)
{
	struct debug_reset_header *check_store = NULL;

	if (check_store != NULL) {
		pr_err("%s : already memory alloc for check_store\n", __func__);
		return -EINVAL;
	}

	check_store = get_debug_reset_header();
	if (check_store == NULL) {
		seq_printf(m, "0\n");
	} else {
		seq_printf(m, "1\n");
	}

	if (check_store != NULL) {
		kfree(check_store);
		check_store = NULL;
	}

	return 0;
}

static int sec_store_lastkmsg_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, set_store_lastkmsg_proc_show, NULL);
}

static const struct file_operations sec_store_lastkmsg_proc_fops = {
	.open = sec_store_lastkmsg_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int sec_reset_reason_dbg_part_notifier_callback(
		struct notifier_block *nfb, unsigned long action, void *data)
{
	ap_health_t *p_health;
	uint32_t rr_data;

	switch (action) {
		case DBG_PART_DRV_INIT_DONE:
			p_health = ap_health_data_read();
			if (!p_health)
				return NOTIFY_DONE;

			sec_debug_update_reset_reason(p_health->last_rst_reason);
			rr_data = sec_debug_get_reset_reason();

			break;
		default:
			return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}


static struct notifier_block sec_reset_reason_dbg_part_notifier = {
	.notifier_call = sec_reset_reason_dbg_part_notifier_callback,
};

static int __init sec_debug_reset_reason_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("reset_reason", S_IWUGO, NULL,
		&sec_reset_reason_proc_fops);

	if (!entry)
		return -ENOMEM;

	entry = proc_create("reset_summary", S_IWUGO, NULL,
			&sec_reset_summary_info_proc_fops);

	if (!entry)
		return -ENOMEM;	

	entry = proc_create("reset_klog", S_IWUGO, NULL,
			&sec_reset_klog_proc_fops);

	if (!entry)
		return -ENOMEM;	

	entry = proc_create("reset_tzlog", S_IWUGO, NULL,
			&sec_reset_tzlog_proc_fops);

	if (!entry)
		return -ENOMEM;	
	entry = proc_create("store_lastkmsg", S_IWUGO, NULL,
			&sec_store_lastkmsg_proc_fops);

	if (!entry)
		return -ENOMEM;	
	
	dbg_partition_notifier_register(&sec_reset_reason_dbg_part_notifier);

	return 0;
}

device_initcall(sec_debug_reset_reason_init);
#endif // CONFIG_USER_RESET_DEBUG

int __init sec_debug_init(void)
{
	int ret;
	uint32_t scm_id = 0; 
	struct scm_desc desc = {0}; 

#ifdef CONFIG_USER_RESET_DEBUG
	sec_debug_get_extra_info_region();
#endif
#ifdef CONFIG_PROC_AVC
	sec_avc_log_init();
#endif	
	
	ret=sec_dt_addr_init();
	
	if (ret<0)
		return ret;

	register_reboot_notifier(&nb_reboot_block);
	atomic_notifier_chain_register(&panic_notifier_list, &nb_panic_block);

	sec_debug_set_upload_magic(0x776655ee);
	sec_debug_set_upload_cause(UPLOAD_CAUSE_INIT);

#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
	__init_sec_debug_log();
#endif

	if (!enable) {
		sec_do_bypass_sdi_execution_in_low();
		return -EPERM;
	}

	scm_id = _TZ_DUMP_SECURITY_ALLOWS_MEM_DUMP_ID;
	desc.arginfo = _TZ_DUMP_SECURITY_ALLOWS_MEM_DUMP_ID_PARAM_ID;

	ret = scm_call2(scm_id, &desc);

	if (!ret) {
		pr_info("%s: syscall returns: %#llx, %#llx\n", __func__, desc.ret[0], desc.ret[1]);
		secure_dump = desc.ret[0];
	}
	else {
		pr_err("%s: syscall Error: 0x%x\n", __func__, ret); 
	}

	return 0;
}

int sec_debug_is_secure_dump(void)
{
	return secure_dump;
}

int sec_debug_is_rp_enabled(void)
{
	return rp_enabled;
}

int sec_debug_is_enabled(void)
{
	return enable;
}

#ifdef CONFIG_SEC_SSR_DEBUG_LEVEL_CHK
int sec_debug_is_enabled_for_ssr(void)
{
	return enable_cp_debug;
}
#endif

#ifdef CONFIG_SEC_FILE_LEAK_DEBUG
int sec_debug_print_file_list(void)
{
	int i=0;
	unsigned int nCnt=0;
	struct file *file=NULL;
	struct files_struct *files = current->files;
	const char *pRootName=NULL;
	const char *pFileName=NULL;
	int ret=0;

	nCnt=files->fdt->max_fds;

	printk(KERN_ERR " [Opened file list of process %s(PID:%d, TGID:%d) :: %d]\n",
		current->group_leader->comm, current->pid, current->tgid,nCnt);

	for (i=0; i<nCnt; i++) {

		rcu_read_lock();
		file = fcheck_files(files, i);

		pRootName=NULL;
		pFileName=NULL;

		if (file) {
			if (file->f_path.mnt
				&& file->f_path.mnt->mnt_root
				&& file->f_path.mnt->mnt_root->d_name.name)
				pRootName=file->f_path.mnt->mnt_root->d_name.name;

			if (file->f_path.dentry && file->f_path.dentry->d_name.name)
				pFileName=file->f_path.dentry->d_name.name;

			printk(KERN_ERR "[%04d]%s%s\n",i,pRootName==NULL?"null":pRootName,
							pFileName==NULL?"null":pFileName);
			ret++;
		}
		rcu_read_unlock();
	}
	if(ret == nCnt)
		return 1;
	else
		return 0;
}

void sec_debug_EMFILE_error_proc(void)
{
	printk(KERN_ERR "Too many open files(%d:%s)\n",
		current->tgid, current->group_leader->comm);

	if (!enable)
		return;

	/* We check EMFILE error in only "system_server","mediaserver" and "surfaceflinger" process.*/
	if (!strcmp(current->group_leader->comm, "system_server")
		||!strcmp(current->group_leader->comm, "mediaserver")
		||!strcmp(current->group_leader->comm, "surfaceflinger")){
		if (sec_debug_print_file_list() == 1) {
			panic("Too many open files");
		}
	}
}
#endif


/* klaatu - schedule log */
#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
void __sec_debug_task_sched_log(int cpu, struct task_struct *task,
						char *msg)
{
	unsigned i;

	if (!secdbg_log)
		return;

	if (!task && !msg)
		return;

	i = atomic_inc_return(&(secdbg_log->idx_sched[cpu]))
		& (SCHED_LOG_MAX - 1);
	secdbg_log->sched[cpu][i].time = cpu_clock(cpu);
	if (task) {
		strlcpy(secdbg_log->sched[cpu][i].comm, task->comm,
			sizeof(secdbg_log->sched[cpu][i].comm));
		secdbg_log->sched[cpu][i].pid = task->pid;
		secdbg_log->sched[cpu][i].pTask = task;
	} else {
		strlcpy(secdbg_log->sched[cpu][i].comm, msg,
			sizeof(secdbg_log->sched[cpu][i].comm));
		secdbg_log->sched[cpu][i].pid = -1;
		secdbg_log->sched[cpu][i].pTask = NULL;
	}
}

void sec_debug_task_sched_log_short_msg(char *msg)
{
	__sec_debug_task_sched_log(raw_smp_processor_id(), NULL, msg);
}

void sec_debug_task_sched_log(int cpu, struct task_struct *task)
{
	__sec_debug_task_sched_log(cpu, task, NULL);
}

void sec_debug_timer_log(unsigned int type, int int_lock, void *fn)
{
	int cpu = smp_processor_id();
	unsigned i;

	if (!secdbg_log)
		return;

	i = atomic_inc_return(&(secdbg_log->idx_timer[cpu]))
		& (SCHED_LOG_MAX - 1);
	secdbg_log->timer_log[cpu][i].time = cpu_clock(cpu);
	secdbg_log->timer_log[cpu][i].type = type;
	secdbg_log->timer_log[cpu][i].int_lock = int_lock;
	secdbg_log->timer_log[cpu][i].fn = (void *)fn;
}

void sec_debug_secure_log(u32 svc_id,u32 cmd_id)
{
	unsigned long flags;
	static DEFINE_SPINLOCK(secdbg_securelock);
	int cpu ;
	unsigned i;

	spin_lock_irqsave(&secdbg_securelock, flags);
	cpu	= smp_processor_id();
	if (!secdbg_log){
	   spin_unlock_irqrestore(&secdbg_securelock, flags);
	   return;
	}
	i = atomic_inc_return(&(secdbg_log->idx_secure[cpu]))
		& (TZ_LOG_MAX - 1);
	secdbg_log->secure[cpu][i].time = cpu_clock(cpu);
	secdbg_log->secure[cpu][i].svc_id = svc_id;
	secdbg_log->secure[cpu][i].cmd_id = cmd_id ;
	spin_unlock_irqrestore(&secdbg_securelock, flags);
}

void sec_debug_irq_sched_log(unsigned int irq, void *fn, int en)
{
	int cpu = smp_processor_id();
	unsigned i;

	if (!secdbg_log)
		return;

	i = atomic_inc_return(&(secdbg_log->idx_irq[cpu]))
		& (SCHED_LOG_MAX - 1);
	secdbg_log->irq[cpu][i].time = cpu_clock(cpu);
	secdbg_log->irq[cpu][i].irq = irq;
	secdbg_log->irq[cpu][i].fn = (void *)fn;
	secdbg_log->irq[cpu][i].en = en;
	secdbg_log->irq[cpu][i].preempt_count = preempt_count();
	secdbg_log->irq[cpu][i].context = &cpu;
}

void sec_debug_irq_enterexit_log(unsigned int irq,
					unsigned long long start_time)
{
	int cpu = smp_processor_id();
	unsigned i;

	if (!secdbg_log)
		return;

	i = atomic_inc_return(&(secdbg_log->idx_irq_exit[cpu]))
		& (SCHED_LOG_MAX - 1);
	secdbg_log->irq_exit[cpu][i].time = start_time;
	secdbg_log->irq_exit[cpu][i].end_time = cpu_clock(cpu);
	secdbg_log->irq_exit[cpu][i].irq = irq;
	secdbg_log->irq_exit[cpu][i].elapsed_time =
		secdbg_log->irq_exit[cpu][i].end_time - start_time;
}

#ifdef CONFIG_SEC_DEBUG_MSG_LOG
asmlinkage int sec_debug_msg_log(void *caller, const char *fmt, ...)
{
	int cpu = smp_processor_id();
	int r = 0;
	int i;
	va_list args;

	if (!secdbg_log)
		return 0;

	i = atomic_inc_return(&(secdbg_log->idx_secmsg[cpu]))
		& (MSG_LOG_MAX - 1);
	secdbg_log->secmsg[cpu][i].time = cpu_clock(cpu);
	va_start(args, fmt);
	r = vsnprintf(secdbg_log->secmsg[cpu][i].msg,
		sizeof(secdbg_log->secmsg[cpu][i].msg), fmt, args);
	va_end(args);

	secdbg_log->secmsg[cpu][i].caller0 = __builtin_return_address(0);
	secdbg_log->secmsg[cpu][i].caller1 = caller;
	secdbg_log->secmsg[cpu][i].task = current->comm;

	return r;
}

#endif

#ifdef CONFIG_SEC_DEBUG_AVC_LOG
asmlinkage int sec_debug_avc_log(const char *fmt, ...)
{
	int cpu = smp_processor_id();
	int r = 0;
	int i;
	va_list args;

	if (!secdbg_log)
		return 0;

	i = atomic_inc_return(&(secdbg_log->idx_secavc[cpu]))
		& (AVC_LOG_MAX - 1);
	va_start(args, fmt);
	r = vsnprintf(secdbg_log->secavc[cpu][i].msg,
		sizeof(secdbg_log->secavc[cpu][i].msg), fmt, args);
	va_end(args);

	return r;
}

#endif
#endif	/* CONFIG_SEC_DEBUG_SCHED_LOG */

static int __init sec_dbg_setup(char *str)
{
	unsigned size = memparse(str, &str);

	pr_emerg("%s: str=%s\n", __func__, str);

	if (size /*&& (size == roundup_pow_of_two(size))*/ && (*str == '@')) {
		secdbg_paddr = (uint64_t)memparse(++str, NULL);
		secdbg_size = size;
	}

	pr_emerg("%s: secdbg_paddr = 0x%llx\n", __func__, secdbg_paddr);
	pr_emerg("%s: secdbg_size = 0x%x\n", __func__, secdbg_size);

	return 1;
}

__setup("sec_dbg=", sec_dbg_setup);


static void sec_user_fault_dump(void)
{
	if (enable == 1 && enable_user == 1)
		panic("User Fault");
}

static long sec_user_fault_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *offs)
{
	char buf[100];

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (strncmp(buf, "dump_user_fault", 15) == 0)
		sec_user_fault_dump();

	return count;
}

static const struct file_operations sec_user_fault_proc_fops = {
	.write = sec_user_fault_write,
};

static int __init sec_debug_user_fault_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("user_fault", S_IWUSR|S_IWGRP, NULL,
			&sec_user_fault_proc_fops);
	if (!entry)
		return -ENOMEM;
	return 0;
}
device_initcall(sec_debug_user_fault_init);

#ifdef CONFIG_SEC_DEBUG_DCVS_LOG
void sec_debug_dcvs_log(int cpu_no, unsigned int prev_freq,
						unsigned int new_freq)
{
	unsigned int i;
	if (!secdbg_log)
		return;

	i = atomic_inc_return(&(secdbg_log->dcvs_log_idx[cpu_no]))
		& (DCVS_LOG_MAX - 1);
	secdbg_log->dcvs_log[cpu_no][i].cpu_no = cpu_no;
	secdbg_log->dcvs_log[cpu_no][i].prev_freq = prev_freq;
	secdbg_log->dcvs_log[cpu_no][i].new_freq = new_freq;
	secdbg_log->dcvs_log[cpu_no][i].time = cpu_clock(cpu_no);
}
#endif
#ifdef CONFIG_SEC_DEBUG_FUELGAUGE_LOG
void sec_debug_fuelgauge_log(unsigned int voltage, unsigned short soc,
				unsigned short charging_status)
{
	unsigned int i;
	int cpu = smp_processor_id();

	if (!secdbg_log)
		return;

	i = atomic_inc_return(&(secdbg_log->fg_log_idx))
		& (FG_LOG_MAX - 1);
	secdbg_log->fg_log[i].time = cpu_clock(cpu);
	secdbg_log->fg_log[i].voltage = voltage;
	secdbg_log->fg_log[i].soc = soc;
	secdbg_log->fg_log[i].charging_status = charging_status;
}
#endif
