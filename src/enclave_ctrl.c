#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/slab.h>


#include "pisces_ioctl.h"
#include "pisces_boot_params.h"
#include "enclave_ctrl.h"
#include "enclave.h"
#include "boot.h"
#include "ctrl_ioctl.h"
#include "pisces_xbuf.h"
#include "enclave_pci.h"

#include "pgtables.h"

#include "v3_console.h"


static ssize_t
ctrl_read(struct file * filp, 
	  char __user * buffer, 
	  size_t        length, 
	  loff_t      * offset ) 
{
    struct pisces_enclave * enclave = filp->private_data;
    struct pisces_ctrl    * ctrl    = &(enclave->ctrl);
    int ret = 0;

    mutex_lock(&(ctrl->lock));
    {

    // read potential resp data
    }
    mutex_unlock(&(ctrl->lock));

    return ret;
}

// Allow ctrl server to write in a raw command
static ssize_t 
ctrl_write(struct file       * filp, 
	   const char __user * buffer, 
	   size_t              length, 
	   loff_t            * offset) 
{
    //  struct pisces_enclave * enclave = filp->private_data;


    return 0;
}



static long long 
send_vm_cmd(struct pisces_xbuf_desc * xbuf_desc, 
	    u64                       cmd_id, 
	    u64                       vm_id) 
{
    struct cmd_vm_ctrl   cmd;
    struct pisces_resp * resp = NULL;
    long long status   = 0;
    u32       resp_len = 0;
    int       ret      = 0;

    memset(&cmd, 0, sizeof(struct cmd_vm_ctrl));

    cmd.hdr.cmd      = cmd_id;
    cmd.hdr.data_len = (sizeof(struct cmd_vm_ctrl) - sizeof(struct pisces_cmd));
    cmd.vm_id        = vm_id;

    printk("Sending VM CMD (%llu) to VM (%llu)\n", cmd_id, vm_id);

    ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_vm_ctrl),  (u8 **)&resp, &resp_len);

    if (ret != 0) {
	return -1;
    }

    status = resp->status;
    kfree(resp);

    return status;
}


int 
ctrl_add_mem(struct pisces_enclave * enclave, 
	     struct memory_range   * reg)
{
    struct pisces_ctrl      * ctrl      = &(enclave->ctrl);
    struct pisces_xbuf_desc * xbuf_desc = ctrl->xbuf_desc;
    struct pisces_resp      * resp      = NULL;

    struct cmd_mem_add  cmd;

    u32 resp_len = 0;
    int ret      = 0;

    memset(&cmd, 0, sizeof(struct cmd_mem_add));

    cmd.hdr.cmd      = PISCES_CMD_ADD_MEM;
    cmd.hdr.data_len = ( sizeof(struct cmd_mem_add) - 
			 sizeof(struct pisces_cmd));

    cmd.phys_addr    = reg->base_addr;
    cmd.size         = reg->pages * PAGE_SIZE_4KB;
    
    ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_mem_add),  (u8 **)&resp, &resp_len);
    
    if (ret == 0) {
	kfree(resp);
    } else {
	printk(KERN_ERR "Error adding memory to enclave %d\n", enclave->id);
	// remove memory from enclave
	return -1;
    }
    

    return 0;
}


int 
ctrl_add_cpu(struct pisces_enclave * enclave, 
	     u64                     cpu_id)
{
    struct pisces_ctrl      * ctrl      = &(enclave->ctrl);
    struct pisces_xbuf_desc * xbuf_desc = ctrl->xbuf_desc;
    struct pisces_resp      * resp      = NULL;

    struct cmd_cpu_add cmd;

    int status   = 0;
    u32 resp_len = 0;
    int ret      = 0;

    memset(&cmd, 0, sizeof(struct cmd_cpu_add));

    cmd.hdr.cmd      = PISCES_CMD_ADD_CPU;
    cmd.hdr.data_len = ( sizeof(struct cmd_cpu_add) - 
			 sizeof(struct pisces_cmd));
    cmd.phys_cpu_id  = cpu_id;
    cmd.apic_id      = apic->cpu_present_to_apicid(cpu_id);

    printk("Adding CPU %llu (APIC %llu)\n", cpu_id, cmd.apic_id);

    /* Setup Linux trampoline to jump to enclave trampoline */
    pisces_setup_trampoline(enclave);

    printk("Sending Command\n");

    ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_cpu_add),  (u8 **)&resp, &resp_len);

    if (ret == 0) {
	status = resp->status;
	kfree(resp);
    } else {
	status = -1;
    }
		
    pisces_restore_trampoline(enclave);

    printk("\tDone\n");

    if (status != 0) { 
	// remove CPU from enclave
	return -1;
    }

    return 0;
}


int 
ctrl_add_pci(struct pisces_enclave  * enclave,
	     struct pisces_pci_spec * pci_spec)
{
    struct pisces_ctrl      * ctrl      = &(enclave->ctrl);
    struct pisces_xbuf_desc * xbuf_desc = ctrl->xbuf_desc;
    struct pisces_resp      * resp      = NULL;

    struct cmd_add_pci_dev   cmd;

    int status   = 0;
    u32 resp_len = 0;
    int ret      = 0;

    memset(&cmd, 0, sizeof(struct cmd_add_pci_dev));

    printk("Adding V3 PCI device\n");

    cmd.hdr.cmd      = PISCES_CMD_ADD_V3_PCI;
    cmd.hdr.data_len = ( sizeof(struct cmd_add_pci_dev) - 
			 sizeof(struct pisces_cmd));

    memcpy(&(cmd.spec), pci_spec, sizeof(struct pisces_pci_spec));

    printk(" Notifying enclave\n");
    ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_add_pci_dev), (u8 **)&resp, &resp_len);

    if (ret == 0) {
	status = resp->status;
	kfree(resp);
    } else {
	status = -1;
    } 

    if (status != 0) {
	printk(KERN_ERR "Error adding PCI device to Enclave %d\n", enclave->id);
	enclave_pci_remove_dev(enclave, &cmd.spec);
	return -1;
    }

    return 0;
}


// Allow high level control commands over ioctl
static long 
ctrl_ioctl(struct file   * filp, 
	   unsigned int    ioctl, 
	   unsigned long   arg) 
{
    struct pisces_enclave   * enclave   = filp->private_data;
    struct pisces_ctrl      * ctrl      = &(enclave->ctrl);
    struct pisces_xbuf_desc * xbuf_desc = ctrl->xbuf_desc;
    struct pisces_resp      * resp      = NULL;
    void __user             * argp      = (void __user *)arg;
    u32 resp_len = 0;
    int ret      = 0;
    int status   = 0;


    mutex_lock(&(ctrl->lock));
    {

	if (enclave->state != ENCLAVE_RUNNING) {
	    printk("Attempted Ctrl IOCTL on non-running enclave\n");

	    mutex_unlock(&(ctrl->lock));

	    return -1;
	}

	printk("CTRL IOCTL (%d)\n", ioctl);

	switch (ioctl) {
	    case PISCES_CMD_ADD_CPU: {
	
		u64 cpu_id = (u64)arg;
	    
		if (pisces_enclave_add_cpu(enclave, cpu_id) != 0) {
		    printk(KERN_ERR "Error adding CPU to enclave %d\n", enclave->id);
		    ret = -1;
		    break;
		}

		ret = ctrl_add_cpu(enclave, cpu_id);

		break;
	    }
	    case PISCES_CMD_REMOVE_CPU: {
		struct cmd_cpu_add  cmd;
		u64 cpu_id = (u64)arg;

		memset(&cmd, 0, sizeof(struct cmd_cpu_add));

		cmd.hdr.cmd      = PISCES_CMD_REMOVE_CPU;
		cmd.hdr.data_len = ( sizeof(struct cmd_cpu_add) -
				     sizeof(struct pisces_cmd));
		cmd.phys_cpu_id  = cpu_id;
		cmd.apic_id      = apic->cpu_present_to_apicid(cpu_id);


		printk("Offlining CPU %llu (APIC %llu)\n", cpu_id, cmd.apic_id);

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_cpu_add),  (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		} 

		if (status != 0) {
		    ret = -1;
		    break;
		}

		//pisces_enclave_remove_cpu(enclave, cpu_id);

		break;
	    }

	    case PISCES_CMD_ADD_MEM: {
		struct memory_range reg;

		memset(&reg, 0, sizeof(struct memory_range));

		if (copy_from_user(&reg, argp, sizeof(struct memory_range))) {
		    printk(KERN_ERR "Could not copy memory region from user space\n");
		    ret = -EFAULT;
		    break;
		}

		if (pisces_enclave_add_mem(enclave, reg.base_addr, reg.pages) != 0) {
		    printk(KERN_ERR "Error adding memory descriptor to enclave %d\n", enclave->id);
		    ret = -1;
		    break;
		}

		ret = ctrl_add_mem(enclave, &reg);


		break;
	    }
	    case PISCES_CMD_REMOVE_MEM: {

		printk("Removing memory is not supported\n");
		ret = -1;
		break;
		
	    }
	    case PISCES_CMD_ADD_V3_PCI: {
		struct pisces_pci_spec spec;

		memset(&spec, 0, sizeof(struct pisces_pci_spec));

		if (copy_from_user(&spec, argp, sizeof(struct pisces_pci_spec))) {
		    printk(KERN_ERR "Could not copy pci device structure from user space\n");
		    ret = -EFAULT;
		    break;
		}

		printk("Init an offlined PCI device\n");

		ret = enclave_pci_add_dev(enclave, &spec);

		if (ret != 0) {
		    printk(KERN_ERR "Could not initailize PCI device\n");
		    ret = ret;
		    break;
		}

		ret = ctrl_add_pci(enclave, &spec);

		break;
	    }
	    case PISCES_CMD_FREE_V3_PCI: {
		struct cmd_free_pci_dev   cmd;

		cmd.hdr.cmd      = PISCES_CMD_FREE_V3_PCI;
		cmd.hdr.data_len = ( sizeof(struct cmd_free_pci_dev) - 
				     sizeof(struct pisces_cmd));
	    
		if (copy_from_user(&(cmd.spec), argp, sizeof(struct pisces_pci_spec))) {
		    printk(KERN_ERR "Could not copy pci device structure from user space\n");
		    ret = -EFAULT;
		    break;
		}

		/* Send Free xbuf Call */
		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_add_pci_dev), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		} 

		if (status != 0) {
		    printk(KERN_ERR "Error in PCI free enclave command\n");
		    ret = -1;
		    break;
		}

		/* Free Linux resources */	    
		if (enclave_pci_remove_dev(enclave, &cmd.spec) != 0) {
		    printk(KERN_ERR "Error removing Pisces device from Enclave state\n");
		    ret = -1;
		    break;
		}

		break;
	    }
	    case PISCES_CMD_LAUNCH_JOB: {
		struct cmd_launch_job cmd;
		
		memset(&cmd, 0, sizeof(struct pisces_cmd));

		cmd.hdr.cmd      = PISCES_CMD_LAUNCH_JOB;
		cmd.hdr.data_len = ( sizeof(struct cmd_launch_job) - 
				     sizeof(struct pisces_cmd));

		if (copy_from_user(&(cmd.spec), argp, sizeof(struct pisces_job_spec))) {
		    printk(KERN_ERR "Could not copy job spec from user space\n");
		    ret = -EFAULT;
		    break;
		}

		printk("Launching Job %s\n", cmd.spec.name);

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_launch_job), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		}

		if (status < 0) {
		    printk(KERN_ERR "Error launching job (%s) [ret=%d, status=%d]\n",
			   cmd.spec.name, ret, status);
		    ret = -1;
		    break;
		}

		ret = status;

		break;
	    }
	    case PISCES_CMD_LOAD_FILE: {
		struct cmd_load_file cmd;

		memset(&cmd, 0, sizeof(struct cmd_load_file));

		cmd.hdr.cmd      = PISCES_CMD_LOAD_FILE;
		cmd.hdr.data_len = ( sizeof(struct cmd_load_file) - 
				     sizeof(struct pisces_cmd));

		if (copy_from_user(&(cmd.file_pair), argp, sizeof(struct pisces_file_pair))) {
		    printk(KERN_ERR "Could not copy file names from user space\n");
		    ret = -EFAULT;
		    break;
		}
		
		printk("Loading file into Kitten\n");

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_load_file), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		}

		if (status < 0) {
		    printk(KERN_ERR "Error loading file (%s) [ret=%d, status=%d]\n",
			   cmd.file_pair.lnx_file, ret, status);
		    ret = -1;
		    break;
		}

		ret = status;

		break;
	    } 
	    case PISCES_CMD_STORE_FILE: {
		struct cmd_store_file cmd;

		memset(&cmd, 0, sizeof(struct cmd_store_file));

		cmd.hdr.cmd      = PISCES_CMD_STORE_FILE;
		cmd.hdr.data_len = ( sizeof(struct cmd_store_file) - 
				     sizeof(struct pisces_cmd));

		if (copy_from_user(&(cmd.file_pair), argp, sizeof(struct pisces_file_pair))) {
		    printk(KERN_ERR "Could not copy file names from user space\n");
		    ret = -EFAULT;
		    break;
		}
		
		printk("Loading file into Kitten\n");

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_store_file), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		}

		if (status < 0) {
		    printk(KERN_ERR "Error loading file (%s) [ret=%d, status=%d]\n",
			   cmd.file_pair.lnx_file, ret, status);
		    ret = -1;
		    break;
		}

		ret = status;

		break;
	    }
	    case PISCES_CMD_CREATE_VM: {
		struct cmd_create_vm cmd;

		memset(&cmd, 0, sizeof(struct cmd_create_vm));

		cmd.hdr.cmd      = PISCES_CMD_CREATE_VM;
		cmd.hdr.data_len = ( sizeof(struct cmd_create_vm) -
				     sizeof(struct pisces_cmd));

		if (copy_from_user(&(cmd.path), argp, sizeof(struct vm_path))) {
		    printk(KERN_ERR "Could not copy vm path from user space\n");
		    ret = -EFAULT;
		    break;
		}

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_create_vm),  (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    status = resp->status;
		    kfree(resp);
		} else {
		    status = -1;
		} 

		if (status < 0) {
		    printk("Error creating VM %s (%s) [ret=%d, status=%d]\n", 
			   cmd.path.vm_name, cmd.path.file_name, 
			   ret, status);
		    ret = -1;
		    break;
		}

		ret = status;

		break;
	    }
	    case PISCES_CMD_LAUNCH_VM:
	    case PISCES_CMD_STOP_VM:
	    case PISCES_CMD_FREE_VM:
	    case PISCES_CMD_PAUSE_VM:
	    case PISCES_CMD_CONTINUE_VM: {

		if (send_vm_cmd(xbuf_desc, ioctl, arg) != 0) {
		    printk("Error Stopping VM (%lu)\n", arg);
		    ret =  -1;
		    break;
		}

		break;
	    }

	    case PISCES_CMD_VM_CONS_CONNECT: {
		long long cons_pa = 0;

		cons_pa = send_vm_cmd(xbuf_desc, PISCES_CMD_VM_CONS_CONNECT, arg);

		if (cons_pa <= 0) {
		    printk("Could not acquire console ring buffer\n");
		    ret = -1;
		    break;
		}

		printk("Console found at %p\n", (void *)cons_pa);
		printk("Enclave=%p\n",                  enclave);

		ret = v3_console_connect(enclave, arg, cons_pa);
		break;
	    }

	    case PISCES_CMD_VM_DBG: {
		struct cmd_vm_debug cmd;

		memset(&cmd, 0, sizeof(struct cmd_vm_debug));

	    
		cmd.hdr.cmd      = PISCES_CMD_VM_DBG;
		cmd.hdr.data_len = ( sizeof(struct cmd_vm_debug) - 
				     sizeof(struct pisces_cmd) );

		if (copy_from_user(&(cmd.dbg_spec), argp, sizeof(struct pisces_dbg_spec))) {
		    printk(KERN_ERR "Could not copy debug command from user space\n");
		    ret = -EFAULT;
		    break;
		}

		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct cmd_vm_debug), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    kfree(resp);
		} else {
		    printk(KERN_ERR "Error sending debug command [%d] to VM (%d)\n",
			   cmd.dbg_spec.cmd, cmd.dbg_spec.vm_id);
		    ret = -1;
		    break;
		}

		break;
	    }

	    case PISCES_CMD_SHUTDOWN: {
		struct pisces_cmd cmd;

		memset(&cmd, 0, sizeof(struct pisces_cmd));

		cmd.cmd      = PISCES_CMD_SHUTDOWN;
		cmd.data_len = 0;
	    
		ret = pisces_xbuf_sync_send(xbuf_desc, (u8 *)&cmd, sizeof(struct pisces_cmd), (u8 **)&resp, &resp_len);

		if (ret == 0) {
		    kfree(resp);
		} else {
		    printk(KERN_ERR "Error sending shutdown command to enclave\n");
		    ret = -1;
		    break;
		}

		break;
	    }
	    default:  {
		printk(KERN_ERR "Unknown Enclave IOCTL (%d)\n", ioctl);
		ret = -1;
		break;
	    }
		   

	}
    }
    mutex_unlock(&(ctrl->lock));

    return ret;
}




static int 
ctrl_release(struct inode * i, 
	     struct file  * filp) 
{
    struct pisces_enclave * enclave = filp->private_data;
    // struct pisces_ctrl    * ctrl    = &(enclave->ctrl);

    enclave_put(enclave);

    return 0;
}


static struct file_operations ctrl_fops = {
    .owner          = THIS_MODULE,
    .write          = ctrl_write,
    .read           = ctrl_read,
    .unlocked_ioctl = ctrl_ioctl,
    .release        = ctrl_release,
};



int pisces_ctrl_connect(struct pisces_enclave * enclave) {
    int ctrl_fd  = 0;

    enclave_get(enclave);

    ctrl_fd = anon_inode_getfd("enclave-ctrl", &ctrl_fops, enclave, O_RDWR);

    if (ctrl_fd < 0) {
        printk(KERN_ERR "Error creating Ctrl inode\n");

	enclave_put(enclave);

        return ctrl_fd;
    }

    return ctrl_fd;
}



int
pisces_ctrl_init(struct pisces_enclave * enclave)
{
    struct pisces_ctrl        * ctrl        = &(enclave->ctrl);
    struct pisces_boot_params * boot_params = NULL;


    mutex_init(&(ctrl->lock));

    boot_params     = __va(enclave->bootmem_addr_pa);    
    ctrl->xbuf_desc = pisces_xbuf_client_init(enclave, (uintptr_t)__va(boot_params->control_buf_addr), 0, 0);
    
    return 0;
}

int 
pisces_ctrl_deinit(struct pisces_enclave * enclave)
{
    struct pisces_ctrl * ctrl = &(enclave->ctrl);


    pisces_xbuf_client_deinit(ctrl->xbuf_desc);

    return 0;
}

 
