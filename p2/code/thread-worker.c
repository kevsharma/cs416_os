#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <assert.h>

/**
 * When the library is first used, this calling context populates
 * the running tcb struct's thread_id attribute with MAIN_THREAD.
*/
#define MAIN_THREAD 1

/**
 * Thread identifiers start with 1 for the main thread and are assigned
 * monotinically increasing integers upon creation of a new thread.
 * 
 * The NONEXISTENT_THREAD definition is used in two places, both of which
 * have considerable ramifications for scheduling logic and thus is
 * imperative to get right:
 * 	1. tcb's join_tid attribute: here, the def. is used to indicate that
 * 								the thread is not waiting for another's completion.
 *  2. mutex list's worker_mutex_t: here, the def. is used to indiciate that 
 * 								no thread holds has acquired the associated mutex.
 */
#define NONEXISTENT_THREAD 0 

/**
 * Mutex numbers are monotonically increasing integers starting with 1.
 * If a mutex number is zero and stored within a tcb's seeking_lock, there
 * are ramifications for scheduling for that thread. See more below.
*/
#define INITIAL_MUTEX 1 

/**
 * NONEXISTENT_MUTEX is used within seeking_lock attribute of TCB
 * to indicate that a thread is not waiting to acquire any lock.
 * 
 * The seeking_lock attribute of a tcb is used by the scheduler to indicate
 * whether the thread should be skipped in light of the thread waiting
 * for another thread to unlock a mutex.
*/
#define NONEXISTENT_MUTEX 0

typedef unsigned int worker_t;
typedef unsigned int mutex_num;

/* mutex struct definition */
typedef struct worker_mutex_t {
	mutex_num lock_num;
	worker_t holder_tid;
} worker_mutex_t;

typedef struct TCB {
	worker_t 		thread_id;
	ucontext_t 		*uctx; 
	worker_t		join_tid;		/* NONEXISTENT_THREAD if not currently waiting on another thread. */
	void **			join_retval;	
	mutex_num		seeking_lock;	/* NONEXISTENT_MUTEX if not seeking to acquire a mutex. */
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

/* Singular Linked List */
typedef struct List {
    unsigned short size;
    struct Node *front;
} List;

typedef struct mutex_node {
    worker_mutex_t *data;
    struct mutex_node *next;
} mutex_node;

/* Singular Linked List */
typedef struct mutex_list {
    struct mutex_node *front;
} mutex_list;


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

int isUninitializedList(List *lst_ptr) {
    return !lst_ptr;
}

int isEmptyList(List *lst_ptr) { 
    assert(!isUninitializedList(lst_ptr));
    return lst_ptr->size == 0;
}

/* Inserts to front of list. */
void insert(List *lst_ptr, tcb *data) {
    assert(!isUninitializedList(lst_ptr));

    Node *item = (Node *) malloc(sizeof(Node));
    item->data = data;
    item->next = lst_ptr->front;
    ++(lst_ptr->size);
    lst_ptr->front = item;
}

/* Returns 1 if the list contains the worker, else returns 0.*/
int contains(List *lst_ptr, worker_t target) {
    Node *ptr = lst_ptr->front;
    while(ptr) {
        if (ptr->data->thread_id == target) {
            return 1;
        }
        ptr = ptr->next;
    }

    return 0;
}

tcb* remove_from(List *lst_ptr, worker_t target) {
    assert(!isEmptyList(lst_ptr));
    assert(contains(lst_ptr, target));

    Node *front = lst_ptr->front;
    tcb *target_tcb;

    if (front->data->thread_id == target) {
        Node *match = front;
        target_tcb = match->data;
        lst_ptr->front = front->next;
        --(lst_ptr->size);

        free(match);
        return target_tcb;
    }

    Node *ptr = front->next;
    Node *prev = front;

    for (; !ptr; prev = prev->next, ptr = ptr->next) {
        if (ptr->data->thread_id != target) {
            continue;
        }

        prev->next = ptr->next;
        --(lst_ptr->size);

        target_tcb = ptr->data;
        free(ptr);
        break;
    }

    return target_tcb;
}

void print_list(List *lst_ptr) {
    Node *ptr = lst_ptr->front;
    printf("Printing lst: ");
    
    while(ptr) {
        printf("%d ", ptr->data->thread_id);
        ptr = ptr->next;
    }

    printf("\n");
}

void print_mutex_list(mutex_list *mutexes) {
    mutex_node *ptr = mutexes->front;
    printf("Printing lst: \n");
    
    while(ptr) {
        printf("%d held by %d.\n", ptr->data->lock_num, ptr->data->holder_tid);
        ptr = ptr->next;
    }

    printf("\n");
}

// INITAILIZE ALL YOUR OTHER VARIABLES HERE
////////////////////////////////////////////////////////////////////////////////////////////
static Queue *q_arrival;
static Queue *q_scheduled;
static List *tcbs;

static ucontext_t *scheduler; // In a repeat loop in schedule();
static ucontext_t *cleanup; // Cleans up after a worker thread ends. Workers' uc_link points to this;

static tcb *running; // Currently executing thread; CANNOT BE SCHEDULER, CLEANUP;
static mutex_list *mutexes; // list of (mutex_num, holder_tid) for currently init-d mutexes.

static worker_t *last_created_worker_tid;
static mutex_num *current_mutex_num;
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

void init_list() {
	assert(isUninitializedList(tcbs));
	tcbs = malloc(sizeof(List));
	tcbs->size = 0;
	tcbs->front = NULL;
}

void deinit_queues() {
	assert(isEmpty(q_arrival) && isEmpty(q_scheduled));
	free(q_arrival);
	free(q_scheduled);
	q_arrival = NULL;
	q_scheduled = NULL;
}

void deinit_list() {
	assert(tcbs->size <= 1); /* Only the main context within list */
	free(tcbs);
	tcbs = NULL;
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
		//printf("DEBUG[schedule 6]: dequeued uctx: %d\n", running->uctx);

		swapcontext(scheduler, running->uctx);
	}

	printf("INFO[schedule -1]: Exited scheduler loop");
	// Supporting Mechanisms
	deinit_queues();
	deinit_list();
	// Context flows to cleanup.
}

void alert_waiting_threads(worker_t ended, void *ended_retval_ptr) {
	Node *ptr = tcbs->front;

	// Notify all threads who called worker_join(ended, retval).
	while(!ptr) {
		if (ptr->data->join_tid == ended) {
			ptr->data->join_tid = NONEXISTENT_THREAD; // No longer waiting.

			// Save data from exiting thread.
			if (ended_retval_ptr != NULL) {
				// This is only ever called from worker_exit. Not from cleanup().
				// When cleanup calls this function, *join_retval is not overwritten.
				*(ptr->data->join_retval) = ended_retval_ptr;
			}
		}
		ptr = ptr->next;
	}
}

void perform_cleanup() {
	// If while condition is true, then scheduler job has not completed.
	printf("INFO[cleanup context]: entered perform_clean()\n");
	while(!isUninitializedList(tcbs)) {
		worker_t tid_ended = running->thread_id;
		printf("INFO[CLEANUP]: Cleaned running tid (%d)\n", tid_ended);
		alert_waiting_threads(tid_ended, NULL); // Never overwrite join return value.
		// JOIN search: filter queue to update any worker waiting on tid_ended
		remove_from(tcbs, tid_ended);

		// Allocated Heap space for TCB and TCB->uctx and TCB->uctx->uc_stack base ptr
		if (tid_ended != MAIN_THREAD) {
			/* main context's stack was not allocated. */
			free(running->uctx->uc_stack.ss_sp); // Free the worker's stack
		}
		free(running->uctx);
		free(running);
		running = NULL; // IMPORTANT FLAG (see scheduler while guard)
		swapcontext(cleanup, scheduler);
	}

	// free supporting memory allocated mechanisms.
	printf("\nINFO[cleanup context]: frees supporting mechanisms\n");
	free(last_created_worker_tid);
	free(current_mutex_num);
	free(mutexes);

	printf("\nINFO[cleanup context]: frees initiated of scheduler and cleanup contexts\n");
	// Scheduler done too.
	free(scheduler->uc_stack.ss_sp);
	free(scheduler);
	free(cleanup->uc_stack.ss_sp);
	free(cleanup);
	scheduler = cleanup = NULL;
}

void init_library() {
	// Initialize supporting mechanisms.
	init_queues();
	init_list();

	printf("INFO[worker_create]: printing arrival: \t\t");
	print_queue(q_arrival);
	printf("INFO[worker_create]: printing scheduled: \t");
	print_queue(q_scheduled);

	// Scheduler and Cleanup should be referenced from any context (hence heap).
	scheduler = (ucontext_t *) malloc(sizeof(ucontext_t));
	cleanup = (ucontext_t *) malloc(sizeof(ucontext_t));

	// Create scheduler context
	getcontext(scheduler);
	scheduler->uc_link = cleanup;
	scheduler->uc_stack.ss_size = 4096;
	scheduler->uc_stack.ss_sp = malloc(4096);
	makecontext(scheduler, schedule, 0);

	// Create cleanup context
	getcontext(cleanup);
	cleanup->uc_link = NULL;
	cleanup->uc_stack.ss_size = 4096;
	cleanup->uc_stack.ss_sp = malloc(4096);
	makecontext(cleanup, perform_cleanup, 0);

	// worker_create should get a monotonically increasing worker_t tid.
	last_created_worker_tid = (worker_t *) malloc(sizeof(worker_t));
	*last_created_worker_tid = MAIN_THREAD;

	// mutexes should not overlap - also monotonically increasing.
	current_mutex_num = (mutex_num *) malloc(sizeof(mutex_num));
	*current_mutex_num = INITIAL_MUTEX;
	mutexes = (mutex_list *) malloc(sizeof(mutex_list));
}

int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg)
{
	if (!scheduler) {
		// First time library called, hence initialize the library:
		init_library();
		
		// Create tcb for main
		running = (tcb *) malloc(sizeof(tcb));
		running->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
		running->thread_id = MAIN_THREAD;
		getcontext(running->uctx);
		running->uctx->uc_link = cleanup;
		
		insert(tcbs, running);
	}

	printf("INFO[worker_create]: created TCB for new worker\n");
	// Create tcb for new_worker and put into q_arrival queue
	tcb* new_tcb = (tcb *) malloc(sizeof(tcb));
	new_tcb->thread_id = ++(*last_created_worker_tid); // thread id stored in tcb
	
	new_tcb->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(new_tcb->uctx); // heap space stores context
	new_tcb->uctx->uc_link = cleanup; // all workers must flow into cleanup
	new_tcb->uctx->uc_stack.ss_size = 4096;
	new_tcb->uctx->uc_stack.ss_sp = malloc(4096);
	new_tcb->join_tid = 0; // not waiting on anyone
	new_tcb->join_retval = NULL;
	makecontext(new_tcb->uctx, (void *) function, 1, arg); // TO-DO: verify correctness...

	insert(tcbs, new_tcb);
	enqueue(q_arrival, new_tcb);
	
	printf("INFO[worker_create]: enqueued new worker into Q_arrival\n");
	printf("INFO[worker_create]: printing arrival: \t\t");
	print_queue(q_arrival);
	return 0;
};

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield() {
	return swapcontext(running->uctx, scheduler);
}

/* terminate a thread */
void worker_exit(void *value_ptr) {
	alert_waiting_threads(running->thread_id, value_ptr);
	// Collapse to cleanup.
}

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	running->join_tid = thread;
	running->join_retval = value_ptr; // alert function will modify this. 
	return swapcontext(running->uctx, scheduler);
}


int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
    // block signals (we will access shared mutex list).
    assert(mutexes != NULL);

    // Create mutex.
    mutex = (worker_mutex_t *) malloc(sizeof(worker_mutex_t));
    mutex->lock_num = (*current_mutex_num)++;
    mutex->holder_tid = NONEXISTENT_THREAD;

    // Insert created mutex
    mutex_node *mutex_item = (mutex_node *) malloc(sizeof(mutex_node));
    mutex_item->data = mutex;
    mutex_item->next = mutexes->front;
    mutexes->front = mutex_item;

    // unblock signals.
    return 0;
}

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {
    // block signals - accesses shared resource mutex.

    /**
     * The thread is blocked from returning from this function
     * until it can succesfully acquire the lock.
     * 
     * Note that when the lock is released, it is not necessary
     * that this thread acquires the lock. Therefore, we must reset
     * the seeking_lock attribute to the desired lock.
     * 
     * The Scheduler will not schedule any thread that is waiting
     * on another thread to release a lock. Suppose A holds mutex m,
     * and B and C wish to acquire mutex m. When A releases, B and C's 
     * seeking_lock is reset such that they are no longer waiting.
     * - If B is scheduled after A releases the lock, then B
     * acquires the lock. When B is preempted and C is scheduled (for example),
     * then C will find that B still holds the lock and will set it's 
     * seeking_lock to waiting for m again.
     * - After B releases, there is no more race and C can acquire the 
     * lock after it is scheduled.
     * 
     * The scheduler skips any thread waiting, but because A's relinquishing
     * of the lock triggered a cycle through the tcb list to clear out every
     * thread's seeking variable (if and only if that thread was seeking m), 
     * then the scheduler can then schedule B and C. The reasoning behind
     * this is that there is no point to schedule B or C until A gives up
     * the lock since B or C can make no meaningful progress into the 
     * critical section anyway.
    */
    while(mutex->holder_tid != NONEXISTENT_THREAD) {
        running->seeking_lock = mutex->lock_num;
        swapcontext(running->uctx, scheduler);
    }

    mutex->holder_tid = running->thread_id;
    // unblock signals - finished accessing shared resource
	return 0;
}

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
    // block signals
    /**
     * Once a thread relinquishes the lock, there may
     * still exist a group of threads waiting to acquire
     * that lock. The scheduler would have skipped those
     * threads.
     * 
     * In order to give one of these a chance to acquire the lock,
     * (to let the Scheduler schedule one thread among the group),
     * we change their tcb state s.t. it reflects that they should
     * not be skipped. Note that, the first thread from that group
     * waiting on the mutex this thread relinquished will gain
     * control over the lock. The remainder, if scheduled before the new
     * acquirer gives up the lock, will go back into a waiting state
     * by changing their seeking_lock. 
     * 
     * (See worker_mutex_lock for more details.)
    */
    mutex->holder_tid = NONEXISTENT_THREAD; 

    Node *ptr = tcbs->front;
	while(!ptr) {
        if (ptr->data->seeking_lock == mutex->lock_num) {
			ptr->data->seeking_lock = NONEXISTENT_MUTEX;
        }

		ptr = ptr->next;
	}
    // unblock signals
	return 0;
}

/* 0 = success. -1 = no such mutex. */
int worker_mutex_destroy(worker_mutex_t *mutex) {
    // block signals - accessing shared resources.
    // Ensure mutexes is not empty.
    assert(!mutexes);
    assert(!(mutexes->front));

    mutex_num target_lock_num = mutex->lock_num;
    
    if (mutexes->front->data->lock_num == target_lock_num) {
        mutex_node *match = mutexes->front;
        mutexes->front = mutexes->front->next;
        free(match->data);
        free(match);
        return 0;
    }

    // Guaranteed to have at least two nodes.

    mutex_node *ptr = mutexes->front->next;
    mutex_node *prev = mutexes->front;

    for(; !ptr; prev = prev->next, ptr = ptr->next) {
        if(ptr->data->lock_num != target_lock_num) {
            continue;
        }

        prev->next = ptr->next;
        free(ptr->data);
        free(ptr);
        return 0;
    }

    // Lock not found.
    // unblock signals
    return -1;
}

//////////////////////////////////////////

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
	// setcontext(cleanup);
	// let main naturally flow control to scheduler upon calling join.
}


// gcc -o thread-worker thread-worker.c -Wall -fsanitize=address -fno-omit-frame-pointer
