/* Pisces Enclave 
 * (c) 2013, Jack Lange, (jacklange@cs.pitt.edu)
 * (c) 2013, Jiannan Ouyang, (ouyang@cs.pitt.edu)
 */


#ifndef __ENCLAVE_H__
#define __ENCLAVE_H__

#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/cdev.h>

#include "enclave_cons.h"
#include "pisces_ctrl.h"

#define ENCLAVE_LOADED      1
#define ENCLAVE_RUNNING     2

struct pisces_enclave {
    int state;

    // We'll just hold on to the image for now,
    // We need to get rid of this soon though
    struct pisces_image * tmp_image_ptr;

    char * kern_path;
    char * initrd_path;
    char * kern_cmdline;

    dev_t dev; 
    struct cdev cdev;

    /* APIC ID of boot cpu*/
    u32 boot_cpu;

    u32 num_cpus;
    cpumask_t assigned_cpus;

    struct pisces_cons cons;
    struct pisces_ctrl ctrl;

    uintptr_t bootmem_addr_pa;
    u64 bootmem_size;

    // This is what we will want eventually.....
    struct list_head memdesc_list;
    u32 memdesc_num;


};



int pisces_enclave_create(struct pisces_image * img);

int pisces_enclave_launch(struct pisces_enclave * enclave);


#endif
