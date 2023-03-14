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
    worker_mutex_t *data;
    struct mutex_node *next;
} mutex_node;

/* Singular Linked List */
typedef struct mutex_list {
    struct mutex_node *front;
} mutex_list;

/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, void*(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);
