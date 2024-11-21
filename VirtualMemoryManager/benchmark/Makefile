CC = gcc
CFLAGS = -g -Wall -m32 
LDFLAGS = -m32 -lpthread

# Library creation
../libmy_vm.a: ../my_vm.o
	ar rcs ../libmy_vm.a ../my_vm.o

../my_vm.o: ../my_vm.c ../my_vm.h
	$(CC) $(CFLAGS) -c ../my_vm.c -o ../my_vm.o

# Test executables
all: test mtest

test: test.c ../libmy_vm.a
	$(CC) test.c -L.. -lmy_vm $(CFLAGS) $(LDFLAGS) -o test

mtest: multi_test.c ../libmy_vm.a
	$(CC) multi_test.c -L.. -lmy_vm $(CFLAGS) $(LDFLAGS) -o mtest

clean:
	rm -f test mtest ../my_vm.o ../libmy_vm.a

.PHONY: all clean