#include <stdio.h>
#include <stdlib.h>
#include "support.h"

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


typedef struct mutex_node {
    worker_mutex_t *data;
    struct mutex_node *next;
} mutex_node;

/* Singular Linked List */
typedef struct mutex_list {
    struct mutex_node *front;
} mutex_list;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
////////////////////////////////////////////////////////////////////////////////////////////
static Queue *q_arrival;
static Queue *q_scheduled;
static List *tcbs;

static ucontext_t *scheduler;
/**
 * The cleanup context is used to reap terminated worker_threads.
 * When a worker thread is created, its uc_link points to cleanup.
 * - As such, upon terminating, control flows to the cleanup context.
*/
static ucontext_t *cleanup;

static worker_t *last_created_worker_tid;
static mutex_num *current_mutex_num;
static mutex_list *mutexes; // list of (mutex_num, holder_tid) for currently init-d mutexes.

/**
 * running points to the currently executing thread.
 * If running is NULL: the previous running thread was cleaned up by ucontext cleanup.
*/
static tcb *running; // Currently executing thread.

////////////////////////////////////////////////////////////////////////////////////////////

void print_mutex_list(mutex_list *mutexes) {
    mutex_node *ptr = mutexes->front;
    printf("Printing lst: \n");
    
    while(ptr) {
        printf("%d held by %d.\n", ptr->data->lock_num, ptr->data->holder_tid);
        ptr = ptr->next;
    }

    printf("\n");
}

/* Registered with atexit() upon library's first use invocation. */
void cleanup_library() {
	/* Remove main from scheduled queue and tcbs list.*/
	running = dequeue(q_scheduled);	// Empties scheduled list since last thread.
	remove_from(tcbs, MAIN_THREAD); // Empties tcbs list since last thread.
	free(running->uctx);
	free(running);

	/* Free all allocated library mechanisms. */
	assert(isEmpty(q_arrival));
	free(q_arrival);
	assert(isEmpty(q_scheduled));
	free(q_scheduled);

	assert(isEmptyList(tcbs));
	free(tcbs);

	free(last_created_worker_tid);
	free(current_mutex_num);
	free(mutexes);

	free(scheduler->uc_stack.ss_sp);
	free(scheduler);
	free(cleanup->uc_stack.ss_sp);
	free(cleanup);
}

/* returns 1 if [thread_id] is waiting on a thread, 0 otherwise. */
int is_waiting_on_a_thread(tcb *thread) {
	return thread->join_tid != NONEXISTENT_THREAD;
}

/* returns 1 if [thread_id] is waiting on a lock, 0 otherwise. */
int is_waiting_on_a_mutex(tcb *thread) {
	return thread->seeking_lock != NONEXISTENT_MUTEX;
}

/* returns 1 if thread is blocked, 0 otherwise. */
int is_blocked(tcb *thread) {
	return is_waiting_on_a_thread(thread) || is_waiting_on_a_mutex(thread);
}

void round_robin_scheduler() {
	while(1) {
		printf("INFO[schedule 1]: entered scheduler loop. Scheduled Queue: "); print_queue(q_scheduled);

		// Insert newly arrived jobs into schedule queue.
		while(!isEmpty(q_arrival)) {
			print_queue(q_arrival); print_queue(q_scheduled);
			enqueue(q_scheduled, dequeue(q_arrival));
		}

		/* Scheduler logic: */
		do {
			if (running != NULL) { // dont enqueue terminated job freed up by cleanup context.
				print_queue(q_scheduled);
				enqueue(q_scheduled, running);
			}

			running = dequeue(q_scheduled);

			print_queue(q_scheduled);
			if(is_blocked(running)) 
				printf("INFO[schedule 4]: skipped tid (%d) BCUZ BLOCKED\n", running->thread_id);

		} while(is_blocked(running));
		
		printf("INFO[schedule 5]: Scheduling tid (%d)\n", running->thread_id);
		/**
		 * Only start timer IFF tcb's list size is greater than 1 (> 1).
		 * This way, we do not context switch out of main if there is no point to doing so.
		*/
		swapcontext(scheduler, running->uctx);
	}
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

void clean_exited_worker_thread() {
	while(1) {
		worker_t tid_ended = running->thread_id;
		printf("INFO[CLEANUP]: Cleaned running tid (%d)\n", tid_ended);

		alert_waiting_threads(tid_ended, NULL); // Never overwrite join return value.
		remove_from(tcbs, tid_ended);

		// Allocated Heap space for TCB and TCB->uctx and TCB->uctx->uc_stack base ptr
		free(running->uctx->uc_stack.ss_sp);
		free(running->uctx);
		free(running);

		running = NULL; // IMPORTANT FLAG so scheduler doesn't enqueue cleaned thread.
		swapcontext(cleanup, scheduler);
	}
}

/* Initializes user-level threads library supporting mechanisms. */
void init_library() {
	// Initialize arrival and scheduled queue.
	assert(isUninitialized(q_arrival));
	q_arrival = (Queue *) malloc(sizeof(Queue));
	q_arrival->size = 0;
	q_arrival->rear = NULL;

	assert(isUninitialized(q_scheduled));
	q_scheduled = (Queue *) malloc(sizeof(Queue));
	q_scheduled->size = 0;
	q_scheduled->rear = NULL;
	
	// Initialize the tcbs list. 
	assert(isUninitializedList(tcbs));
	tcbs = (List *) malloc(sizeof(List));
	tcbs->size = 0;
	tcbs->front = NULL;

	// Scheduler and Cleanup should be referenced from any context (hence heap).
	scheduler = (ucontext_t *) malloc(sizeof(ucontext_t));
	cleanup = (ucontext_t *) malloc(sizeof(ucontext_t));

	// Create scheduler context
	getcontext(scheduler);
	scheduler->uc_link = NULL; // no longer cleanup. see atexit registered function.
	scheduler->uc_stack.ss_size = 4096;
	scheduler->uc_stack.ss_sp = malloc(4096);
	makecontext(scheduler, round_robin_scheduler, 0);

	// Create cleanup context
	getcontext(cleanup);
	cleanup->uc_link = NULL;
	cleanup->uc_stack.ss_size = 4096;
	cleanup->uc_stack.ss_sp = malloc(4096);
	makecontext(cleanup, clean_exited_worker_thread, 0);

	// worker_create should associate a new thread with an increasing worker_t tid.
	last_created_worker_tid = (worker_t *) malloc(sizeof(worker_t));
	*last_created_worker_tid = MAIN_THREAD;

	// preserve distinctness of mutex numbers - also monotonically increasing.
	current_mutex_num = (mutex_num *) malloc(sizeof(mutex_num));
	*current_mutex_num = INITIAL_MUTEX;
	mutexes = (mutex_list *) malloc(sizeof(mutex_list));

	// Register atexit() function to clean up supporting mechanisms.
	if (atexit(cleanup_library) != 0) {
		printf("Will be unable to free user level threads library mechanisms.\n");
	}

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
	new_tcb->thread_id = ++(*last_created_worker_tid);
	new_tcb->join_tid = NONEXISTENT_THREAD; // not waiting on any thread
	new_tcb->join_retval = NULL;
	new_tcb->seeking_lock = NONEXISTENT_MUTEX; // not waiting on any lock

	new_tcb->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(new_tcb->uctx); // heap space stores context
	new_tcb->uctx->uc_link = cleanup; // all workers must flow into cleanup
	new_tcb->uctx->uc_stack.ss_size = 4096;
	new_tcb->uctx->uc_stack.ss_sp = malloc(4096);
	makecontext(new_tcb->uctx, (void *) function, 1, arg); 

	// Put newly created tcb into structures used by the library.
	enqueue(q_arrival, new_tcb);
	insert(tcbs, new_tcb);
	
	printf("INFO[worker_create]: enqueued new worker into arrival queue: ");
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
	// ITS TIME TO REPLACE THIS WITH JOIN
	//setcontext(cleanup);
	// let main naturally flow control to scheduler upon calling join.
}
