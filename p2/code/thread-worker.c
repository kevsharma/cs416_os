// File:	thread-worker.c

/**
 * Project 2: User Level Threads
 * -------------------------------
 * 	Author: Kev Sharma | kks107
 *  Author: Yash Patel | yp315
 * 
 *  Tested on [man.cs.rutgers.edu]
*/

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;

/* Supporting Variables: */
/* ==================== */

static ucontext_t *scheduler, *cleanup;

static worker_t *last_created_worker_tid; /* Monotonically increasing counter. */

static worker_mutex_t *current_mutex_num; /* Monotonically increasing counter. */
static mutex_list *mutexes; /* Currently initialized mutexes. */

static Queue *q_arrival, *q_scheduled;
static List *tcbs, *ended_tcbs;

static tcb *running; /* Currently Executing thread. running is NULL if reaped.*/

/* Preemption mechanisms: */
static struct itimerval *timer;
static struct sigaction *sa;

///////////////////////////////////////////////////////////////////////

int worker_create(worker_t * thread, pthread_attr_t * attr, 
					void *(*function)(void*), void * arg) 
{
	if (!scheduler) { // first time library called:
		init_library();
	}

	// Create tcb for new_worker and put into q_arrival queue
	tcb* new_tcb = (tcb *) malloc(sizeof(tcb));
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

	// Synchronize access to shared resources.
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK, &set, NULL);

	new_tcb->thread_id = ++(*last_created_worker_tid); // shared resource.
	*thread = new_tcb->thread_id;
	
	new_tcb->previously_scheduled = 0;
	clock_gettime(CLOCK_MONOTONIC, &new_tcb->arrival);
	enqueue(q_arrival, new_tcb);
	insert(tcbs, new_tcb);

	// Finished accessing shared resources.
	sigprocmask(SIG_UNBLOCK, &set, NULL);

	return 0;
}

int worker_yield() {
	++tot_cntx_switches;
	return swapcontext(running->uctx, scheduler);
}

void worker_exit(void *value_ptr) {
	// block signals - accessing shared tcb list in alert.
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);

	running->ret_value = value_ptr;
	alert_waiting_threads(running->thread_id, value_ptr); 
	
	// unblock signals - finished accessing shared tcb list.
	sigprocmask(SIG_UNBLOCK,&set, NULL);

	// Transfer then flows to cleanup context.
}

int worker_join(worker_t thread, void **value_ptr) {
	// Synchronize access to shared resource ended_tcbs list
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);
	
	tcb *waiting_on_tcb_already_ended = contains(ended_tcbs, thread);
	
	// Completed using shared resource.
	sigprocmask(SIG_UNBLOCK,&set, NULL);

	/**
	 * Assumptions must not be made about how the scheduler interleaves
	 * execution of threads. Namely, `thread` could have completed before the caller
	 * enters this function.
	*/
	if (waiting_on_tcb_already_ended) {
		if (value_ptr) {
			*value_ptr = waiting_on_tcb_already_ended->ret_value;
		}
	} else {
		running->join_tid = thread;
		running->join_retval = value_ptr; // alert function will modify this. 
		worker_yield();
	}

	return 0;
}

int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
	if (!scheduler) { // first time library called:
		init_library();
	}    
	
	// block signals (we will access shared mutex list).
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);

	assert(mutexes);

	// Write the value back to caller.
	*mutex = (*current_mutex_num)++;

    // Insert created mutex
    mutex_node *mutex_item = (mutex_node *) malloc(sizeof(mutex_node));
    mutex_item->lock_num = *mutex;
	mutex_item->holder_tid = NONEXISTENT_THREAD;
    mutex_item->next = mutexes->front;
    mutexes->front = mutex_item;

    // unblock signals.
	sigprocmask(SIG_UNBLOCK,&set, NULL);
    return 0;
}

int worker_mutex_lock(worker_mutex_t *mutex) {
    // block signals - accesses shared resource mutex.
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);

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
		worker_yield();
	}

	// now mutex not held by any thread so acquire.
	mutex_node *m = fetch_from_mutexes(*mutex);
	assert(m); // can't acquire an invalid node	
	m->holder_tid = running->thread_id; // u can make this atomic. 
    
	// unblock signals - finished accessing shared resource
	sigprocmask(SIG_UNBLOCK,&set, NULL);
	return 0;
}

int worker_mutex_unlock(worker_mutex_t *mutex) {
    // block signals    
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);
	
	// Can't unlock mutex when caller does not have a lock over it.
	assert(is_held_by(running->thread_id, *mutex));

	// Release the lock.
	(fetch_from_mutexes(*mutex))->holder_tid = NONEXISTENT_THREAD;
	int at_least_one_other_thread_unblocked = broadcast_lock_release(*mutex); 

    // unblock signals
	sigprocmask(SIG_UNBLOCK, &set, NULL);

	return 0;
}

int worker_mutex_destroy(worker_mutex_t *mutex) {
    // block signals - accessing shared resources.
	sigset_t set = sigset_init();
	sigprocmask(SIG_SETMASK,&set,NULL);

    // Ensure mutexes is not empty.
    assert(mutexes);

	// If the mutex is held by some thread, unlock it first.
	if (!is_held_by(NONEXISTENT_THREAD, *mutex)) {
		worker_mutex_unlock(mutex);
	}

	// Proceed to destroy unlocked mutex.
	mutex_node *front = mutexes->front;
    assert(front);

    if (front->lock_num == *mutex) {
		*mutex = NONEXISTENT_MUTEX;
        mutexes->front = front->next;
        free(front);
    } else {
		// Guaranteed to have at least two nodes.
		mutex_node *ptr = front->next;
		mutex_node *prev = front;

		for(; ptr; prev = prev->next, ptr = ptr->next) {
			if(ptr->lock_num == *mutex) { 
				*mutex = NONEXISTENT_MUTEX;
				prev->next = ptr->next;
				free(ptr);
				break;
			}
		}
	}

	// Note that lock always initialized due to is_held_by assertion.
	sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}

/* scheduler */
static void schedule() {

	#ifndef MLFQ
		round_robin_scheduler();
		//sched_psjf();
	#else 
		round_robin_scheduler();
		//sched_mlfq();
	#endif
}

/** 
 * Pre-emptive round_robin
 * This function is invoked when scheduler context first runs.
 * When main creates a thread, the library's associated mechanisms are initialized;
 * when main later waits on a thread (via a call to worker_join), the scheduler 
 * is afforded the chance to execute this function for the first time.
*/
static void round_robin_scheduler() {
	while(1) {
		// printf("INFO[schedule 1]: entered scheduler loop. Scheduled Queue: "); print_queue(q_scheduled);

		// Insert newly arrived jobs into schedule queue.
		while(!isEmpty(q_arrival)) {
			// print_queue(q_arrival); print_queue(q_scheduled);
			enqueue(q_scheduled, dequeue(q_arrival));
		}

		/* Scheduler logic: */
		do {
			if (running != NULL) { // dont enqueue terminated job freed up by cleanup context.
				// print_queue(q_scheduled);
				enqueue(q_scheduled, running);
			}

			running = dequeue(q_scheduled);

			// print_queue(q_scheduled);
			// if(is_blocked(running)) 
			// 	printf("INFO[schedule 4]: skipped tid (%d) BCUZ BLOCKED\n", running->thread_id);

		} while(is_blocked(running));
		
		// printf("after descheduling: ");
		// print_queue(q_scheduled);
		// printf("INFO[schedule 5]: remaining threads blocked: (%d) | and current (%d) blocked - (%d)\n",
		// 	remaining_threads_blocked(running), running->thread_id, is_blocked(running));

		if (!running->previously_scheduled) {
			clock_gettime(CLOCK_MONOTONIC, &running->first_scheduled);
			running->previously_scheduled = 1;
		}

		set_timer(!remaining_threads_blocked(running));
		++tot_cntx_switches; 
		// printf("INFO[schedule 5]: Scheduling tid (%d)\n", running->thread_id);
		swapcontext(scheduler, running->uctx);
	}
}


/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}


/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


/* Supporting Functions */
/* ==================== */

void recompute_benchmarks() {
	assert(ended_tcbs->size);
	assert(running);

	const double num_microseconds_in_sec = 1000;
	const double num_ns_in_microseconds = 1000000;

	const double turnaround_time = 
		(running->completion.tv_sec - running->arrival.tv_sec) * num_microseconds_in_sec + 
		(running->completion.tv_nsec - running->arrival.tv_nsec) / num_ns_in_microseconds;

	const double response_time = 
		(running->first_scheduled.tv_sec - running->arrival.tv_sec) * num_microseconds_in_sec + 
		(running->first_scheduled.tv_nsec - running->arrival.tv_nsec) / num_ns_in_microseconds;

	double size_matters = (double) ended_tcbs->size;
	avg_turn_time = (avg_turn_time * (size_matters - 1) + turnaround_time) / size_matters;
	avg_resp_time = (avg_resp_time * (size_matters - 1) + response_time) / size_matters;
}

/* One shot timer that will send SIGPROF signal after TIME_QUANTUM microseconds. */
void set_timer(int to_set) {
	timer->it_interval.tv_sec = 0;
	timer->it_interval.tv_usec = 0;

	timer->it_value.tv_sec = 0;
	timer->it_value.tv_usec = to_set ? TIME_QUANTUM : 0;

	setitimer(ITIMER_PROF, timer, NULL);
}

/* Swaps the context to scheduler after a SIGPROF signal. */
void timer_signal_handler(int signum) {
	// printf("RING RING -> Swapping to scheduler context\n");
	worker_yield();
}


/* Registers up sigaction on SIGPROF signal to call signal_handler() */
void register_handler() {
	memset(sa, 0, sizeof(*sa));
	sa->sa_handler = &timer_signal_handler;
	sigaction(SIGPROF, sa, NULL);
}


/* Initializes and returns sigset for use in sigprocmask(). */
sigset_t sigset_init(){
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set,SIGPROF);
	return set;
} 

/* Initializes user-level threads library supporting mechanisms. */
void init_library() {
	/* Register preemption handler for scheduler + cleanup. */
	timer = (struct itimerval *) malloc(sizeof(struct itimerval));
	sa = (struct sigaction *) malloc(sizeof(struct sigaction));
	register_handler();

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
	scheduler->uc_sigmask = sigset_init();
	makecontext(scheduler, schedule, 0);

	// Create cleanup context
	getcontext(cleanup);
	cleanup->uc_link = NULL;
	cleanup->uc_stack.ss_size = 4096;
	cleanup->uc_stack.ss_sp = malloc(4096);
	cleanup->uc_sigmask = sigset_init();
	makecontext(cleanup, clean_exited_worker_thread, 0);

	// worker_create should associate a new thread with an increasing worker_t tid.
	last_created_worker_tid = (worker_t *) malloc(sizeof(worker_t));
	*last_created_worker_tid = MAIN_THREAD;

	// preserve distinctness of mutex numbers - also monotonically increasing.
	current_mutex_num = (worker_mutex_t *) malloc(sizeof(worker_mutex_t));
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


/* Registered with atexit() upon library's first use invocation. */
void cleanup_library() {
	/* Remove preemption mechanisms */
	free(timer);
	free(sa);

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
}


/**
 * The cleanup context is used to reap terminated worker_threads.
 * When a worker thread is created, its uc_link points to cleanup.
 * - As such, upon terminating, control flows to the cleanup context.
 *
 * This function invoked when cleanup context first runs. 
 * Ending workers pass control to cleanup context. See uc_link of created workers.
 */
void clean_exited_worker_thread() {
	while(1) {
		++tot_cntx_switches; /* Worker ended and context switched to here. */
		
		clock_gettime(CLOCK_MONOTONIC, &running->completion);
		insert(ended_tcbs, remove_from(tcbs, running->thread_id));
		
		// With each thread complete, we compute avg tt/rr times.
		recompute_benchmarks();

		/* The following alert call is necessary if worker_thread forgot to call worker_exit().*/
		alert_waiting_threads(running->thread_id, NULL); // Never overwrite join return value. 
	
		running = NULL; // IMPORTANT FLAG so scheduler doesn't enqueue cleaned thread.
		++tot_cntx_switches;
		swapcontext(cleanup, scheduler);
	}
}


/* Returns 1 if this_tid holds target, 0 otherwise. Aborts if target mutex doesn't exist */
int is_held_by(worker_t this_tid, worker_mutex_t target) {
	mutex_node *node_containing_mutex = fetch_from_mutexes(target);
	assert(node_containing_mutex); // can't hold a mutex which doesn't exist.

	return node_containing_mutex->holder_tid == this_tid;
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

/* Returns 1 if all threads except the one to be scheduled are waiting to join, 0 otherwise. */
int remaining_threads_blocked(tcb *to_be_scheduled) {
	Node *ptr = tcbs->front;
	while (ptr) {
		if (!is_waiting_on_a_thread(ptr->data) && (ptr->data->thread_id != to_be_scheduled->thread_id)) {
			return 0;
		}
		ptr = ptr->next;
	}

	// Since all other threads are blocked, no point in starting a timer
	// to interrupt this one.
	return 1;
}

int broadcast_lock_release(worker_mutex_t mutex) {
	int result = 0;
    Node *ptr = tcbs->front;
	while(ptr) {
		if (ptr->data->seeking_lock == mutex) {
			ptr->data->seeking_lock = NONEXISTENT_MUTEX;
			result = 1;
		}
		ptr = ptr->next;
	}
	return result;
}


void alert_waiting_threads(worker_t ended, void *ended_retval_ptr) {
	Node *ptr = tcbs->front;

	// Notify all threads who called worker_join(ended, retval).
	while(ptr) {
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

/* Supporting Data Structures: */
/* ========================== */

void print_list(List *lst_ptr) {
    Node *ptr = lst_ptr->front;
    printf("Printing lst: ");
    
    while(ptr) {
        printf("%d ", ptr->data->thread_id);
        ptr = ptr->next;
    }

    printf("\n");
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

/* Returns NULL if target is not a currently initialized mutex, mutex_node* otherwise. */
mutex_node* fetch_from_mutexes(worker_mutex_t target) {
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

