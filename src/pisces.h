#ifndef _PISCES_H_
#define _PISCES_H_

/*
 * Name of the device file
 */
#define DEVICE_NAME "pisces"
#define PISCES_PROC_DIR "pisces"

#define MAX_ENCLAVES 128

/* Pisces global cmds */
#define PISCES_ADD_MEM              1000
#define PISCES_LOAD_IMAGE           1001

/* Pisces enclave cmds */
#define PISCES_ENCLAVE_LAUNCH       2000
#define PISCES_ENCLAVE_CONS_CONNECT     2004
#define PISCES_ENCLAVE_CTRL_CONNECT     2005



struct memory_range {
    unsigned long long base_addr;
    unsigned long long pages;
} __attribute__((packed));


struct pisces_image {
    char kern_path[1024];
    char initrd_path[1024];
    char cmd_line[1024];

    unsigned long long memsize_in_bytes;
    unsigned long long num_cpus;
} __attribute__((packed));


int device_init(void);
void device_exit(void);
#endif
