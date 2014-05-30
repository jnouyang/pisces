/* 
 * V3 Control utility
 * (c) Jack lange, 2011
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <sys/ioctl.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <unistd.h> 
#include <string.h>
 

#include <pet_types.h>
#include <pet_ioctl.h>

#include "../src/pisces.h"
#include "../src/pisces_cmds.h"


int main(int argc, char* argv[]) {
    char * enclave_path = argv[1];
    int vm_id = atoi(argv[2]);
    int ctrl_fd;
    int err;

    if (argc <= 2) {
	printf("usage: v3_free <pisces-enclave> <vm-id>\n");
	return -1;
    }




    printf("Freeing VM [%d] on enclave %s\n", vm_id, enclave_path);
    
    ctrl_fd = pet_ioctl_path(enclave_path, PISCES_ENCLAVE_CTRL_CONNECT, NULL);

    if (ctrl_fd < 0) {
        printf("Error opening enclave control channel (%s)\n", enclave_path);
        return -1;
    }

    if (pet_ioctl_fd(ctrl_fd, ENCLAVE_CMD_FREE_VM, (void *)(uint64_t)vm_id) != 0) {
	printf("Error: Could not STOP VM\n");
	return -1;
    }

    /* Close the file descriptor.  */ 
    close(ctrl_fd); 

    return 0; 
} 


