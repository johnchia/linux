/*
 * uid_proc.c - the NOR part's factory unique ID, for userspace
 *
 * Copyright (c) [2019~2020] SigmaStar Technology.
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 */

/*
 * WHY THIS EXISTS
 *
 * Every camera built from one image ships with the same fallback MAC address,
 * so two on a network are a collision. The SPI NOR part carries the one
 * per-unit value these boards have: Read Unique ID (4Bh) returns 64
 * factory-programmed read-only bits, and the flash is soldered down. It is also
 * the only identity that survives a chip erase, being derived rather than
 * stored -- and on this SoC it is the only one at all, since the die ID
 * registers read zero.
 *
 * This publishes it so that the userspace which owns network configuration can
 * derive an address from it.
 *
 * WHY IN THE KERNEL RATHER THAN /dev/mem
 *
 * The opcode is harmless to the chip -- no write enable, nothing latched -- but
 * it is not harmless to the *controller*, which this driver owns. Issuing it
 * from userspace behind the driver's back can land in the middle of a jffs2
 * commit, and the failure is a corrupted write rather than an error. Here it is
 * an ordinary call into the driver, serialised the way its other operations
 * are.
 *
 * The read happens on open() rather than at init, so it costs nothing on a boot
 * that never looks -- which is every boot after the first, since the userspace
 * side only consults this when it has no address of its own.
 *
 * The format is sixteen lowercase hex digits, most significant byte first: the
 * same string the SigmaStar U-Boot fork writes to its uidraw environment
 * variable from the same opcode on the same part, so a board can move from one
 * to the other without its derived address changing.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "drvSPINOR.h"

/*
 * Deliberately not vendor-prefixed. What reads this is the userspace that owns
 * network configuration, and it should not have to learn a new path when the
 * same board design turns up on another vendor's SoC -- the question it asks is
 * "does this machine have a flash unique ID", which is not a SigmaStar
 * question. The kernel symbols below keep their sstar_ prefix because those
 * share a namespace with the rest of the tree; a procfs name is an interface
 * and does not.
 */
#define FLASH_UID_NAME "flash_uid"

/*
 * A degenerate ID is already refused by the driver, which reports all-zero and
 * all-ones as a failure rather than a value -- they are what a part answers
 * with when it implements no unique number, and are identical on every unit.
 * So anything short of success leaves the file empty: no identity is a state
 * userspace can test for, a wrong one is not.
 */
static int sstar_flash_uid_show(struct seq_file *m, void *v)
{
	u8 au8_uid[SPI_NOR_RDUID_BYTE_CNT];
	u8 u8_i;

	memset(au8_uid, 0, sizeof(au8_uid));

	if (ERR_SPINOR_SUCCESS != mdrv_spinor_read_unique_id(au8_uid))
		return 0;

	for (u8_i = 0; SPI_NOR_RDUID_BYTE_CNT > u8_i; u8_i++)
		seq_printf(m, "%02x", au8_uid[u8_i]);

	seq_putc(m, '\n');

	return 0;
}

static int sstar_flash_uid_open(struct inode *inode, struct file *file)
{
	return single_open(file, sstar_flash_uid_show, NULL);
}

static const struct proc_ops sstar_flash_uid_proc_ops = {
	.proc_open    = sstar_flash_uid_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/*
 * late_initcall so the flash driver is up. Creating the entry cannot fail in a
 * way worth handling -- without it userspace finds no file and keeps whatever
 * address it had, which is the same outcome as a part with no unique ID.
 */
static int __init sstar_flash_uid_init(void)
{
	proc_create(FLASH_UID_NAME, 0444, NULL, &sstar_flash_uid_proc_ops);

	return 0;
}

late_initcall(sstar_flash_uid_init);
