#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INITIAL_MUTEX 0 /* All mutex_num values are unsigned integers. 0 implies mutex_list init-d first time.*/
#define NOT_HELD_BY_ANY_THREAD 0 /* Valid thread_ids start with 1 (main) and are unsigned ints which are not 0.*/

typedef unsigned int worker_t;
typedef unsigned int mutex_num;

/* mutex struct definition */
typedef struct worker_mutex_t {
	mutex_num lock_num;
	worker_t holder_tid;
} worker_mutex_t;

typedef struct mutex_node {
    worker_mutex_t *data;
    struct Node *next;
} mutex_node;

/* Singular Linked List */
typedef struct mutex_list {
    struct mutex_node *front;
} mutex_list;

static mutex_num *current_mutex_num;
static mutex_list *mutexes;


void print_mutex_list() {
    mutex_node *ptr = mutexes->front;
    printf("Printing lst: \n");
    
    while(ptr) {
        printf("%d held by %d.\n", ptr->data->lock_num, ptr->data->holder_tid);
        ptr = ptr->next;
    }

    printf("\n");
}


int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
    // block signals (we will access shared mutex list).
    assert(mutexes != NULL);

    // Create mutex.
    mutex = (worker_mutex_t *) malloc(sizeof(worker_mutex));
    mutex->lock_num = ++(*current_mutex_num); // the next mutex_num.
    mutex->holder_tid = NOT_HELD_BY_ANY_THREAD;

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
    while(mutex->holder_tid != NOT_HELD_BY_ANY_THREAD) {
        running->seeking_lock = mutex->lock_num;
        swapcontext(running->uctx, scheduler);
    }

    mutex->holder_tid = running->thread_id;
    // unblock signals - finished accessing shared resource
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
     * thread gives up the lock, will go back into a waiting state
     * by changing their seeking_lock. 
     * 
     * (See worker_mutex_lock for more details.)
    */
    mutex->holder_tid = NOT_HELD_BY_ANY_THREAD; 

    Node *ptr = tcbs->front;
	while(!ptr) {
        // If a thread is waiting on the unlocked mutex, change its state
        // so that Scheduler will not skip it anymore.
        if (ptr->data->seeking_lock == mutex->lock_num) {
            ptr->data->seeking_lock = NOT_HELD_BY_ANY_THREAD;
        }

		ptr = ptr->next;
	}
    // unblock signals
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
