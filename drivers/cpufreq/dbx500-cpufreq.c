/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License Terms: GNU General Public License v2
 * Author: Sundar Iyer
 * Author: Martin Persson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */

#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/id.h>
#include <linux/mfd/db8500-liveopp.h>

static __iomem void *prcmu_base;

#define PRCM_ACK_MB1 0xE04
#define PRCM_ACK_MB1_CURRENT_ARM_OPP	(PRCM_ACK_MB1 + 0x0)

unsigned int last_idx;

static struct cpufreq_frequency_table db8500_freq_table[] = {
	[0] = {
		.index = 0,
		/* These frequency numbers have no effect on real clock */
		.frequency = 200000,
	},
	[1] = {
		.index = 1,
		.frequency = 400000,
	},
	[2] = {
		.index = 2,
		.frequency = 600000,
	},
	[3] = {
		.index = 3,
		.frequency = 800000,
	},
	[4] = {
		.index = 4,
		.frequency = 1000000,
	},
	[5] = {
		/* Used for MAX_OPP, if available */
		.index = 5,
		.frequency = CPUFREQ_TABLE_END,
	},
	[6] = {
		.index = 6,
		.frequency = CPUFREQ_TABLE_END,
	},
};

static struct cpufreq_frequency_table db5500_freq_table[] = {
	[0] = {
		.index = 0,
		.frequency = 200000,
	},
	[1] = {
		.index = 1,
		.frequency = 396500,
	},
	[2] = {
		.index = 2,
		.frequency = 793000,
	},
	[3] = {
		.index = 3,
		.frequency = CPUFREQ_TABLE_END,
	},
};

static struct cpufreq_frequency_table *freq_table;
static int freq_table_len;

static enum arm_opp db8500_idx2opp[] = {
	ARM_EXTCLK,
	ARM_50_OPP,
	ARM_50_OPP,
	ARM_100_OPP,
	ARM_100_OPP,
	ARM_MAX_OPP,
};

static enum arm_opp db5500_idx2opp[] = {
	ARM_EXTCLK,
	ARM_50_OPP,
	ARM_100_OPP,
};

static enum arm_opp *idx2opp;

static struct freq_attr *dbx500_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static int dbx500_cpufreq_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int dbx500_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int idx;

	prcmu_base = __io_address(U8500_PRCMU_BASE);

	/* scale the target frequency to one of the extremes supported */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;
	if (target_freq > policy->cpuinfo.max_freq)
		target_freq = policy->cpuinfo.max_freq;

	/* Lookup the next frequency */
	if (cpufreq_frequency_table_target
	    (policy, freq_table, target_freq, relation, &idx)) {
		return -EINVAL;
	}

	freqs.old = policy->cur;
	freqs.new = freq_table[idx].frequency;

	if (freqs.old == freqs.new)
		return 0;

	/* pre-change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	BUG_ON(idx >= freq_table_len);

	/* request the PRCM unit for opp change */
	if (freq_table[idx].frequency!=1200000){
		if (last_idx==1){
			db8500_prcmu_set_arm_opp(ARM_100_OPP);
			prcmu_abb_write(AB8500_REGU_CTRL2, 0x0B, &varm_raw_rec, 1);
			prcmu_abb_write(AB8500_REGU_CTRL2, AB8500_VBB_REG, &vbb_raw_rec, 1);
			db8500_prcmu_write(PRCMU_ARMPLL_REG,PLLARM_FREQ100OPP);}
	if (prcmu_set_arm_opp(idx2opp[idx])) {
		pr_err("ux500-cpufreq:  Failed to set OPP level\n");
		return -EINVAL;
	}
	if (freq_table[idx].frequency == 1000000) {
		db8500_prcmu_set_arm_opp(ARM_MAX_OPP);}
	last_idx=0;
	} else {
	last_idx=1;
	/* OPP */
	db8500_prcmu_set_arm_opp(ARM_MAX_OPP);
	/* Varm */
	prcmu_abb_write(AB8500_REGU_CTRL2, 0x0B, &varm_raw, 1);
	/* VBBp/VBBn */
	prcmu_abb_write(AB8500_REGU_CTRL2, AB8500_VBB_REG, &vbb_raw, 1);
	/* Frequency */
	db8500_prcmu_write(PRCMU_ARMPLL_REG,PLLARM_MAXOPP);
	db8500_prcmu_write(PRCMU_ARMPLL_REG, 0x0004017D);
	writeb(ARM_MAX_OPP,(prcmu_base + PRCM_ACK_MB1_CURRENT_ARM_OPP));}

	/* post change notification */
	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned int dbx500_cpufreq_getspeed(unsigned int cpu)
{
	int i;
	enum arm_opp current_opp;

	current_opp = prcmu_get_arm_opp();

	/* request the prcm to get the current ARM opp */
	for (i = 0;  i < freq_table_len; i++) {
		if (current_opp == idx2opp[i])
			return freq_table[i].frequency;
	}

	pr_err("cpufreq: ERROR: unknown opp %d given from prcmufw!\n",
	       current_opp);
	BUG_ON(1);

	/*
	 * Better to return something that might be correct than
	 * errno or zero, since clk_get_rate() won't do well with an errno.
	 */
	return freq_table[0].frequency;
}

static void __init dbx500_cpufreq_init_maxopp_freq(void)
{
	struct prcmu_fw_version *fw_version = prcmu_get_fw_version();

	if ((fw_version == NULL) || !prcmu_has_arm_maxopp())
		return;

	switch (fw_version->project) {
	case PRCMU_FW_PROJECT_U8500:
	case PRCMU_FW_PROJECT_U9500:
	case PRCMU_FW_PROJECT_U8420:
		freq_table[5].frequency = 1200000;
		break;
	case PRCMU_FW_PROJECT_U8500_C2:
	case PRCMU_FW_PROJECT_U9500_C2:
	case PRCMU_FW_PROJECT_U8520:
		freq_table[3].frequency = 1150000;
		break;
	default:
		break;
	}
}

static bool initialized;

static void __init dbx500_cpufreq_early_init(void)
{
	if (cpu_is_u5500()) {
		freq_table = db5500_freq_table;
		idx2opp = db5500_idx2opp;
		freq_table_len = ARRAY_SIZE(db5500_freq_table);
	} else if (cpu_is_u8500()) {
		freq_table = db8500_freq_table;
		idx2opp = db8500_idx2opp;
		dbx500_cpufreq_init_maxopp_freq();
		freq_table_len = ARRAY_SIZE(db8500_freq_table);
		if (!prcmu_has_arm_maxopp())
			freq_table_len--;
	} else {
		ux500_unknown_soc();
	}
	initialized = true;
}

/*
 * This is called from localtimer initialization, via the clk_get_rate() for
 * the smp_twd clock.  This is way before cpufreq is initialized.
 */
unsigned long dbx500_cpufreq_getfreq(void)
{
	if (!initialized)
		dbx500_cpufreq_early_init();

	return dbx500_cpufreq_getspeed(0) * 1000;
}

static int __cpuinit dbx500_cpufreq_init(struct cpufreq_policy *policy)
{
	int res;
	int i;

	/* get policy fields based on the table */
	res = cpufreq_frequency_table_cpuinfo(policy, freq_table);
	if (!res)
		cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	else {
		pr_err("dbx500-cpufreq : Failed to read policy table\n");
		return res;
	}

	/*policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->cur = dbx500_cpufreq_getspeed(policy->cpu);*/
	policy->min = 200  * 1000;
	policy->max = 1000 * 1000;
	policy->cur = dbx500_cpufreq_getspeed(policy->cpu);

	for (i = 0; freq_table[i].frequency != policy->cur; i++);

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/*
	 * FIXME : Need to take time measurement across the target()
	 *	   function with no/some/all drivers in the notification
	 *	   list.
	 */
	/*policy->cpuinfo.transition_latency = 20 * 1000;*/ /* in ns */
	policy->cpuinfo.transition_latency = 30 * 1000; /* in ns */

	/* policy sharing between dual CPUs */
	cpumask_copy(policy->cpus, &cpu_present_map);

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;

	return 0;
}

static struct cpufreq_driver dbx500_cpufreq_driver = {
	.flags  = CPUFREQ_STICKY,
	.verify = dbx500_cpufreq_verify_speed,
	.target = dbx500_cpufreq_target,
	.get    = dbx500_cpufreq_getspeed,
	.init   = dbx500_cpufreq_init,
	.name   = "DBX500",
	.attr   = dbx500_cpufreq_attr,
};

static int __init dbx500_cpufreq_register(void)
{
	int i;

	if (!initialized)
		dbx500_cpufreq_early_init();

	pr_info("dbx500-cpufreq : Available frequencies:\n");

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
		pr_info("  %d Mhz\n", freq_table[i].frequency / 1000);

	return cpufreq_register_driver(&dbx500_cpufreq_driver);
}
device_initcall(dbx500_cpufreq_register);
