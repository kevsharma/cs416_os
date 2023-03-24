#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define handle_error(msg) \
    do {perror(msg); exit(EXIT_FAILURE); } while (0)

static ucontext_t uctx_scheduler, uctx_foo, uctx_bar;
static ucontext_t *scheduled_worker;

enum running {FOO, BAR}; // we won't need this in project since we dequeued worker.
static enum running scheduled;

static struct itimerval timer;
static struct sigaction sa;


// Timer Interrupt Handler
void timer_interrupt_handler(int signum) {
	printf("DEBUG: Swapping to Scheduler\n");
	// note that scheduled_worker points to the correct worker.
	int swap_error = swapcontext(scheduled_worker, &uctx_scheduler);

	if(swap_error == -1)
		handle_error("ERROR: Timer interrupt Swapping\n");
}

void countdown_timer() {
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	// The type suseconds_t is a signed integral type 
	// capable of storing values at least in the range [-1, 1,000,000].

	// Set up current timer to go off in 1 second
	timer.it_value.tv_usec = 1;
	timer.it_value.tv_sec = 0;

	setitimer(ITIMER_PROF, &timer, NULL);
}

void register_timer_handler() {
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &timer_interrupt_handler;
	sigaction(SIGPROF, &sa, NULL);
}

// only uctx_scheduler access this
void schedule() {
	while(1) {
	// Run the other worker by determining which ran.
		if (scheduled == FOO) {
			printf("Determined that FOO ran.\n");
			scheduled_worker = &uctx_bar;
			scheduled = BAR;
		} 
		else if (scheduled == BAR) {
			printf("Determined that BAR ran.\n");
			scheduled_worker = &uctx_foo;
			scheduled = FOO;
		}

		// Run the newly scheduled worker
		printf("Swapping...\n");

		countdown_timer();
		swapcontext(&uctx_scheduler, scheduled_worker);
	}
}

void func_bar(void) {
	while(1) {
		printf("INFO: Called from Bar context.\n");
	}
}

void func_foo(void) {
	while(1) {
		printf("INFO: Called from Foo context.\n");
	}
}


int main(int argc, char* argv[]) {

	// Register handler so it shows up in foo/bar environment
	register_timer_handler();

    char bar_stack[4096];
    char foo_stack[4096];

	getcontext(&uctx_foo);
	uctx_foo.uc_link = &uctx_scheduler;
	uctx_foo.uc_stack.ss_sp = foo_stack;
	uctx_foo.uc_stack.ss_size = 4096;
	makecontext(&uctx_foo, func_foo, 0);

	getcontext(&uctx_bar);
	uctx_bar.uc_link = &uctx_scheduler;
	uctx_bar.uc_stack.ss_sp = bar_stack;
	uctx_bar.uc_stack.ss_size = 4096;
	makecontext(&uctx_bar, func_bar, 0);

	printf("Letting foo run first by setting scheduled to bar\n");
	printf("=================================================\n");
	
	scheduled = BAR;
	schedule();

	return 0;
}

