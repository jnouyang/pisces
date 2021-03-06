#ifndef __PISCES_CMDS_H__
#define __PISCES_CMDS_H__

#ifndef __KERNEL__
#include "../linux_usr/pisces_types.h"
#endif



/* 
 * Generic Pisces Control Channel structures 
 */
struct pisces_cmd {
    u64 cmd;
    u32 data_len;
    u8  data[0];
} __attribute__((packed));


struct pisces_resp {
    u64 status;
    u32 data_len;
    u8  data[0];
} __attribute__((packed));



/* 
 * Linux -> Enclave Command Structures
 * 
 * For the most part these are identical to the user-space ioctl numbers 
 */

#define PISCES_CMD_ADD_CPU            100
#define PISCES_CMD_ADD_MEM            101

#define PISCES_CMD_REMOVE_CPU         110
#define PISCES_CMD_REMOVE_MEM         111


#define PISCES_CMD_CREATE_VM          120
#define PISCES_CMD_FREE_VM            121
#define PISCES_CMD_LAUNCH_VM          122
#define PISCES_CMD_STOP_VM            123
#define PISCES_CMD_PAUSE_VM           124
#define PISCES_CMD_CONTINUE_VM        125
#define PISCES_CMD_SIMULATE_VM        126

#define PISCES_CMD_VM_MOVE_CORE       140
#define PISCES_CMD_VM_DBG             141


#define PISCES_CMD_VM_CONS_CONNECT    150
#define PISCES_CMD_VM_CONS_DISCONNECT 151
#define PISCES_CMD_VM_CONS_KEYCODE    152  /* Not accessible via an IOCTL */

#define PISCES_CMD_ADD_V3_PCI         180
#define PISCES_CMD_ADD_V3_SATA        181

#define PISCES_CMD_FREE_V3_PCI        190

#define PISCES_CMD_LAUNCH_JOB         200
#define PISCES_CMD_LOAD_FILE          201
#define PISCES_CMD_STORE_FILE         202


#define PISCES_CMD_XPMEM_CMD_EX       300


#define PISCES_CMD_SHUTDOWN           900

struct memory_range {
    u64 base_addr;
    u64 pages;
} __attribute__((packed));

struct vm_path {
    char file_name[256];
    char vm_name[128];
} __attribute__((packed));


struct pisces_pci_spec {
    char  name[128];
    u32 bus;
    u32 dev;
    u32 func;
} __attribute__((packed));


struct pisces_sata_dev {
    char  name[128];
    u32 bus;
    u32 dev;
    u32 func;
    u32 port;
} __attribute__((packed));

struct pisces_dbg_spec {
    u32 vm_id;
    u32 core;
    u32 cmd;
} __attribute__((packed));



struct pisces_job_spec {
    char name[64];
    char exe_path[256];
    char argv[256];
    char envp[256];

    union {
	u64 flags;
	struct {
	    u64   use_large_pages : 1;
	    u64   use_smartmap    : 1;
	    u64   rsvd            : 62;
	} __attribute__((packed));
    } __attribute__((packed));

    u8   num_ranks;
    u64  cpu_mask;
    u64  heap_size;
    u64  stack_size;
} __attribute__((packed));


struct pisces_file_pair {
    char lnx_file[128];
    char lwk_file[128];
} __attribute__((packed));



/* Kernel Space command Structures */
#ifdef __KERNEL__

struct cmd_cpu_add {
    struct pisces_cmd hdr;

    u64 phys_cpu_id;
    u64 apic_id;
} __attribute__((packed));


struct cmd_mem_add {
    struct pisces_cmd hdr;

    u64 phys_addr;
    u64 size;
} __attribute__((packed));


struct cmd_create_vm {
    struct pisces_cmd hdr;

    struct vm_path path;
} __attribute__((packed));


struct cmd_vm_ctrl {
    struct pisces_cmd hdr;

    u32 vm_id;
} __attribute__((packed));



struct cmd_vm_cons_keycode {
    struct pisces_cmd     hdr;
    u32 vm_id;
    u8  scan_code;
} __attribute__((packed));


struct cmd_vm_debug {
    struct pisces_cmd      hdr;
    struct pisces_dbg_spec dbg_spec;
} __attribute__((packed));



struct cmd_add_pci_dev {
    struct pisces_cmd      hdr;
    struct pisces_pci_spec spec;
    u32 device_ipi_vector;
} __attribute__((packed));



struct cmd_free_pci_dev {
    struct pisces_cmd      hdr;
    struct pisces_pci_spec spec;
} __attribute__((packed));



struct cmd_launch_job {
    struct pisces_cmd      hdr;
    struct pisces_job_spec spec;
} __attribute__((packed));


struct cmd_load_file {
    struct pisces_cmd       hdr;
    struct pisces_file_pair file_pair;
} __attribute__((packed));

struct cmd_store_file {
    struct pisces_cmd       hdr;
    struct pisces_file_pair file_pair;
} __attribute__((packed));


#endif

/* ** */

#endif
