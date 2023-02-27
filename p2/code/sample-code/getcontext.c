#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <ucontext.h>

#define STACK_SIZE SIGSTKSZ

int main(int argc, char **argv) {
	ucontext_t cctx,ncctx;
	
	if (argc != 1) {
		printf(": USAGE Program Name and No  Arguments expected\n");
		exit(1);
	}
	
	if (getcontext(&cctx) < 0){
		perror("getcontext");
		exit(1);
	}
	
	puts("I am going again and again with no context \n");
	
	if (setcontext(&cctx) < 0){
		perror("set current context");
		exit(1);
	}
	
	puts(" Looks like I am lost \n");
        
	return 0;
}
