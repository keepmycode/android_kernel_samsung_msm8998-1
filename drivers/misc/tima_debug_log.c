#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/types.h>

#ifdef CONFIG_TIMA_RKP
#include <linux/rkp_entry.h>
#endif

#define DEBUG_LOG_START (0x9f900000)
#define	DEBUG_LOG_SIZE	(1<<20)
#define	DEBUG_LOG_MAGIC	(0xaabbccdd)
#define	DEBUG_LOG_ENTRY_SIZE	128

#ifdef CONFIG_TIMA_RKP
#define DEBUG_RKP_LOG_START	TIMA_DEBUG_LOG_START
#define SECURE_RKP_LOG_START	TIMA_SEC_LOG 
#endif
typedef struct debug_log_entry_s
{
    uint32_t	timestamp;          /* timestamp at which log entry was made*/
    uint32_t	logger_id;          /* id is 1 for tima, 2 for lkmauth app  */
#define	DEBUG_LOG_MSG_SIZE	(DEBUG_LOG_ENTRY_SIZE - sizeof(uint32_t) - sizeof(uint32_t))
    char	log_msg[DEBUG_LOG_MSG_SIZE];      /* buffer for the entry                 */
} __attribute__ ((packed)) debug_log_entry_t;

typedef struct debug_log_header_s
{
    uint32_t	magic;              /* magic number                         */
    uint32_t	log_start_addr;     /* address at which log starts          */
    uint32_t	log_write_addr;     /* address at which next entry is written*/
    uint32_t	num_log_entries;    /* number of log entries                */
    char	padding[DEBUG_LOG_ENTRY_SIZE - 4 * sizeof(uint32_t)];
} __attribute__ ((packed)) debug_log_header_t;

#define DRIVER_DESC   "A kernel module to read tima debug log"

unsigned long *tima_log_addr = 0;
unsigned long *tima_debug_log_addr = 0;
#ifdef CONFIG_TIMA_RKP
unsigned long *tima_debug_rkp_log_addr = 0;
unsigned long *tima_secure_rkp_log_addr = 0;
#endif

ssize_t	tima_read(struct file *filep, char __user *buf, size_t size, loff_t *offset)
{
	/* First check is to get rid of integer overflow exploits */
	if (size > DEBUG_LOG_SIZE || (*offset) + size > DEBUG_LOG_SIZE) {
		printk(KERN_ERR"Extra read\n");
		return -EINVAL;
	}
	if( !strcmp(filep->f_path.dentry->d_iname, "tima_debug_log")) {
		tima_log_addr = tima_debug_log_addr;
		memcpy_fromio(buf, (const char *)tima_log_addr + (*offset), size);
		*offset += size;
		return size;
	}
#ifdef CONFIG_TIMA_RKP
	else if( !strcmp(filep->f_path.dentry->d_iname, "tima_debug_rkp_log")) {
		if (*offset >= TIMA_DEBUG_LOG_SIZE) {
			return -EINVAL;
		} else if (*offset + size > TIMA_DEBUG_LOG_SIZE) {
			size = (TIMA_DEBUG_LOG_SIZE) - *offset;
		}
		tima_log_addr = tima_debug_rkp_log_addr;
		if (copy_to_user(buf, (const char *)tima_log_addr + (*offset), size)) {
			printk(KERN_ERR"Copy to user failed\n");
			return -1;
		} else {
			*offset += size;
			return size;
		}
	}
	else if( !strcmp(filep->f_path.dentry->d_iname, "tima_secure_rkp_log")) {
		if (*offset >= TIMA_SEC_LOG_SIZE) {
			return -EINVAL;
		} 
		tima_log_addr = tima_secure_rkp_log_addr;
		/* First check is to get rid of integer overflow exploits */
		if (size > TIMA_SEC_LOG_SIZE || (*offset) + size > TIMA_SEC_LOG_SIZE) {
			printk(KERN_ERR"Warning! Extra read. we change it as a proper size\n");
			size = TIMA_SEC_LOG_SIZE - (*offset);
			//return -EINVAL;
		}
		if (copy_to_user(buf, (const char *)tima_log_addr + (*offset), size)) {
			printk(KERN_ERR"Copy to user failed\n");
			return -1;
		} else {
			*offset += size;
			return size;
		}
	}
#endif
	else {
		printk(KERN_ERR"NO tima*log\n");
		return -1;
	}
}

static const struct file_operations tima_proc_fops = {
	.read		= tima_read,
};

/**
 *      tima_debug_log_read_init -  Initialization function for TIMA
 *
 *      It creates and initializes tima proc entry with initialized read handler
 */
static int __init tima_debug_log_read_init(void)
{
        if (proc_create("tima_debug_log", 0644,NULL, &tima_proc_fops) == NULL) {
		printk(KERN_ERR"tima_debug_log_read_init: Error creating proc entry\n");
		goto error_return;
	}
        printk(KERN_INFO"tima_debug_log_read_init: Registering /proc/tima_debug_log Interface \n");
#ifdef CONFIG_TIMA_RKP
	if (proc_create("tima_debug_rkp_log", 0644,NULL, &tima_proc_fops) == NULL) {
		printk(KERN_ERR"tima_debug_rkp_log_read_init: Error creating proc entry\n");
		goto ioremap_failed;
	}
	if (proc_create("tima_secure_rkp_log", 0644,NULL, &tima_proc_fops) == NULL) {
		printk(KERN_ERR"tima_secure_rkp_log_read_init: Error creating proc entry\n");
		goto remove_debug_rkp_entry;
	}
#endif
	tima_debug_log_addr = (unsigned long *)ioremap(DEBUG_LOG_START, DEBUG_LOG_SIZE);
#ifdef CONFIG_TIMA_RKP
	tima_debug_rkp_log_addr  = (unsigned long *)phys_to_virt(DEBUG_RKP_LOG_START);
	tima_secure_rkp_log_addr = (unsigned long *)phys_to_virt(SECURE_RKP_LOG_START);
#endif
	if (tima_debug_log_addr == NULL) {
		printk(KERN_ERR"tima_debug_log_read_init: ioremap Failed\n");
		goto ioremap_failed;
	}
        return 0;

#ifdef CONFIG_TIMA_RKP
remove_debug_rkp_entry:
	remove_proc_entry("tima_debug_rkp_log", NULL);
#endif
ioremap_failed:
	remove_proc_entry("tima_debug_log", NULL);
error_return:
	return -1;
}


/**
 *      tima_debug_log_read_exit -  Cleanup Code for TIMA
 *
 *      It removes /proc/tima proc entry and does the required cleanup operations
 */
static void __exit tima_debug_log_read_exit(void)
{
        remove_proc_entry("tima_debug_log", NULL);
        printk(KERN_INFO"Deregistering /proc/tima_debug_log Interface\n");
#ifdef CONFIG_TIMA_RKP
	remove_proc_entry("tima_debug_rkp_log", NULL);
	remove_proc_entry("tima_secure_rkp_log", NULL);
#endif
	if(tima_debug_log_addr != NULL)
		iounmap(tima_debug_log_addr);
}


module_init(tima_debug_log_read_init);
module_exit(tima_debug_log_read_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
