#include "thread-worker.h"

int isUninitializedList(List *lst_ptr);
int isEmptyList(List *lst_ptr);
void insert(List *lst_ptr, tcb *data);
int contains(List *lst_ptr, worker_t target);
tcb* remove_from(List *lst_ptr, worker_t target);
void print_list(List *lst_ptr);

int isUninitialized(Queue *q_ptr);
int isEmpty(Queue *q_ptr);
void enqueue(Queue* q_ptr, tcb *data);
tcb* dequeue(Queue *q_ptr);
void print_queue(Queue* q_ptr);

void print_mutex_list(mutex_list *mutexes);