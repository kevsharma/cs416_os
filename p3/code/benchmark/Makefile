
all : test
test: ../my_vm.h
	gcc test.c -L../ -lmy_vm -m32 -o test

mtest: gcc multi_test.c -L../ -lmy_vm -m32 -o mtest -lpthread

clean:
	rm -rf test mtest
