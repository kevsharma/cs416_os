#include "thread-worker.h"


/* Circular Linked List */
typedef struct Queue {
    unsigned short size;
    struct Node *rear;
} Queue;

int isUninitialized(Queue *q_ptr);
int isEmpty(Queue *q_ptr);
void enqueue(Queue* q_ptr, tcb *data);
tcb* dequeue(Queue *q_ptr);
void print_queue(Queue* q_ptr);