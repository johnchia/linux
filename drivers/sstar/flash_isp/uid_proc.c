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
 * per-unit value these boards have: Read Unique ID (4Bh) returns 128
 * factory-programmed read-only bits, and the flash is soldered down. It is also
 * the only identity that survives a chip erase, being derived rather than
 * stored.
 *
 * ALL SIXTEEN BYTES ARE PUBLISHED, AND THE CALLER MUST FOLD ALL SIXTEEN
 *
 * Only part of the value identifies the unit. On two boards measured, bytes 0-7
 * were byte-identical -- ASCII "AP3P056" plus a revision byte, a product/lot
 * code -- and of the remainder only bytes 9 and 10 differed. Anything deriving
 * an address from a prefix of this would hand the whole fleet one MAC, which is
 * precisely the collision it was meant to cure. So the file carries the lot and
 * says nothing about which end matters.
 *
 * That also means the entropy here is thin: sixteen bits observed across two
 * units from what is very likely one production lot. It is enough to separate
 * the cameras on a small network and it is not a substitute for a real OUI.
 *
 * The vendor's own HAL_SERFLASH_ReadUID() cannot be used for this. It returns
 * eight bytes, and worse, it drives the ISP/RIU registers while the flash on
 * these boards is driven by the FSP engine, so it never read anything at all --
 * it is the only read in that driver with no CONFIG_RIUISP guard around it.
 * HAL_SERFLASH_ReadUIDBytes() is the FSP implementation.
 *
 * WHY IN THE KERNEL RATHER THAN /dev/mem
 *
 * The opcode is harmless to the chip -- no write enable, nothing latched -- but
 * it is not harmless to the *controller*, which the MTD driver owns. Issuing it
 * from userspace behind that driver's back can land in the middle of a jffs2
 * commit, and the failure is a corrupted write rather than an error. Here the
 * read takes the driver's own mutex, so it serialises against every other flash
 * operation the way any other flash access does.
 *
 * The read happens on open() rather than at init, so it costs nothing on a boot
 * that never looks -- which is every boot after the first, since the userspace
 * side only consults this when it has no address of its own.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "drvSERFLASH.h"

/*
 * Defined in $(CONFIG_SSTAR_CHIP_NAME)/halSERFLASH.c and declared in none of
 * the vendor headers. Declared here rather than added to seven per-SoC headers
 * of which exactly one is ever compiled.
 */
extern MS_BOOL HAL_SERFLASH_ReadUIDBytes(MS_U8 *pu8Data, MS_U32 u32Size);

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
#define FLASH_UID_SIZE 16

/*
 * All-zero and all-ones are what a part answers with when it implements no
 * unique number, or when the bus reads back idle. Both are identical on every
 * unit, so a caller deriving an identity from one would hand the whole fleet
 * the same address. The file is left empty in that case: no identity is a
 * state userspace can test for, a wrong one is not.
 */
static int sstar_flash_uid_show(struct seq_file *m, void *v)
{
	MS_U8 au8Uid[FLASH_UID_SIZE];
	int i, zero = 1, ones = 1;

	memset(au8Uid, 0, sizeof(au8Uid));

	if (!HAL_SERFLASH_ReadUIDBytes(au8Uid, FLASH_UID_SIZE))
		return 0;

	for (i = 0; i < FLASH_UID_SIZE; i++) {
		if (au8Uid[i] != 0x00)
			zero = 0;
		if (au8Uid[i] != 0xFF)
			ones = 0;
	}

	if (zero || ones)
		return 0;

	for (i = 0; i < FLASH_UID_SIZE; i++)
		seq_printf(m, "%02x", au8Uid[i]);

	seq_putc(m, '\n');

	return 0;
}

static int sstar_flash_uid_open(struct inode *inode, struct file *file)
{
	return single_open(file, sstar_flash_uid_show, NULL);
}

static const struct file_operations sstar_flash_uid_fops = {
	.owner   = THIS_MODULE,
	.open    = sstar_flash_uid_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/*
 * late_initcall so the flash driver is up. Creating the entry cannot fail in a
 * way worth handling -- without it userspace finds no file and keeps whatever
 * address it had, which is the same outcome as a part with no unique ID.
 */
static int __init sstar_flash_uid_init(void)
{
	proc_create(FLASH_UID_NAME, 0444, NULL, &sstar_flash_uid_fops);

	return 0;
}

late_initcall(sstar_flash_uid_init);
