/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2012 Xilinx. Inc
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@xilinx.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/config-file.h"
#include "sysemu/qtest.h"
#include "hw/arm/xlnx-zynqmp.h"

#include <libfdt.h>
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#ifndef ARM_GENERIC_FDT_DEBUG
#define ARM_GENERIC_FDT_DEBUG 3
#endif
#define DB_PRINT(lvl, ...) do { \
    if (ARM_GENERIC_FDT_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_RAW(lvl, ...) do { \
    if (ARM_GENERIC_FDT_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0);

#define GENERAL_MACHINE_NAME "arm-generic-fdt2"

typedef struct {
    ram_addr_t ram_kernel_base;
    ram_addr_t ram_kernel_size;
} memory_info;

static void init_memory(void *fdt, ram_addr_t ram_size)
{
    FDTMachineInfo *fdti;
    char node_path[DT_PATH_LENGTH];
    int mem_offset = 0;

    /* Find a memory node or add new one if needed */
    while (qemu_devtree_get_node_by_name(fdt, node_path, "memory")) {
        qemu_fdt_add_subnode(fdt, "/memory@0");
        qemu_fdt_setprop_cells(fdt, "/memory@0", "reg", 0, ram_size);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    /* Let's find out how much memory we have already created, then
     * based on what the user ser with '-m' let's add more if needed.
     */
    uint64_t reg_value, mem_created = 0;
    int mem_container;
    char mem_node_path[DT_PATH_LENGTH];

    do {
        mem_offset =
            fdt_node_offset_by_compatible(fdt, mem_offset,
                                          "qemu:memory-region");

        /* Check if we found anything and that it is top level memory */
        if (mem_offset > 0 &&
                fdt_node_depth(fdt, mem_offset) == 1) {
            fdt_get_path(fdt, mem_offset, mem_node_path,
                         DT_PATH_LENGTH);

            mem_container = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                  "container",
                                                  0, 0, NULL);

            /* We only want RAM, so we filter to make sure the container of
             * what we are looking at is the same as the main memory@0 node
             * we just found above.
             */
            if (mem_container != qemu_fdt_get_phandle(fdt, node_path)) {
                continue;
            }

            DB_PRINT(0, "Found top level memory region %s\n",
                     mem_node_path);

            reg_value = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                              "reg", 0, 0, NULL);
            reg_value = reg_value << 32;
            reg_value += qemu_fdt_getprop_cell(fdt, mem_node_path,
                                               "reg", 1, 0, NULL);

            DB_PRINT(1, "    Address: 0x%" PRIx64 " ", reg_value);

            reg_value += qemu_fdt_getprop_cell(fdt, mem_node_path,
                                               "reg", 2, 0, NULL);

            DB_PRINT_RAW(1, "Size: 0x%" PRIx64 "\n", reg_value);

            /* Find the largest address (start address + size) */
            if (mem_created < reg_value) {
                mem_created = reg_value;
            }
        }
    } while (mem_offset > 0);

    if (mem_created < ram_size)
    {
    	error_report("Error: Not enough memory was specified in the device-tree");
    }
    /* The device tree generated more or equal amount of memory then
     * the user specified. Set that internally in QEMU.
     */
    DB_PRINT(0, "No extra memory is required\n");
    qemu_opt_set_number(qemu_find_opts_singleton("memory"), "size",
                        mem_created, &error_fatal);

    fdt_init_destroy_fdti(fdti);
}

static void arm_generic_fdt_init(MachineState *machine)
{
    void *fdt = NULL;
    int fdt_size;
    const char *hw_dtb_arg;

    hw_dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "hw-dtb");

    /* If the user provided a -hw-dtb, use it as the hw description.  */
    if (hw_dtb_arg) {
        fdt = load_device_tree(hw_dtb_arg, &fdt_size);
        if (!fdt) {
            error_report("Error: Unable to load Device Tree %s", hw_dtb_arg);
            exit(1);
        }
    } else {
    	error_report("Error: Unable to load Device Tree, the option hw-dtb must be specified");
    }

    init_memory(fdt, machine->ram_size);
}

static void arm_generic_fdt_machine_init(MachineClass *mc)
{
    mc->desc = "ARM device tree driven machine model";
    mc->init = arm_generic_fdt_init;
    mc->ignore_memory_transaction_failures = true;
    /* 4 A53s and 2 R5s */
    mc->max_cpus = 6;
    mc->default_cpus = 6;
}

DEFINE_MACHINE(GENERAL_MACHINE_NAME, arm_generic_fdt_machine_init)
