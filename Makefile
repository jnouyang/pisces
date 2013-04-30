KERN_PATH=../linux-3.7.1


obj-m += pisces.o

pisces-objs :=  src/main.o \
    		src/pisces_dev.o \
		src/pisces_loader.o \
		src/pisces_cons.o \
		src/wakeup_secondary.o \
		src/domain_xcall.o \
		src/buddy.o \
		src/enclave.o \
		src/pisces_lock.o \
		src/pisces_ringbuf.o \
		src/numa.o \
		src/mm.o \
		src/file_io.o \
		src/launch_code.o

all:
	make -C $(KERN_PATH) M=$(PWD) modules

clean:
	make -C $(KERN_PATH) M=$(PWD) clean

.PHONY: tags
tags:
	ctags -R src/
