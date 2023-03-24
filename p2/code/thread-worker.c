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
static unsigned int yieldsS = 0;
static unsigned int preemptions = 0;
static ucontext_t *scheduler, *cleanup;

/* Monotonically increasing counters: */
static atomic_uint last_created_worker_tid = ATOMIC_VAR_INIT(MAIN_THREAD); 
static atomic_uint current_mutex_num = ATOMIC_VAR_INIT(0);

static Queue *q_arrival;
static List *tcbs, *ended_tcbs;

/* MLFQ support variables */
static Queue **all_queue;
static unsigned int total_quantums_elapsed = 0;
const static unsigned int S_value_decay_usage_const = QUEUE_LEVELS * 16;

/* Currently Executing thread. running is NULL if reaped.*/
static tcb *running;

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
	new_tcb->thread_id = *thread = (atomic_fetch_add(&last_created_worker_tid, 1) + 1);
	new_tcb->ret_value = NULL;
	new_tcb->join_tid = NONEXISTENT_THREAD; // not waiting on any thread
	new_tcb->join_retval = NULL;
	
	new_tcb->quantums_elapsed = 0;
	new_tcb->quantum_amt_used = 0;
	new_tcb->time_quantum_used_up_fully = 0;
	new_tcb->priority_level = 0;
	new_tcb->previously_scheduled = 0;

	new_tcb->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(new_tcb->uctx); // heap space stores context
	new_tcb->uctx->uc_link = cleanup; // all workers must flow into cleanup
	new_tcb->uctx->uc_stack.ss_size = STACK_SIZE;
	new_tcb->uctx->uc_stack.ss_sp = malloc(STACK_SIZE);
	new_tcb->uctx->uc_sigmask = running->uctx->uc_sigmask;
	makecontext(new_tcb->uctx, (void *) function, 1, arg); 

	clock_gettime(CLOCK_MONOTONIC, &new_tcb->arrival);

	// Synchronize access to shared resource ended_tcbs list
	sigset_t block_prof_set = sigset_init();
	sigset_t oldset;
	sigprocmask(SIG_BLOCK, &block_prof_set, &oldset);
	
	enqueue(q_arrival, new_tcb);

	// Completed using shared resource.
	sigprocmask(SIG_SETMASK, &oldset, NULL);	

	return 0;
}

int worker_yield() {
	struct timespec curr_time;
	clock_gettime(CLOCK_MONOTONIC, &curr_time);

	const double num_us_in_sec = 1000000;
	const double num_ns_in_us = 1000;

	running->quantum_amt_used += 
		(curr_time.tv_sec - running->time_of_last_scheduling.tv_sec) * num_us_in_sec + 
		(curr_time.tv_nsec - running->time_of_last_scheduling.tv_nsec) / num_ns_in_us;
	
	if (running->quantum_amt_used >= TIME_QUANTUM) {
		running->time_quantum_used_up_fully = 1;
		running->quantum_amt_used = 0;

		running->quantums_elapsed += 1;
		total_quantums_elapsed += 1; // Only used in MLFQ.
	}

	return swapcontext(running->uctx, scheduler);
}

void worker_exit(void *value_ptr) {
	running->ret_value = value_ptr;
}

int worker_join(worker_t thread, void **value_ptr) {
	// Synchronize access to shared resource ended_tcbs list
	sigset_t block_prof_set = sigset_init();
	sigset_t oldset;
	sigprocmask(SIG_BLOCK, &block_prof_set, &oldset);
	
	tcb *waiting_on_tcb_already_ended = contains(ended_tcbs, thread);
	
	// Completed using shared resource.
	sigprocmask(SIG_SETMASK, &oldset, NULL);

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

	assert(mutex);
	mutex->mutex_num = atomic_fetch_add(&current_mutex_num, 1) + 1;
	atomic_flag_clear(&(mutex->is_acquired));
    return 0;
}

int worker_mutex_lock(worker_mutex_t *mutex) {
	// If there exists some worker with lesser used time quantum, yield.
	// for (Node *ptr = tcbs->front; ptr; ptr = ptr->next) {
	// 	if (running->quantums_elapsed < ptr->data->quantums_elapsed) {
	// 		++yieldsS;
	// 		worker_yield();
	// 		break;
	// 	}
	// }
    
	/* Attempt to acquire lock. */
	while(atomic_flag_test_and_set(&(mutex->is_acquired))) {
		++yieldsS;
		worker_yield();
	}

	return 0;
}

int worker_mutex_unlock(worker_mutex_t *mutex) {	
	atomic_flag_clear(&(mutex->is_acquired));	
	return 0;
}

int worker_mutex_destroy(worker_mutex_t *mutex) {
	worker_mutex_unlock(mutex);
	mutex->mutex_num = NONEXISTENT_MUTEX;
	atomic_flag_clear_explicit(&(mutex->is_acquired), __ATOMIC_SEQ_CST);
}

/* scheduler */
static void schedule() {
	#ifndef MLFQ
		sched_psjf();
	#else 
		insert_by_usage(tcbs, running);
		sched_mlfq();
	#endif
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	while(1) {
		++tot_cntx_switches;

		// Handle new arrivals.
		while(!isEmpty(q_arrival)) {
			tcb *new_arrival = dequeue(q_arrival);
			insert_at_front(tcbs, new_arrival);
		}

		if (running) { 	// Enqueue if worker didn't get cleaned.
			// put the worker into appropriate position (ascending by runtime)
			insert_by_usage(tcbs, running);
		}
		
		// Find next worker with lowest runtime so far.
		running = remove_from(tcbs, find_first_unblocked_thread(tcbs));

		// Perform setup before scheduling next worker:
		if (!running->previously_scheduled) {
			// Being scheduled for the first time. Used in Response time metric.
			clock_gettime(CLOCK_MONOTONIC, &running->first_scheduled);
			running->previously_scheduled = 1;
		}
		
		set_timer(running->quantum_amt_used);

		clock_gettime(CLOCK_MONOTONIC, &running->time_of_last_scheduling);
		++tot_cntx_switches; 
		swapcontext(scheduler, running->uctx);
	}
}

/* This functions boosts all tcb to top and also resets all scheduler attributes*/
void boost_all_queues(){
	int curr_level = 0;
	if (QUEUE_LEVELS == 1) {
		return;
	}

	for(curr_level = 1; curr_level < QUEUE_LEVELS; ++curr_level){
		while(!isEmpty(all_queue[curr_level])){
			//Get current tcb to reset attributes
			tcb *curr_tcb = dequeue(all_queue[curr_level]);

			//resets all information back to default
			curr_tcb->quantum_amt_used = 0;
			curr_tcb->time_quantum_used_up_fully = 0;
			curr_tcb->priority_level = 0;

			//add it back into top priority queue
			enqueue(all_queue[0],curr_tcb);
		}
	}

	assert(all_queue[0]->size == tcbs->size);
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	while(1) {
		++tot_cntx_switches;

		while(!isEmpty(q_arrival)) {
			tcb *newly_arrived_job = dequeue(q_arrival);
			enqueue(all_queue[0], newly_arrived_job); //adds all arrived tcb to the highest priority
			insert_at_front(tcbs, newly_arrived_job);
		}

		// add current running in the right priority level queue
		if (running != NULL) { // dont enqueue terminated job freed up by cleanup context.
			if (running->time_quantum_used_up_fully) { // Preempted because used up time quantum
				running->time_quantum_used_up_fully = 0;
				running->quantum_amt_used = 0;
				running->priority_level = (running->priority_level == (QUEUE_LEVELS - 1)) 
					? running->priority_level : running->priority_level + 1;
			}
			enqueue(all_queue[running->priority_level], running);
			running = NULL;
		}

		// Boost queues. 
		if (total_quantums_elapsed >= S_value_decay_usage_const) {
			total_quantums_elapsed %= S_value_decay_usage_const;
			boost_all_queues();
		}

		// Choose highest priority unblocked worker to schedule next.
		for(int curr_level = 0; running == NULL && curr_level < QUEUE_LEVELS; ++curr_level) {
			int threads_at_this_priority = all_queue[curr_level]->size;

			while (threads_at_this_priority) {
				tcb *candidate = dequeue(all_queue[curr_level]);
			
				if (!is_blocked(candidate)) {
					running = candidate;
					break;
				}

				enqueue(all_queue[curr_level], candidate);
				--threads_at_this_priority;
			}                         
		}

		assert(running);

		// Perform setup before scheduling next worker:
		if (!running->previously_scheduled) {
			// Being scheduled for the first time. Used in Response time metric.
			clock_gettime(CLOCK_MONOTONIC, &running->first_scheduled);
			running->previously_scheduled = 1;
		}

		set_timer(running->quantum_amt_used);

		clock_gettime(CLOCK_MONOTONIC, &running->time_of_last_scheduling);
		++tot_cntx_switches; 
		swapcontext(scheduler, running->uctx);
	}
}

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {
	print_list(ended_tcbs);
	printf("Yields %d | Preemptions %d | Total Time Qs: %d\n", yieldsS, preemptions, total_quantums_elapsed);
       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


/* Supporting Functions */
/* ==================== */

void recompute_benchmarks() {
	assert(ended_tcbs->size);
	assert(running);

	const double num_us_in_sec = 1000000;
	const double num_ns_in_us = 1000;

	const double runtime = 
			(running->completion.tv_sec - running->first_scheduled.tv_sec) * num_us_in_sec + 
			(running->completion.tv_nsec - running->first_scheduled.tv_nsec) / num_ns_in_us;

	const double turnaround_time = 
		(running->completion.tv_sec - running->arrival.tv_sec) * num_us_in_sec + 
		(running->completion.tv_nsec - running->arrival.tv_nsec) / num_ns_in_us;

	const double response_time = 
		(running->first_scheduled.tv_sec - running->arrival.tv_sec) * num_us_in_sec + 
		(running->first_scheduled.tv_nsec - running->arrival.tv_nsec) / num_ns_in_us;

	printf("Ended tid (%d) had %d quantums |  runtime: [%f] | TT:[%f] | RespT:[%f]\n", 
	running->thread_id, running->quantums_elapsed, runtime, turnaround_time, response_time);

	double size_matters = (double) ended_tcbs->size;
	avg_turn_time = (avg_turn_time * (size_matters - 1) + turnaround_time) / size_matters;
	avg_resp_time = (avg_resp_time * (size_matters - 1) + response_time) / size_matters;
}

/* One shot timer that will send SIGPROF signal after TIME_QUANTUM -remaining microseconds. */
int set_timer(int remaining) {
	timer->it_interval.tv_sec = 0;
	timer->it_interval.tv_usec = 0;

	timer->it_value.tv_sec = 0;
	timer->it_value.tv_usec = TIME_QUANTUM;

	return setitimer(ITIMER_PROF, timer, NULL);
}

/* Swaps the context to scheduler after a SIGPROF signal. */
void timer_signal_handler(int signum) {
	running->time_quantum_used_up_fully = 1;
	running->quantums_elapsed += 1;
	total_quantums_elapsed += 1; // Only used in MLFQ
	running->quantum_amt_used = 0;
	++preemptions;
	swapcontext(running->uctx, scheduler);
}


/* Registers up sigaction on SIGPROF signal to call signal_handler() */
void register_handler() {
	memset(sa, 0, sizeof(*sa));
	sa->sa_handler = &timer_signal_handler;
	
	sigemptyset(&(sa->sa_mask));
	sa->sa_flags = 0;

	if (sigaction(SIGPROF, sa, NULL) != 0) {
		perror("Couldn't register signal handler properly.");
	}
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
	scheduler->uc_stack.ss_size = STACK_SIZE;
	scheduler->uc_stack.ss_sp = malloc(STACK_SIZE);
	scheduler->uc_sigmask = sigset_init();
	makecontext(scheduler, schedule, 0);

	// Create cleanup context
	getcontext(cleanup);
	cleanup->uc_link = NULL;
	cleanup->uc_stack.ss_size = STACK_SIZE;
	cleanup->uc_stack.ss_sp = malloc(STACK_SIZE);
	cleanup->uc_sigmask = sigset_init();
	makecontext(cleanup, clean_exited_worker_thread, 0);

	#ifndef PSJF
		// Initialize queue** that holds all priority queue
		all_queue = (Queue **) malloc(QUEUE_LEVELS * sizeof(Queue *));
		int level;
		for(level = 0; level < QUEUE_LEVELS; ++level){
			all_queue[level] = (Queue*) malloc(sizeof(Queue));
			all_queue[level]->size = 0;
			all_queue[level]->rear = NULL;
		}
	#endif

	// Create tcb for main
	running = (tcb *) malloc(sizeof(tcb));
	running->uctx = (ucontext_t *) malloc(sizeof(ucontext_t));
	running->thread_id = MAIN_THREAD;
	running->join_tid = NONEXISTENT_THREAD;
	running->quantums_elapsed = 0;
	running->quantum_amt_used = 0;
	running->time_quantum_used_up_fully = 0;
	running->priority_level = 0;
	running->previously_scheduled = 0;
	getcontext(running->uctx);

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

	// PSJF removed MAIN because tcbs list functions as a queue.
	#ifndef PSJF
		remove_from(tcbs, MAIN_THREAD); // Empties tcbs list since last thread.
	#endif

	free(running->uctx);
	free(running);
	
	/* Free all allocated library mechanisms. */
	assert(isEmpty(q_arrival));
	free(q_arrival);

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

	#ifndef PSJF
		int level;
		for (level = 0; level < QUEUE_LEVELS; ++level) {
			free(all_queue[level]);
		}
		free(all_queue);
	#endif

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
		clock_gettime(CLOCK_MONOTONIC, &running->completion);
		
		// PSJF would have removed, but MLFQ wouldn't have.
		if (contains(tcbs, running->thread_id)) {
			remove_from(tcbs, running->thread_id);
		}
		
		insert_at_front(ended_tcbs, running);
		recompute_benchmarks();  // Recompute avg tt/rr times given that worker ended.

		alert_waiting_threads(running->thread_id, running->ret_value); 

		running = NULL; // IMPORTANT FLAG for scheduler;
		swapcontext(cleanup, scheduler);
	}
}


/* returns 1 if [thread_id] is waiting on a thread, 0 otherwise. */
int is_waiting_on_a_thread(tcb *thread) {
	return thread->join_tid != NONEXISTENT_THREAD;
}


/* returns 1 if thread is blocked, 0 otherwise. */
int is_blocked(tcb *thread) {
	return is_waiting_on_a_thread(thread);
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


int isUninitializedList(List *lst_ptr) {
    return !lst_ptr;
}


int isEmptyList(List *lst_ptr) { 
    assert(!isUninitializedList(lst_ptr));
    return lst_ptr->size == 0;
}


/* Returns 1 if to_be_inserted has less usage than curr, 0 otherwise. */
int compare_usage(tcb *to_be_inserted, tcb *curr) {
	return to_be_inserted->quantums_elapsed < curr->quantums_elapsed
		 || (to_be_inserted->quantums_elapsed == curr->quantums_elapsed &&
		 	to_be_inserted->quantum_amt_used <= curr->quantum_amt_used);
}

/* 1 if sorted in ascending order of usage, 0 otherwise.*/
int sorted_by_usage(List *lst_ptr) {
	assert(lst_ptr);

	Node *ptr, *prev;
	for (ptr = lst_ptr->front->next, prev = lst_ptr->front; ptr; ptr = ptr->next, prev = prev->next) {
		if (!compare_usage(prev->data, ptr->data)) {
			return 0;
		}
	}

	return 1;
}

/* Inserts to front of list. */
void insert_by_usage(List *lst_ptr, tcb *to_be_inserted) {
    assert(!isUninitializedList(lst_ptr));
	assert(!contains(lst_ptr, to_be_inserted->thread_id));

    Node *item = (Node *) malloc(sizeof(Node));
    item->data = to_be_inserted;
	item->next = NULL;

	if (isEmptyList(lst_ptr)) {
		lst_ptr->front = item;
	} else {
		if (compare_usage(to_be_inserted, lst_ptr->front->data)) {
			item->next = lst_ptr->front;
			lst_ptr->front = item;
		} else {
			Node *ptr = lst_ptr->front->next;
			Node *prev = lst_ptr->front;
			int position_found = 0;
			
			for (; ptr; ptr = ptr->next, prev = prev->next) {
				if (compare_usage(to_be_inserted, ptr->data)) {
					prev->next = item;
					item->next = ptr;
					position_found = 1;
					break;
				}
			}

			if (!position_found) { 
				prev->next = item; // appended to list.
			}
		}
	}

    ++(lst_ptr->size);

	assert(sorted_by_usage(lst_ptr));
	assert(contains(lst_ptr, to_be_inserted->thread_id));
}


void insert_at_front(List *lst_ptr, tcb *to_be_inserted) {
	assert(!isUninitializedList(lst_ptr));

    Node *item = (Node *) malloc(sizeof(Node));
    item->data = to_be_inserted;
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


worker_t find_first_unblocked_thread(List *lst_ptr) {
	Node *ptr;

	// Traverse ordered list.
	for (ptr = lst_ptr->front; ptr; ptr = ptr->next) {
		if (!is_blocked(ptr->data)) {
			return ptr->data->thread_id;
		}
	}

	return NONEXISTENT_THREAD;
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

