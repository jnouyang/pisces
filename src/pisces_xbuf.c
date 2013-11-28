#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/wait.h>

#include "enclave.h"
#include "util-hashtable.h"
#include "pisces_xbuf.h"
#include "ipi.h"



#define XBUF_READY     0x01
#define XBUF_PENDING   0x02
#define XBUF_STAGED    0x04
#define XBUF_ACTIVE    0x08
#define XBUF_COMPLETE  0x10

struct pisces_xbuf {
    union {
	u64 flags;
	struct {
	    u8 ready          : 1;   // Flag set by enclave OS, after channel is init'd
	    u8 pending        : 1;   // Set when a message is ready to be received
	    u8 staged         : 1;   // Used by the endpoints for staged data transfers
	    u8 active         : 1;   // Set when a message has been accepted by the receiver
	    u8 complete       : 1;   // Set by the receiver when message has been handled
	    u64 rsvd          : 59;
	} __attribute__((packed));
    } __attribute__((packed));
    
    u32 host_apic;
    u32 host_vector;
    u32 enclave_cpu;
    u32 enclave_vector;
    u32 total_size;

    u32 data_len;

    u8 data[0];
} __attribute__((packed));



static u32 init_stage_data(struct pisces_xbuf * xbuf, u8 * data, u32 data_len) {
    u32 xbuf_size = xbuf->total_size;
    u32 staged_len = (data_len > xbuf_size) ? xbuf_size : data_len;
	
    xbuf->data_len = data_len;

    memcpy(xbuf->data, data, staged_len);
    xbuf->staged = 1;
    mb();
	
    return staged_len;
}


static u32 send_data(struct pisces_xbuf * xbuf, u8 * data, u32 data_len) {
    u32 xbuf_size = xbuf->total_size;
    u32 bytes_sent = 0;
    u32 bytes_left = data_len;


    while (xbuf->staged == 1) {
	__asm__ __volatile__ ("":::"memory");
    }

    while (bytes_left > 0) {
	u32 staged_len = (bytes_left > xbuf_size) ? xbuf_size : bytes_left;
	
	memcpy(xbuf->data, data + bytes_sent, staged_len);

	xbuf->staged = 1;
	mb();
	
	while (xbuf->staged == 1) {
	    __asm__ __volatile__ ("":::"memory");
	}
	
	bytes_sent += staged_len;
	bytes_left -= staged_len;
    }


    return bytes_sent;
}


static u32 recv_data(struct pisces_xbuf * xbuf, u8 ** data, u32 * data_len) {
    u32 xbuf_size = xbuf->total_size;
    u32 bytes_read = 0;
    u32 bytes_left = xbuf->data_len;

    while (xbuf->staged == 0) {
	__asm__ __volatile__ ("":::"memory");
    }


    

    *data_len = xbuf->data_len;
    *data = kmalloc(xbuf->data_len, GFP_ATOMIC);



    while (bytes_left > 0) {
	u32 staged_len = (bytes_left > xbuf_size) ? xbuf_size : bytes_left;

	while (xbuf->staged == 0) {
	    __asm__ __volatile__ ("":::"memory");
	}
	
	printk("Copying %d bytes in recv_Data\n", staged_len);

	memcpy(*data + bytes_read, xbuf->data, staged_len);

	
	xbuf->staged = 0;

	
	bytes_read += staged_len;
	bytes_left -= staged_len;
    }

    return bytes_read;

}


int pisces_xbuf_sync_send(struct pisces_xbuf_desc * desc, u8 * data, u32 data_len,
			  u8 ** resp_data, u32 * resp_len) {
    struct pisces_xbuf * xbuf = desc->xbuf;
    unsigned long flags = 0;
    int acquired = 0;
    
    if (xbuf->ready == 0) {
        printk(KERN_ERR "Attempted longcall to unready host OS\n");
        return -1;
    }

    while (acquired == 0) {
	spin_lock_irqsave(&(desc->xbuf_lock), flags);
	
	if (xbuf->pending == 0) {
	    // clear all flags and signal that message is pending */
	    xbuf->flags = XBUF_READY | XBUF_PENDING;
	    mb();
	    acquired = 1;
	}


	spin_unlock_irqrestore(&(desc->xbuf_lock), flags);

	if (!acquired) {
            wait_event_interruptible(desc->xbuf_waitq, (xbuf->pending == 0));
        }
    }

    if ((data != NULL) && (data_len > 0)) {
	u32 bytes_staged = 0;

	bytes_staged = init_stage_data(xbuf, data, data_len);
	
	data_len -= bytes_staged;
	data += bytes_staged;
    }


    printk("Sending IPI %d to cpu %d\n", xbuf->enclave_vector, xbuf->enclave_cpu);
    pisces_send_ipi(desc->enclave, xbuf->enclave_cpu, xbuf->enclave_vector);
    printk("IPI completed\n");

    send_data(xbuf, data, data_len);

    /* Wait for complete flag to be 1 */
    while (xbuf->complete == 0) {
	schedule();
        __asm__ __volatile__ ("":::"memory");
    }


    if ((*resp_data) && (xbuf->staged == 1)) {
	// Response exists and we actually want to retrieve it
	recv_data(xbuf, resp_data, resp_len);
    }


    xbuf->flags = XBUF_READY;
    mb();
    
    wake_up_interruptible(&(desc->xbuf_waitq));

    return 0;
}


int pisces_xbuf_send(struct pisces_xbuf_desc * desc, u8 * data, u32 data_len) {
    return pisces_xbuf_sync_send(desc, data, data_len, NULL, NULL);
}




int pisces_xbuf_complete(struct pisces_xbuf_desc * desc, u8 * data, u32 data_len) {
    struct pisces_xbuf * xbuf = desc->xbuf;
	
    if (xbuf->active == 0) {
	printk(KERN_ERR "Error: Attempting to complete an inactive xbuf\n");
	return -1;
    }

    printk("Completing LCALL\n");

    if ((data_len > 0) && (data != NULL)) {
	u32 bytes_staged = 0;

	printk("Initing Staged data. Len=%d\n", data_len);

	bytes_staged = init_stage_data(xbuf, data, data_len);
	
	data_len -= bytes_staged;
	data += bytes_staged;
    }

    __asm__ __volatile__ ("":::"memory");

    xbuf->complete = 1;    

    __asm__ __volatile__ ("":::"memory");

    
    send_data(xbuf, data, data_len);

    return 0;
}







static void 
ipi_handler(void * private_data)
{	
    struct pisces_xbuf_desc * desc = private_data;
    struct pisces_xbuf * xbuf = desc->xbuf;
    u8 * data = NULL;
    u32 data_len = 0;

    recv_data(xbuf, &data, &data_len);
    
    xbuf->active = 1;

    if (desc->recv_handler) {
	desc->recv_handler(desc->enclave, data, data_len);
    } else {
	printk("IPI Arrived for XBUF without a handler\n");
	xbuf->complete = 1;
    }

    return;
}



struct pisces_xbuf_desc * pisces_xbuf_server_init(struct pisces_enclave * enclave, 
						  uintptr_t xbuf_va, u32 total_bytes, 
						  void (*recv_handler)(struct pisces_enclave *, u8 *, u32), 
						  u32 ipi_vector, u32 target_cpu) {
    struct pisces_xbuf * xbuf = (struct pisces_xbuf *)xbuf_va;
    struct pisces_xbuf_desc * desc = NULL;

    if (xbuf->ready == 1) {
	printk(KERN_ERR "XBUF has already been initialized\n");
	return NULL;
    }

    desc = kmalloc(sizeof(struct pisces_xbuf_desc), GFP_KERNEL);

    if (IS_ERR(desc)) {
	return NULL;
    }

    memset(desc, 0, sizeof(struct pisces_xbuf_desc));
    memset(xbuf, 0, sizeof(struct pisces_xbuf));

    xbuf->host_apic = target_cpu;
    xbuf->host_vector = ipi_vector;
    xbuf->total_size = total_bytes - sizeof(struct pisces_xbuf);
    
    desc->xbuf = xbuf;
    desc->recv_handler = recv_handler;
    desc->enclave = enclave;
    spin_lock_init(&(desc->xbuf_lock));
    init_waitqueue_head(&(desc->xbuf_waitq));

    printk("Registering Handler for Pisces Control IPIs\n");

    if (pisces_register_ipi_callback(ipi_handler, desc) != 0) {
	printk(KERN_ERR "Error registering lcall IPI callback for enclave %d\n", enclave->id);
	kfree(desc);
	return NULL;
    }
    
    xbuf->ready = 1;

    return desc;

}


struct pisces_xbuf_desc * pisces_xbuf_client_init(struct pisces_enclave * enclave, uintptr_t xbuf_va,
						  u32 ipi_vector, u32 target_cpu) {
    struct pisces_xbuf * xbuf = (struct pisces_xbuf *)xbuf_va;
    struct pisces_xbuf_desc * desc = kmalloc(sizeof(struct pisces_xbuf_desc), GFP_KERNEL);

    if (desc == NULL) {
	return NULL;
    }

    memset(desc, 0, sizeof(struct pisces_xbuf_desc));

    xbuf->enclave_cpu = target_cpu;
    xbuf->enclave_vector = ipi_vector;

    desc->xbuf = xbuf;
    desc->enclave = enclave;
    spin_lock_init(&(desc->xbuf_lock));
    init_waitqueue_head(&(desc->xbuf_waitq));

    
    return desc;
}













