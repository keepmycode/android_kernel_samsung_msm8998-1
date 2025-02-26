/*
 * include/linux/sec_param.h
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct debug_partition_data_s {
	struct work_struct debug_partition_work;
	struct completion work;
	void *value;
	unsigned int offset;
	unsigned int size;
	unsigned int direction;
	long int error;
};

enum debug_partition_index {
	debug_index_reset_summary_info = 0,
	debug_index_reset_klog_info,
	debug_index_reset_tzlog_info,
	debug_index_reset_ex_info,
	debug_index_reset_summary,
	debug_index_reset_klog,
	debug_index_reset_tzlog,
	debug_index_ap_health,
	debug_index_reset_extrc_info,
	debug_index_max,
};

bool check_magic_data(void);
bool init_debug_partition(void);
extern bool read_debug_partition(enum debug_partition_index index, void *value);
extern bool write_debug_partition(enum debug_partition_index index, void *value);


#define AP_HEALTH_MAGIC 0x48544C4145485041
#define AP_HEALTH_VER 1
#define MAX_PCIE_NUM 1

typedef struct {
	uint64_t magic;
	uint32_t size;
	uint16_t version;
	uint16_t need_write;
} ap_health_header_t;

typedef struct {
	uint32_t cpu_throttle_cnt[NR_CPUS];
	uint32_t cpu_hotplug_cnt[NR_CPUS];
	uint32_t ktm_reset_cnt;
	uint32_t gcc_t0_reset_cnt;
	uint32_t gcc_t1_reset_cnt;
} therm_health_t;

typedef struct {
	uint32_t gld_err_cnt;
	uint32_t obsrv_err_cnt;
} cache_health_t;

typedef struct {
	uint32_t phy_init_fail_cnt;
	uint32_t link_down_cnt;
	uint32_t link_up_fail_cnt;
	uint32_t link_up_fail_ltssm;
} pcie_health_t;

typedef struct {
	uint32_t np;
	uint32_t rp;
	uint32_t mp;
	uint32_t kp;
	uint32_t dp;
	uint32_t wp;
	uint32_t tp;
	uint32_t sp;
	uint32_t pp;
} reset_reason_t;

#define MAX_CLUSTER_NUM 2
#define CPU_NUM_PER_CLUSTER 4
#define MAX_VREG_CNT 3
#define MAX_BATT_DCVS 10

typedef struct {
	uint32_t cpu_KHz;
	uint16_t apc_mV;
	uint16_t l2_mV;
} apps_dcvs_t;

typedef struct {
	uint32_t ddr_KHz;
	uint16_t mV[MAX_VREG_CNT];
} rpm_dcvs_t;

typedef struct {
	uint64_t ktime;	
	int32_t cap;
	int32_t volt;
	int32_t temp;
	int32_t curr;
} batt_dcvs_t;

typedef struct {
	uint32_t tail;
	batt_dcvs_t batt[MAX_BATT_DCVS];
} battery_health_t;

typedef struct {
	apps_dcvs_t apps[MAX_CLUSTER_NUM];
	rpm_dcvs_t rpm;
	batt_dcvs_t batt;
} dcvs_info_t;

typedef struct {
	ap_health_header_t header;
	uint32_t last_rst_reason;
	dcvs_info_t last_dcvs;
	uint64_t spare_magic1;
	reset_reason_t rr;
	therm_health_t thermal;
	cache_health_t cache;
	pcie_health_t pcie[MAX_PCIE_NUM];
	battery_health_t battery;
	uint64_t spare_magic2;
	reset_reason_t daily_rr;
	therm_health_t daily_thermal;
	cache_health_t daily_cache;
	pcie_health_t daily_pcie[MAX_PCIE_NUM];
	uint64_t spare_magic3;
} ap_health_t;


#define DEBUG_PARTITION_NAME	"/dev/block/bootdevice/by-name/debug"	/* debug block */

#define DEBUG_PARTITION_MAGIC	0x41114729

#define PARTITION_RD	0
#define PARTITION_WR	1

#define SECTOR_UNIT_SIZE			(4096) /* UFS */

/* DEBUG Partition Data Table */
#define SEC_DEBUG_PARTITION_SIZE		(0xA00000)		/* 10MB */
#define SEC_DEBUG_RESET_HEADER_OFFSET		(0x0)
#define SEC_DEBUG_RESET_HEADER_SIZE		(SECTOR_UNIT_SIZE)
#define SEC_DEBUG_AP_HEALTH_OFFSET 		(8*1024)
#define SEC_DEBUG_AP_HEALTH_SIZE		(sizeof(ap_health_t))
#define SEC_DEBUG_EXTRA_INFO_OFFSET 		(SEC_DEBUG_RESET_HEADER_OFFSET + SEC_DEBUG_RESET_HEADER_SIZE)
#define SEC_DEBUG_EXTRA_INFO_SIZE		(ALIGN(SEC_DEBUG_EX_INFO_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_RESET_KLOG_OFFSET		(1*1024*1024)
#define SEC_DEBUG_RESET_KLOG_SIZE		(0x200000-0xC)		/* 2MB */
#define SEC_DEBUG_RESET_SUMMARY_OFFSET		(SEC_DEBUG_RESET_KLOG_OFFSET +\
							ALIGN(SEC_DEBUG_RESET_KLOG_SIZE, SECTOR_UNIT_SIZE)) /* 3*1024*1024*/
#define SEC_DEBUG_RESET_SUMMARY_SIZE		(0x200000)		/* 2MB */
#define SEC_DEBUG_RESET_TZLOG_OFFSET		(SEC_DEBUG_RESET_SUMMARY_OFFSET +\
							ALIGN(SEC_DEBUG_RESET_SUMMARY_SIZE, SECTOR_UNIT_SIZE)) /* 5*1024*1024*/
#define SEC_DEBUG_RESET_TZLOG_SIZE		(0x40000)		/* 256KB */
#define SEC_DEBUG_RESET_EXTRC_OFFSET		(SEC_DEBUG_RESET_TZLOG_OFFSET +\
							ALIGN(SEC_DEBUG_RESET_TZLOG_SIZE, SECTOR_UNIT_SIZE)) /* 5*1024*1024 + 256*1024*/
#define SEC_DEBUG_RESET_EXTRC_SIZE		(1*1024)		/* 1KB */

ap_health_t* ap_health_data_read(void);
int ap_health_data_write(ap_health_t *data);
int dbg_partition_notifier_register(struct notifier_block *nb);
extern unsigned int is_boot_recovery;

enum {
	DBG_PART_DRV_INIT_DONE,
	DBG_PART_DRV_INIT_EXIT,
};


