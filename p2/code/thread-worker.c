#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>

typedef unsigned int worker_t;
typedef unsigned int mutex_num;

/* mutex struct definition */
typedef unsigned int worker_mutex_t;

typedef struct TCB {
	worker_t 		thread_id;
	ucontext_t 		*uctx;
	void			*ret_value;
	worker_t		join_tid;		/* NONEXISTENT_THREAD if not currently waiting on another thread. */
	void **			join_retval;	
	worker_mutex_t	seeking_lock;	/* NONEXISTENT_MUTEX if not seeking to acquire a mutex. */
} tcb; 

typedef struct Node {
    tcb *data;
    struct Node *next;
} Node;

/* Singular Linked List */
typedef struct List {
    unsigned short size;
    struct Node *front;
} List;

/* Circular Linked List */
typedef struct Queue {
    unsigned short size;
    struct Node *rear;
} Queue;

typedef struct mutex_node {
    worker_mutex_t lock_num;
	worker_t holder_tid;
    struct mutex_node *next;
} mutex_node;

/* Singular Linked List */
typedef struct mutex_list {
    struct mutex_node *front;
} mutex_list;

/* Function Declarations: */

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

/* Returns valid ptr if the list contains the worker, else returns NULL.*/
tcb* contains(List *lst_ptr, worker_t target) {
    Node *ptr = lst_ptr->front;
    while(ptr) {
        if (ptr->data->thread_id == target) {
            return ptr->data;
        }
        ptr = ptr->next;
    }

    return NULL;
}

tcb* remove_from(List *lst_ptr, worker_t target) {
    assert(!isEmptyList(lst_ptr));
    assert(contains(lst_ptr, target));

    Node *front = lst_ptr->front;
    tcb *target_tcb;

    if (front->data->thread_id == target) {
        target_tcb = front->data;
        lst_ptr->front = front->next;
        --(lst_ptr->size);

        free(front);
        return target_tcb;
    }

    Node *ptr = front->next;
    Node *prev = front;

    for (; ptr; prev = prev->next, ptr = ptr->next) {
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

void print_mutex_list(mutex_list *mutexes) {
    mutex_node *ptr = mutexes->front;
    printf("Printing lst of mutexes: \n");
    
    while(ptr) {
        printf("%d held by %d.\n", ptr->lock_num, ptr->holder_tid);
        ptr = ptr->next;
    }

    printf("\n");
}

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
 * The seeking_lock attribute of a tcb is used by the scheduler to indicatecurrent_mutex_num
 * whether the thread should be skipped in light of the thread waiting
 * for another thread to unlock a mutex.
*/
#define NONEXISTENT_MUTEX 0

// INITAILIZE ALL YOUR OTHER VARIABLES HERE
////////////////////////////////////////////////////////////////////////////////////////////
static Queue *q_arrival, *q_scheduled;
static List *tcbs, *ended_tcbs;

static ucontext_t *scheduler;
/**
 * The cleanup context is used to reap terminated worker_threads.
 * When a worker thread is created, its uc_link points to cleanup.
 * - As such, upon terminating, control flows to the cleanup context.
*/
static ucontext_t *cleanup;

static worker_t *last_created_worker_tid;
static worker_mutex_t *current_mutex_num;
static mutex_list *mutexes; // list of (mutex_num, holder_tid) for currently init-d mutexes.

/**
 * running points to the currently executing thread.
 * If running is NULL: the previous running thread was cleaned up by ucontext cleanup.
*/
static tcb *running; // Currently executing thread.

////////////////////////////////////////////////////////////////////////////////////////////

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

/**
 * There may be a group of threads waiting for (joined on) the current thread
 * to terminate. Hence, when the current thread does terminate, we ensure that
 * the tcb structures of the threads waiting on this one are updated to reflect
 * the fact that they are ready to be scheduled.
 * 
 * When tcb attribute join_tid is equal to NONEXISTENT thread, a thread
 * is not waiting on any other thread to complete. Accordingly, it is not
 * blocked and won't be skipped by the scheduler. If we fail to update
 * the waiting thread's tcb join_tid attribute to NONEXISTENT, then the 
 * scheduler will skip it in perpetuity. This function aids the worker_exit
 * call to inform the scheduler (indirectly) to now no longer skip
 * the threads waiting on it.
*/
void alert_waiting_threads(worker_t ended, void *ended_retval_ptr) {
	Node *ptr = tcbs->front;

	// Notify all threads who called worker_join(ended, retval).
	while(ptr) {
		if (ptr->data->join_tid == ended) {
			ptr->data->join_tid = NONEXISTENT_THREAD; // No longer waiting.
			printf("\n\n[ALERT]: %d alerted that %d ended.\n\n", ptr->data->thread_id, running->thread_id);

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

/** 
 * This function is invoked when scheduler context first runs.
 * When main creates a thread, the library's associated mechanisms are initialized;
 * when main later waits on a thread (via a call to worker_join), the scheduler 
 * is afforded the chance to execute this function for the first time.
*/
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
		
		printf("after descheduling: ");
		print_queue(q_scheduled);
		printf("INFO[schedule 5]: Scheduling tid (%d)\n", running->thread_id);
		/**
		 * Only start timer IFF tcb's list size is greater than 1 (> 1).
		 * This way, we do not context switch out of main if there is no point to doing so.
		*/
		swapcontext(scheduler, running->uctx);
	}
}

/**
 * This function invoked when cleanup context first runs. 
 * Ending workers pass control to cleanup context. See uc_link of created workers.
 */
void clean_exited_worker_thread() {
	while(1) {
		insert(ended_tcbs, remove_from(tcbs, running->thread_id));

		/* The following alert call is necessary if worker_thread forgot to call worker_exit().*/
		alert_waiting_threads(running->thread_id, NULL); // Never overwrite join return value. 
	
		running = NULL; // IMPORTANT FLAG so scheduler doesn't enqueue cleaned thread.
		swapcontext(cleanup, scheduler);
	}
}

/* Registered with atexit() upon library's first use invocation. */
void cleanup_library() {
	/* Remove main from scheduled queue and tcbs list.*/
	// main already removed lol //running = dequeue(q_scheduled);	// Empties scheduled list since last thread.
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

	// free each tcb in ended_tcbs which preserves ended thread's retvals (join uses this).
	while (ended_tcbs->front) {
		tcb *temp = ended_tcbs->front->data;
		ended_tcbs->front = ended_tcbs->front->next;
		--(ended_tcbs->size);
		free(temp->uctx->uc_stack.ss_sp);
		free(temp->uctx);
		free(temp);
	}

	assert(isEmptyList(ended_tcbs));
	free(ended_tcbs);

	free(last_created_worker_tid);
	free(current_mutex_num);

	assert(!(mutexes->front));
	free(mutexes);

	free(scheduler->uc_stack.ss_sp);
	free(scheduler);
	free(cleanup->uc_stack.ss_sp);
	free(cleanup);

	printf("INFO[ATEXIT]: library cleaned.");
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

	// Initialize the ended_tcbs list (used for joins)
	assert(isUninitializedList(ended_tcbs));
	ended_tcbs = (List *) malloc(sizeof(List));
	ended_tcbs->size = 0;
	ended_tcbs->front = NULL;

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

	// Create tcb for main
	running = (tcb *) malloc(sizeof(tcb));
	running->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	running->thread_id = MAIN_THREAD;
	getcontext(running->uctx);

	insert(tcbs, running);

	// Register atexit() function to clean up supporting mechanisms.
	if (atexit(cleanup_library) != 0) {
		printf("Will be unable to free user level threads library mechanisms.\n");
	}
}

int worker_create(worker_t * thread, void*(*function)(void*), void * arg)
{
	// block signals - accessing shared resource: tcb list and queues.
	if (!scheduler) { // first time library called:
		init_library();
	}

	// Create tcb for new_worker and put into q_arrival queue
	tcb* new_tcb = (tcb *) malloc(sizeof(tcb));
	new_tcb->thread_id = ++(*last_created_worker_tid);
	*thread = new_tcb->thread_id;
	new_tcb->ret_value = NULL;
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

	// unblock signals.
	return 0;
};

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield() {
	return swapcontext(running->uctx, scheduler);
}

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// block signals - accessing shared tcb list in alert.
	running->ret_value = value_ptr;
	alert_waiting_threads(running->thread_id, value_ptr); 
	// unblock signals - finished accessing shared tcb list. (redundent since context ends)
	// Transfer then flows to cleanup context.
}

/* wait for thread termination */
int worker_join(worker_t child_thread, void **value_ptr) {
	// block signals
	tcb *waiting_on_tcb_already_ended = contains(ended_tcbs, child_thread);
	/* One cannot make assumptions about the scheduler, and how it would
	have interleaved the execution of child_thread after call to create() but before
	to join. Namely, the child_thread could have already completed before the caller
	enters this function.*/
	if (waiting_on_tcb_already_ended) {
		printf("INFO[worker_join]: %d is already done. so %d won't wait on it.\n", 
		child_thread, running->thread_id);

		if (value_ptr != NULL) {
			*value_ptr = waiting_on_tcb_already_ended->ret_value;
		}
	} else {
		printf("INFO[worker_join]: %d is going to wait on %d\n", running->thread_id, child_thread);
		running->join_tid = child_thread;
		running->join_retval = value_ptr; // alert function will modify this. 
	}

	swapcontext(running->uctx, scheduler);
	// unblock signals
	return 0;
}

int worker_mutex_init(worker_mutex_t *new_mutex) {
    // block signals (we will access shared mutex list).
	if (!scheduler) { // first time library called:
		init_library();
	}
    
	assert(mutexes);

    // Create mutex.
	*new_mutex = (*current_mutex_num)++;

    // Insert created mutex
    mutex_node *mutex_item = (mutex_node *) malloc(sizeof(mutex_node));
    mutex_item->lock_num = *new_mutex;
	mutex_item->holder_tid = NONEXISTENT_THREAD;
    mutex_item->next = mutexes->front;
    mutexes->front = mutex_item;

	printf("Printing mutexes: ");
	print_mutex_list(mutexes);

    // unblock signals.
    return 0;
}

/* Returns NULL if target is not a currently initialized mutex, mutex_node* otherwise. */
mutex_node* fetch_from_mutex_list(worker_mutex_t target) {
	assert(mutexes);

	mutex_node *ptr = mutexes->front;
	while (ptr) {
		if (ptr->lock_num == target) {
			return ptr;
		}
		ptr = ptr->next;
	}

	return NULL;
}

int is_held_by(worker_t this_tid, worker_mutex_t target) {
	mutex_node *node_containing_mutex = fetch_from_mutex_list(target);
	assert(node_containing_mutex); // can't hold a mutex which doesn't exist.

	return node_containing_mutex->holder_tid == this_tid;
}

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
    while(!is_held_by(NONEXISTENT_THREAD, *mutex)) {
		running->seeking_lock = *mutex;
		swapcontext(running->uctx, scheduler);
	}

	// now mutex not held by any thread so acquire.
	mutex_node *m = fetch_from_mutex_list(*mutex);
	assert(m); // can't acquire an invalid node	
	m->holder_tid = running->thread_id; // u can make this atomic. 

    // unblock signals - finished accessing shared resource
	return 0;
}

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
void broadcast_lock_release(worker_mutex_t mutex) {
    Node *ptr = tcbs->front;
	while(ptr) {
		worker_mutex_t *seeking = &(ptr->data->seeking_lock);
		*seeking = (*seeking == mutex) ? NONEXISTENT_MUTEX : (*seeking);

		ptr = ptr->next;
	}
}

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
    // block signals    
	
	// Can't unlock mutex when caller does not have a lock over it.
	assert(is_held_by(running->thread_id, *mutex));

	// Release the lock.
	(fetch_from_mutex_list(*mutex))->holder_tid = NONEXISTENT_THREAD;
	broadcast_lock_release(*mutex);

    // unblock signals
	return 0;
}

/* 0 = success. -1 = no such mutex. */
int worker_mutex_destroy(worker_mutex_t *mutex_to_destroy) {
    // block signals - accessing shared resources.
    // Ensure mutexes is not empty.
    assert(mutexes);

	// If the mutex is held by some thread, unlock it first.
	if (!is_held_by(NONEXISTENT_THREAD, *mutex_to_destroy)) {
		worker_mutex_unlock(mutex_to_destroy);
	}

	// Proceed to destroy unlocked mutex.
	mutex_node *front = mutexes->front;
    assert(front);

	print_mutex_list(mutexes);
	printf("Seeking to remove target wiht lock num: (%d) \n", *mutex_to_destroy);

    if (front->lock_num == *mutex_to_destroy) {
        mutexes->front = front->next;
        free(front);
        return 0;
    }

    // Guaranteed to have at least two nodes.

    mutex_node *ptr = front->next;
    mutex_node *prev = front;

    for(; ptr; prev = prev->next, ptr = ptr->next) {
        if(ptr->lock_num == *mutex_to_destroy) {  
			prev->next = ptr->next;
			free(ptr);

			print_mutex_list(mutexes);
			return 0;
        }
    }

    // Lock not found.
    // unblock signals
    return -1;
}

//////////////////////////////////////////

worker_mutex_t mut1;

void mtest0() {
	printf("calling create mutex");
	worker_mutex_init(&mut1);

	printf("Calling destroy on %d \n", mut1);
	worker_mutex_destroy(&mut1);

	printf("Post removal: ");
	print_mutex_list(mutexes);
}

int main(int argc, char **argv) {
	mtest0();
}

/* test1,2,3 test library functions. */

void test1_func(void *) {
	printf("WORKER %d: func_bar ran\n", running->thread_id);
	worker_yield();
	printf("WORKER %d: func_bar ran again\n", running->thread_id);
	worker_exit(NULL);
}

void test1() {
	worker_t worker_1;
	worker_t worker_2;

	worker_create(&worker_1, (void *) &test1_func, NULL);
	worker_create(&worker_2, (void *) &test1_func, NULL);

	worker_join(worker_1, NULL);
	worker_join(worker_2, NULL);
}

// Note that the function signature is different: void *(*function)(void*)
void test2_func(int *x) {
	*x = 1776;
	worker_exit(x);
}

void test2() {
	worker_t worker_1; 
	int *result_1 = malloc(sizeof(int));
	worker_t worker_2; 
	int *result_2 = malloc(sizeof(int));

	// This worker will not be done before main joins on it.
	// It will not be in ended_tcbs until main joins on it.
	worker_create(&worker_1, (void *) &test2_func, result_1);

	// This worker will be done before main joins on it.
	// It will be in ended_tcbs when main joins on it. 
	worker_create(&worker_2, (void *) &test2_func, result_2);

	worker_join(worker_1, (void *) &result_1);
	worker_join(worker_2, (void *) &result_2);

	// Hence this test is about verifying that 
	// data can be safely stored in the aforementioned two interleavings.
	assert(*result_1 == 1776);
	assert(*result_2 == 1776);
}

void test3_func(double *x) {
	*x = 69.420f;
	worker_yield();
	worker_exit(x);
}

void test3() {
	worker_t worker_1; 
	double *result_1 = malloc(sizeof(double));
	
	worker_t worker_2; 
	double*result_2 = malloc(sizeof(double));

	worker_t worker_3; 
	double *result_3 = malloc(sizeof(double));
	// This worker will not be done before main joins on it.
	// It will not be in ended_tcbs until main joins on it.
	worker_create(&worker_1, (void *) &test3_func, result_1);

	// This worker will be done before main joins on it.
	// It will be in ended_tcbs when main joins on it. 
	worker_create(&worker_2, (void *) &test3_func, result_2);
	worker_create(&worker_3, (void *) &test3_func, result_3);

	worker_join(worker_1, (void *) &result_1);
	worker_join(worker_2, (void *) &result_2);
	worker_join(worker_3, (void *) &result_3);

	// Hence this test is about verifying that 
	// data can be safely stored in the aforementioned two interleavings.
	assert(*result_1 == 69.420f);
	assert(*result_2 == 69.420f);
	assert(*result_3 == 69.420f);
}

