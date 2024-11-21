CC = gcc
CFLAGS = -m32 -g -Wall

all: libmy_vm.a

libmy_vm.a: my_vm.o
	ar rcs libmy_vm.a my_vm.o

my_vm.o: my_vm.c my_vm.h
	$(CC) $(CFLAGS) -c my_vm.c

test: test.c libmy_vm.a
	$(CC) $(CFLAGS) test.c -L. -lmy_vm -o test
	./test
clean:
	rm -rf *.o *.a test
