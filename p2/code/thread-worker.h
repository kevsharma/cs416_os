// File:	worker_t.h

/**
 * Project 2: User Level Threads
 * -------------------------------
 * 	Author: Kev Sharma | kks107
 *  Author: Yash Patel | yp315
 * 
 *  Tested on [man.cs.rutgers.edu]
*/

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

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
 * The seeking_lock attribute of a tcb is used by the scheduler to indicate current_mutex_num
 * whether the thread should be skipped in light of the thread waiting
 * for another thread to unlock a mutex.
*/
#define NONEXISTENT_MUTEX 0

/**
 * TIME_QUANTUM is the time in ms that a thread will have before context is swapped to scheduler.
*/
#define TIME_QUANTUM 10000

/**
 * STACK_SIZE is the size of the stack that will be allocated for the context
*/
#define STACK_SIZE 16384

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <time.h>

typedef unsigned int worker_t;
typedef unsigned int worker_mutex_t;

typedef struct TCB {
	worker_t 		thread_id;
	ucontext_t 		*uctx;
	void			*ret_value;
	worker_t		join_tid;		/* NONEXISTENT_THREAD if not currently waiting on another thread. */
	void **			join_retval;	
	worker_mutex_t	seeking_lock;	/* NONEXISTENT_MUTEX if not seeking to acquire a mutex. */

    /* Attributes used by Scheduler */
    unsigned int    quantums_elapsed;
    unsigned int    quantum_amt_used;
    struct timespec time_of_last_scheduling;
    int             time_quantum_used_up_fully; /* For use in MLFQ*/

    /* Attributes used for Benchmarks */
    int previously_scheduled;
    struct timespec arrival;
    struct timespec first_scheduled;
    struct timespec completion;
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

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* scheduler */
static void schedule();

/* preemptive temporary rr scheduler. */
static void round_robin_scheduler();

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf();

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq();

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif

/* Helper Function Prototypes:
 * ==========================
 */

void recompute_benchmarks();

/* One shot timer that will send SIGPROF signal after TIME_QUANTUM microseconds. */
void set_timer(int to_set, int remaining);

/* Swaps the context to scheduler after a SIGPROF signal. */
void timer_signal_handler(int signum);

/* Registers up sigaction on SIGPROF signal to call signal_handler() */
void register_handler();

/* Initializes and returns sigset for use in sigprocmask(). */
sigset_t sigset_init();


/* Initializes user-level threads library supporting mechanisms. */
void init_library();

/* Registered with atexit() upon library's first use invocation. */
void cleanup_library();


/**
 * This function invoked when cleanup context first runs. 
 * Ending workers pass control to cleanup context. See uc_link of created workers.
 */
void clean_exited_worker_thread();


int is_held_by(worker_t this_tid, worker_mutex_t target);

int is_waiting_on_a_thread(tcb *thread);

int is_waiting_on_a_mutex(tcb *thread);

int is_blocked(tcb *thread);

int remaining_threads_blocked();

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
 * 
 * broadcast returns 1 if at least one other thread was waiting
 * on this mutex, 0 if no other threads were waiting on mutex.
*/
int broadcast_lock_release(worker_mutex_t mutex);

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
void alert_waiting_threads(worker_t ended, void *ended_retval_ptr);


void print_list(List *lst_ptr);
void print_queue(Queue* q_ptr);
void print_mutex_list(mutex_list *mutexes);

/* Returns NULL if target is not a currently initialized mutex, mutex_node* otherwise. */
mutex_node* fetch_from_mutexes(worker_mutex_t target);

int isUninitializedList(List *lst_ptr);
int isEmptyList(List *lst_ptr);
int compare_usage(tcb *to_be_inserted, tcb *curr);
void insert_by_usage(List *lst_ptr, tcb *to_be_inserted);
void insert_at_front(List *lst_ptr, tcb *to_be_inserted);
tcb* contains(List *lst_ptr, worker_t target);
tcb* remove_from(List *lst_ptr, worker_t target);
tcb* find_first_unblocked_thread(List *lst_ptr);

int isUninitialized(Queue *q_ptr);
int isEmpty(Queue *q_ptr);
void enqueue(Queue* q_ptr, tcb *data);
tcb* dequeue(Queue *q_ptr);

