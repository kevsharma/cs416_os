#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <ucontext.h>

#define STACK_SIZE SIGSTKSZ

void simplef(){
  puts("Donald- you are threaded\n");
}

int main(int argc, char **argv) {
	ucontext_t cctx,ncctx;
	
	if (argc != 1) {
		printf(": USAGE Program Name and no Arguments expected\n");
		exit(1);
	}
	
	if (getcontext(&cctx) < 0){
		perror("getcontext");
		exit(1);
	}
	
	// Allocate space for stack
	void *stack=malloc(STACK_SIZE);
	
	if (stack == NULL){
		perror("Failed to allocate stack");
		exit(1);
	}
	
	/* Setup context that we are going to use */
	cctx.uc_link=NULL;
	cctx.uc_stack.ss_sp=stack;
	cctx.uc_stack.ss_size=STACK_SIZE;
	cctx.uc_stack.ss_flags=0;
	
	puts(" about to call make  context");
	
	// Setup the context to start running at simplef
	makecontext(&cctx,(void *)&simplef,0);
	puts("Successfully modified context");
	
	// Set the current context to run the context that runs simplef	
	setcontext(&cctx);  
	
	// Note: the following statement doesn't run because set context switched
	//       to the other function then exited
	puts("make  context executed correctly \n");
	
	return 0;
}
