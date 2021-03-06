/* 
 * XPMEM communication channel
 *
 * All we're really doing is forwarding commands between the Linux XPMEM
 * domain and the enclave domain
 *
 * (c) Brian Kocoloski, 2013 (briankoco@cs.pitt.edu)
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/poll.h>

#include "pisces_boot_params.h"
#include "pisces_lcall.h"
#include "pisces_xbuf.h"
#include "pisces_irq.h"
#include "enclave.h"

#include "pisces_xpmem.h"

/* Longcall structure */
struct pisces_xpmem_cmd_lcall {
    struct pisces_lcall lcall;
    struct xpmem_cmd_ex xpmem_cmd;
} __attribute__((packed));


/* Incoming XPMEM command from enclave - copy out and deliver to partition */
int
pisces_xpmem_cmd_lcall(struct pisces_enclave   * enclave,
                       struct pisces_xbuf_desc * xbuf_desc,
		       struct pisces_lcall     * lcall)
{
    struct pisces_xpmem           * xpmem       = &(enclave->xpmem);
    struct pisces_xpmem_cmd_lcall * xpmem_lcall = (struct pisces_xpmem_cmd_lcall *)lcall;
    struct pisces_lcall_resp        lcall_resp;
    struct xpmem_cmd_ex             cmd;

    lcall_resp.status   = 0;
    lcall_resp.data_len = 0;

    xpmem = xpmem_get_link_data(xpmem->link);
    if (xpmem == NULL) {
	printk(KERN_ERR "Piscex XPMEM: cannot handle enclave XPMEM request\n");
	lcall_resp.status = -1;
	goto out;
    }

    /* Copy command */
    memcpy(&cmd, &(xpmem_lcall->xpmem_cmd), sizeof(struct xpmem_cmd_ex));

out:
    pisces_xbuf_complete(xbuf_desc, 
			 (u8 *)&lcall_resp,
			 sizeof(struct pisces_lcall_resp));

    /* Deliver command to XPMEM partition if we received everything correctly */
    if (lcall_resp.status == 0) {
	xpmem_cmd_deliver(xpmem->link, &cmd);
	xpmem_put_link_data(xpmem->link);
    }

    return 0;

}

/* Use the XPMEM xbuf to send commands */
static int
xpmem_cmd_fn(struct xpmem_cmd_ex * cmd,
             void                * priv_data)
{
    struct pisces_xpmem * xpmem = (struct pisces_xpmem *)priv_data;

    /* Perform xbuf send */
    return pisces_xbuf_send(
	 xpmem->xbuf_desc, 
	 (u8 *)cmd, 
	 sizeof(struct xpmem_cmd_ex)
    );
}

static int
xpmem_segid_fn(xpmem_segid_t segid,
               xpmem_sigid_t sigid,
	       xpmem_domid_t domid,
	       void        * priv_data)
{
    struct pisces_xpmem   * xpmem   = (struct pisces_xpmem *)priv_data;
    struct pisces_enclave * enclave = xpmem->enclave;
    struct xpmem_signal   * sig     = (struct xpmem_signal *)&sigid;

    pisces_send_enclave_ipi(enclave, sig->vector);

    return 0;
}

static void
xpmem_kill_fn(void * priv_data)
{
    struct pisces_xpmem * xpmem = (struct pisces_xpmem *)priv_data;

    pisces_xbuf_client_deinit(xpmem->xbuf_desc);
}

/* Kernel initialization */
int 
pisces_xpmem_init(struct pisces_enclave * enclave)
{
    struct pisces_xpmem       * xpmem       = &(enclave->xpmem);
    struct pisces_boot_params * boot_params = NULL;

    memset(xpmem, 0, sizeof(struct pisces_xpmem));

    xpmem->enclave = enclave;

    boot_params = __va(enclave->bootmem_addr_pa);

    /* Initialize xbuf channel */
    xpmem->xbuf_desc = pisces_xbuf_client_init(enclave, (uintptr_t)__va(boot_params->xpmem_buf_addr), 0, 0);

    if (!xpmem->xbuf_desc) {
        return -1;
    }

    /* Add connection link for enclave */
    xpmem->link = xpmem_add_connection(
	    (void *)xpmem,
	    xpmem_cmd_fn,
	    xpmem_segid_fn,
	    xpmem_kill_fn);

    if (xpmem->link <= 0) {
	printk(KERN_ERR "Pisces XPMEM: cannot create XPMEM connection\n");
	return -1;
    }

    return 0;
}

/* Kernel deinitialization */
int
pisces_xpmem_deinit(struct pisces_enclave * enclave)
{
    struct pisces_xpmem * xpmem = &(enclave->xpmem);

    xpmem_remove_connection(xpmem->link);

    return 0;
}
