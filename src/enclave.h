/* Pisces Enclave 
 * (c) 2013, Jack Lange, (jacklange@cs.pitt.edu)
 * (c) 2013, Jiannan Ouyang, (ouyang@cs.pitt.edu)
 */


#ifndef __ENCLAVE_H__
#define __ENCLAVE_H__

#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/cdev.h>
#include <linux/kref.h>

#include "enclave_cons.h"
#include "enclave_ctrl.h"
#include "pisces_xpmem.h"
#include "pisces_lcall.h"
#include "enclave_fs.h"
#include "enclave_pci.h"

#define ENCLAVE_LOADED      1
#define ENCLAVE_RUNNING     2
#define ENCLAVE_DEAD        3

struct enclave_mem_block {
    u64 base_addr;
    u32 pages;
    struct list_head node;
};


struct pisces_enclave {
    int state;
    int id; 
    
    struct file * kern_file;
    struct file * init_file;

    char * kern_cmdline;

    dev_t       dev; 
    struct cdev cdev;
    struct proc_dir_entry * proc_dir;

    u32       boot_cpu;
    u32       num_cpus;
    cpumask_t assigned_cpus;

    struct pisces_cons          cons;
    struct pisces_ctrl          ctrl;
#ifdef USING_XPMEM
    struct pisces_xpmem         xpmem;
#endif
    struct pisces_lcall_state   lcall_state;
    struct enclave_fs           fs_state;
    struct enclave_pci_state    pci_state;

    uintptr_t bootmem_addr_pa;
    u64       bootmem_size;

    struct kref  refcount;
    struct mutex op_lock;

    // This is what we will want eventually.....
    struct list_head memdesc_list;
    u32              memdesc_num;

};




int 
pisces_enclave_create(struct pisces_image * img);


int 
pisces_enclave_free(struct pisces_enclave * enclave);


void
enclave_get(struct pisces_enclave * enclave);

void 
enclave_put(struct pisces_enclave * enclave);

int 
pisces_enclave_add_mem(struct pisces_enclave * enclave, 
		       u64                     base_addr, 
		       u32                     pages);


int 
pisces_enclave_add_cpu(struct pisces_enclave * enclave, 
		       u32                     cpu_id);





#endif
