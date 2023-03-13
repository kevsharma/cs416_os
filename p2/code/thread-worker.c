#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <assert.h>

typedef unsigned int worker_t;

typedef struct TCB {
	unsigned int 	thread_id;
	ucontext_t 		*uctx; 
} tcb; 

typedef struct Node {
    tcb *data;
    struct Node *next;
} Node;

/* Circular Linked List */
typedef struct Queue {
    unsigned short size;
    struct Node *rear;
} Queue;


int isUninitialized(Queue *q_ptr) {
    return !q_ptr;
}

int isEmpty(Queue *q_ptr) { 
    assert(!isUninitialized(q_ptr));
    return q_ptr->size == 0;
}

/* O(1) Enqueue Operation */
void enqueue(Queue* q_ptr, tcb *data) {
    assert(!isUninitialized(q_ptr));

	Node *item = (Node *) malloc(sizeof(Node));
	item->data = data;
	item->next = NULL;

    if (isEmpty(q_ptr)) {
        // Note that rear is null.
        q_ptr->rear = item;
        q_ptr->rear->next = item;
    } else {
        // Rear is not null.
        item->next = q_ptr->rear->next;
        q_ptr->rear->next = item;
        q_ptr->rear = item;
    }
    
    ++(q_ptr->size);
}

/* Retrieves and removes the head of this queue, or returns null if this queue is empty. */
tcb* dequeue(Queue *q_ptr) {
    assert(!isUninitialized(q_ptr));
    assert(!isEmpty(q_ptr));

    Node* front;

    if (q_ptr->size == 1) {
        front = q_ptr->rear; // rear is front.
        front->next = NULL; // rear pointed to itself.
        q_ptr->rear = NULL;
    } else {
		// if size == 2, dequeue results in rear pointing to itself.
		front = q_ptr->rear->next;
		q_ptr->rear->next = front->next;
	}
	
	--(q_ptr->size);

    // Extract data and free queue abstraction (node).
	tcb *result = front->data;
	free(front);
	
	return result;
}


void print_queue(Queue* q_ptr) {
	if (isUninitialized(q_ptr)) {
		printf("INFO[print_queue]: q_ptr unintialized\n");
		return;
	}
	
	if (isEmpty(q_ptr)) {
		printf("INFO[print_queue]: queue is empty\n");
		return;
	}

    Node* iterator = q_ptr->rear->next; // start at front;
    for(; iterator != q_ptr->rear; iterator = iterator->next)
        printf("%d, ", iterator->data->thread_id);
    printf("%d\n", iterator->data->thread_id);
}


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
////////////////////////////////////////////////////////////////////////////////////////////
static Queue *q_arrival;
static Queue *q_scheduled;

static ucontext_t *scheduler; // In a repeat loop in schedule();
static ucontext_t *cleanup; // Cleans up after a worker thread ends. Workers' uc_link points to this;

static tcb *running; // Currently executing thread; CANNOT BE SCHEDULER, CLEANUP;
////////////////////////////////////////////////////////////////////////////////////////////

void init_queues() {
	assert(isUninitialized(q_arrival));
	assert(isUninitialized(q_scheduled));

	q_arrival = malloc(sizeof(Queue));
	q_arrival->size = 0;
	q_arrival->rear = NULL;

	q_scheduled = malloc(sizeof(Queue));
	q_scheduled->size = 0;
	q_scheduled->rear = NULL;
}

void deinit_queues() {
	assert(isEmpty(q_arrival) && isEmpty(q_scheduled));
	free(q_arrival);
	free(q_scheduled);
	q_arrival = NULL;
	q_scheduled = NULL;
}

void schedule() {
	printf("INFO[schedule 1]: Entered Scheduler for the first time\n");
	// If either queue contains a job, scheduler not done.q_arrival 
	// Further, if running not cleaned up - it was preempted.
	while(!isEmpty(q_arrival) || !isEmpty(q_scheduled) || (running != NULL)) {
		printf("INFO[schedule 2]: entered scheduler loop. Scheduled Queue: ");
		print_queue(q_scheduled);
		// Insert newly arrived jobs into schedule queue.
		if (!isEmpty(q_arrival)) {
			tcb *newly_arrived_job = dequeue(q_arrival);
			enqueue(q_scheduled, newly_arrived_job);
			printf("INFO[schedule 3]: put tid (%d) into scheduled queue.\n", newly_arrived_job->thread_id);
			continue;
		}

		printf("INFO[schedule 3.5]: About to perform running check.\n");
		// Both queues empty, but exists one remaining job - running.
		if(running != NULL) {
			printf("INFO[schedule 4]: Enqueued Running tid (%d)\n", running->thread_id);
			enqueue(q_scheduled, running);
		}
		printf("INFO[schedule 4.5]: Running is NULL.\n");

		running = dequeue(q_scheduled);
		printf("DEBUG[schedule 5]: dequeued %d\n", running->thread_id);
		printf("DEBUG[schedule 6]: dequeued uctx: %d\n", running->uctx);

		swapcontext(scheduler, running->uctx);
	}

	printf("INFO[schedule -1]: Exited scheduler loop");
	// Supporting Mechanisms
	deinit_queues();
	// Context flows to cleanup.
}

void perform_cleanup() {
	// If while condition is true, then scheduler job has not completed.
	printf("INFO[cleanup context]: entered perform_clean()\n");
	while(!isUninitialized(q_arrival) && !isUninitialized(q_scheduled)) {
		worker_t tid_ended = running->thread_id;
		printf("INFO[CLEANUP]: Cleaned running tid (%d)\n", tid_ended);
		// JOIN search: filter queue to update any worker waiting on tid_ended
		// Allocated Heap space for TCB and TCB->uctx and TCB->uctx->uc_stack base ptr

		if (tid_ended != 0) {
			/* main context's stack was not allocated. */
			free(running->uctx->uc_stack.ss_sp); // Free the worker's stack
		}
		free(running->uctx);
		free(running);
		running = NULL; // IMPORTANT FLAG (see scheduler while guard)
		swapcontext(cleanup, scheduler);
	}

	printf("\nINFO[cleanup context]: frees initiated of scheduler and cleanup contexts\n");
	// Scheduler done too.
	free(scheduler->uc_stack.ss_sp);
	free(scheduler);
	free(cleanup->uc_stack.ss_sp);
	free(cleanup);
}

int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg)
{
	if (!scheduler) {
		// First time library called.
		init_queues();
		printf("INFO[worker_create]: printing arrival: \t\t");
		print_queue(q_arrival);
		printf("INFO[worker_create]: printing scheduled: \t");
		print_queue(q_scheduled);

		// Scheduler and Cleanup should be contacted from anywhere
		scheduler = (ucontext_t *) malloc(sizeof(ucontext_t));
		cleanup = (ucontext_t *) malloc(sizeof(ucontext_t));

		// Create Scheduler
		getcontext(scheduler);
		scheduler->uc_link = cleanup;
		scheduler->uc_stack.ss_size = 4096;
		scheduler->uc_stack.ss_sp = malloc(4096);
		makecontext(scheduler, schedule, 0);

		// Create cleanup
		getcontext(cleanup);
		cleanup->uc_link = NULL;
		cleanup->uc_stack.ss_size = 4096;
		cleanup->uc_stack.ss_sp = malloc(4096);
		makecontext(cleanup, perform_cleanup, 0);
		
		// Create tcb for main
		running = (tcb *) malloc(sizeof(tcb));
		running->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
		running->thread_id = 0; // main first program
		getcontext(running->uctx);
		running->uctx->uc_link = cleanup;
	}

	printf("INFO[worker_create]: created TCB for new worker\n");
	// Create tcb for new_worker and put into q_arrival queue
	tcb* new_tcb = (tcb *) malloc(sizeof(tcb));
	new_tcb->thread_id = *thread; // thread id stored in tcb
	
	new_tcb->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(new_tcb->uctx); // heap space stores context
	new_tcb->uctx->uc_link = cleanup; // all workers must flow into cleanup
	new_tcb->uctx->uc_stack.ss_size = 4096;
	new_tcb->uctx->uc_stack.ss_sp = malloc(4096);
	makecontext(new_tcb->uctx, (void *) function, 1, arg); // TO-DO: verify correctness...

	enqueue(q_arrival, new_tcb);
	
	printf("INFO[worker_create]: enqueued new worker into Q_arrival\n");
	printf("INFO[worker_create]: printing arrival: \t\t");
	print_queue(q_arrival);
	return 0;
};

///////////////////////////////////////////

void* func_bar(void *) {
	printf("WORKER %d: func_bar started\n", running->thread_id);
	printf("WORKER %d: func_bar ended\n", running->thread_id);
	return NULL;
}

int main(int argc, char **argv) {
	printf("MAIN: Starting main: no queues running yet\n");

	worker_t worker_1 = 17;
	worker_create(&worker_1, NULL, (void *) &func_bar, NULL);

	/*
	worker_t worker_2 = 38;
	worker_create(&worker_2, NULL, (void *) &func_bar, NULL);

	worker_t worker_3 = 77;
	worker_create(&worker_3, NULL, (void *) &func_bar, NULL);
	*/

	printf("MAIN: Ending main\n");
	setcontext(cleanup);
}


// gcc -o thread-worker thread-worker.c -Wall -fsanitize=address -fno-omit-frame-pointer

/**
 * maintain list of all TCBs-> this is used to search for waiting by worker_exit.
 * tcb gets two more attirbutes, join_tid; join_retval;
 * 
 * function join(thread_end, retval*) {
 * 		running->join_tid-> = thead_end;
 * 		// retval will be populated when the thread_end calls worker_exit(value);
 * 		swapcontext(running->uctx, scheduler);
 * 
 * 		// exit called by join_tid would have stored the retval in tcb struct
 * 		if retval != NULL:		
 * 			retval* = running->join_retval;
 * }
 * 
 * function worker_exit(void *value_ptr) {
 * 		if value_ptr != NULL:
 * 			// cycle through tcb list to find any waiting on this thread, and change their retval
 * 			for tcb in tcb list:
 * 				if tcb->join_tid -> running->thread_id:
 * 					tcb->join_retval = value_ptr;
 * 
 * 		// because of uc_link -> cleanup gets passed control and cleans the list.
 * }
 * 
 * function worker_yield(void *value_ptr) {
 * 		swapcontext(running->uctx, scheduler);
 * }
 *
 * 
 * ToDo 
 * 1. list structure for TCBs (searchable, insert, delete)
 * 2. tcb modify to add two new attributes: worker_t join_tid (initited to -1), void *join_retval
 * 
 * Control Flow Ramificatins:
 * 3. modify init to malloc space for the new variables
 * 4. malloc space for new list
 * 5. cleanup() fixes: 
 * 		- while (tcb list not empty) condition
 * 		- free newly malloc space
 * 		- free tcb list after completion of main
 * 
 * 6. worker_yield
 * 7. worker_exit
 * 8. worker_join
 * 
 * 
*/