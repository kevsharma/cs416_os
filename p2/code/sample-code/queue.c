#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* This queue.c is a preliminary FIFO queue.
 * TCB allocation in enqueue, and free-ing in dequeue relieves burden from consumer.
 * See also: map function for future use in implementing JOIN.
*/

typedef struct TCB { // kept as simple as possible, pointer to it so doesn't matter.
	unsigned int thread_id;
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


Queue *queue;

int isUninitialized(Queue *q_ptr) {
    return !q_ptr;
}

int isEmpty(Queue *q_ptr) { 
    assert(!isUninitialized(q_ptr));
    return q_ptr->size == 0;
}

void queue_init() {
    queue = (Queue *) malloc(sizeof(Queue));
    queue->size = 0;
    queue->rear = NULL;
}

void queue_deinit() {
    assert(isEmpty(queue));
    free(queue);
    queue = NULL;
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
    assert(!isUninitialized(q_ptr));
    assert(!isEmpty(q_ptr));

    Node* iterator = q_ptr->rear->next; // start at front;
    for(; iterator != q_ptr->rear; iterator = iterator->next)
        printf("%d, ", iterator->data->thread_id);
    printf("%d\n", iterator->data->thread_id);
}


void finaltest() {
    queue_init();

    int i;
    for (i = 0; i < 10; ++i) {
        tcb* item = (tcb *) malloc(sizeof(tcb));
        item->thread_id = i;
        enqueue(queue, item);
    }

    print_queue(queue);

    while (!isEmpty(queue)) {
        tcb* dequeued_item = dequeue(queue);
        printf("Freed Node with Data: %d\n", dequeued_item->thread_id);
        free(dequeued_item);
    }

    queue_deinit();
}

void testEnqueue() {
    queue_init();
    tcb* item = malloc(sizeof(tcb));
    item->thread_id = 13;

    enqueue(queue, item);
    enqueue(queue, item);

    print_queue(queue);

    dequeue(queue);
    dequeue(queue);

    free(item);

    queue_deinit();
}


int main() {
    testEnqueue();
}
