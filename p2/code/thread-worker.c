#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

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

void queue_init(Queue* q_ptr) {
	q_ptr = (Queue *) malloc(sizeof(Queue));
    q_ptr->size = 0;
    q_ptr->rear = NULL;
}

void queue_deinit(Queue *q_ptr) {
	assert(isEmpty(q_ptr));
	free(q_ptr);
	q_ptr = NULL;
}

int isUninitialized(Queue *q_ptr) {
    return !q_ptr;
}

int isEmpty(Queue *q_ptr) {
    return !q_ptr->size;
}

/* O(1) Enqueue Operation */
void enqueue(Queue* q_ptr, tcb *data) {
	Node *item = (Node *) malloc(sizeof(Node));
	item->data = data;
	item->next = NULL;

    if (isUninitialized(q_ptr)) {
        queue_init(q_ptr);
    }

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
    if (isUninitialized(q_ptr) || isEmpty(q_ptr)) {
        return NULL;
    }

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
	tcb *result = front->data;
	free(front);
	
	return result;
}


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
////////////////////////////////////////////////////////////////////////////////////////////
static Queue *arrival;
static Queue *scheduled;

static ucontext_t *scheduler; // In a repeat loop in schedule();
static ucontext_t *cleanup; // Cleans up after a worker thread ends. Workers' uc_link points to this;

static tcb *running; // Currently executing thread; CANNOT BE SCHEDULER, CLEANUP;
////////////////////////////////////////////////////////////////////////////////////////////

void schedule() {
	// If either queue contains a job, scheduler not done. 
	// Further, if running not cleaned up - it was preempted.
	while(!isEmpty(arrival) || !isEmpty(scheduled) || (running != NULL)) {
		// Insert newly arrived jobs into schedule queue.
		if (!isEmpty(arrival)) {
			enqueue(scheduled, dequeue(arrival));
			continue;
		}

		// Both queues empty, but exists one remaining job - running.
		if(running != NULL) {
			enqueue(scheduled, running);
		}

		running = dequeue(scheduled);
		swapcontext(scheduler, running);
	}

	// Supporting Mechanisms
	queue_deinit(arrival);
	queue_deinit(scheduled);
	// Context flows to cleanup.
}

int scheduler_incomplete() {
	return !isUninitialized(arrival) && !isUninitialized(scheduled);
}

void perform_cleanup() {
	// If while condition is true, then scheduler job has not completed.
	while(scheduler_incomplete()) {
		worker_t tid_ended = running->thread_id;
		free(running->uctx->uc_stack.ss_sp); // Free the worker's stack
		free(running);
		running = NULL; // IMPORTANT FLAG (see scheduler while guard)
		// JOIN search: filter queue to update any worker waiting on tid_ended
		swapcontext(cleanup, scheduler);
	}

	// Scheduler done too.
	free(scheduler->uc_stack.ss_sp);
	scheduler = NULL;	
	cleanup = NULL;
}

void initialize_library() {
	queue_init(arrival);
	queue_init(scheduled);

	// Running TCB is also empty: Give it the value of main.
	running = (tcb *) malloc(sizeof(tcb)); // we need to set the uclink to cleanup.
	running->thread_id = 0; // main thread which started the process, we don't know stack.
	getcontext(running->uctx);

	// Enqueue main to arrival
	enqueue(arrival, running);

	// Create Scheduler
	getcontext(scheduler);
	scheduler->uc_link = cleanup;
	scheduler->uc_stack.ss_size = 4096;
	scheduler->uc_stack.ss_sp = malloc(4096);
	makecontext(scheduler, schedule, 0);

	// Create cleanup
	getcontext(cleanup);
	char cleanup_stack[16384];
	cleanup->uc_link = NULL;
	cleanup->uc_stack.ss_size = 16384;
	cleanup->uc_stack.ss_sp = cleanup_stack; // lightweight, no need for heap.
	makecontext(cleanup, perform_cleanup, 0);
}

tcb* new_worker_tcb(worker_t * thread, void *(*function)(void*), void * arg) {
	// Create the TCB
	tcb* new_tcb = (tcb *) malloc(sizeof(tcb));

	// initialize struct item
	new_tcb->thread_id = *thread;

	// initialize struct item
	getcontext(new_tcb->uctx);
	new_tcb->uctx->uc_link = cleanup;
	new_tcb->uctx->uc_stack.ss_size = 4096;
	new_tcb->uctx->uc_stack.ss_sp = malloc(4096);
	// I assume that (void *) means only one of any type of argument
	makecontext(new_tcb->uctx, function, 1, arg); // TO-DO: verify correctness...

	return new_tcb;
}

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {
	if (!scheduler) {
		initialize_library();
	}
	
	tcb* new_worker = new_worker_tcb(thread, function, arg);
	enqueue(arrival, new_worker);

	return swapcontext(running->uctx, scheduler);
};

