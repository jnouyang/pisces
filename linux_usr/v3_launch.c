/* 
 * Pisces/V3 Control utility
 * (c) Jack lange, 2013
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <sys/ioctl.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <unistd.h> 
#include <string.h>
#include <stdint.h>

#include <pet_ioctl.h>

#include "pisces_ioctl.h"
#include "ctrl_ioctl.h"

int main(int argc, char* argv[]) {
    char * enclave_path = argv[1];
    int vm_id = atoi(argv[2]);
    int ctrl_fd;

    if (argc <= 2) {
	printf("usage: v3_launch <pisces_device> <vm_id>\n");
	return -1;
    }

    printf("Launching VM (%s)\n", enclave_path);
    
 
    ctrl_fd = pet_ioctl_path(enclave_path, PISCES_ENCLAVE_CTRL_CONNECT, NULL);

    if (ctrl_fd < 0) {
        printf("Error opening enclave control channel (%s)\n", enclave_path);
        return -1;
    }

    if (pet_ioctl_fd(ctrl_fd, PISCES_CMD_LAUNCH_VM, (void *)(uint64_t)vm_id) != 0) {
	printf("Error: Could not LAUNCH VM\n");
	return -1;
    }

    close(ctrl_fd);
    
    return 0; 
} 


