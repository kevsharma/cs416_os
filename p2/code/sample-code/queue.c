#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    int data;
    struct Node *next;
} Node;

/* Circular Linked List */
typedef struct Queue {
    unsigned short size;
    struct Node *rear;
} Queue;

void queue_init(Queue* q_ptr) {
    q_ptr->size = 0;
    q_ptr->rear = NULL;
}

int isUninitialized(Queue *q_ptr) {
    return !q_ptr;
}

int isEmpty(Queue *q_ptr) {
    return !q_ptr->size;
}

/* O(1) Enqueue Operation */
void enqueue(Queue* q_ptr, Node *item) {
    if (isUninitialized(q_ptr)) {
        queue_init(q_ptr);
    }

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
Node* dequeue(Queue *q_ptr) {
    if (isUninitialized(q_ptr) || isEmpty(q_ptr)) {
        return NULL;
    }

    Node* front;

    if (q_ptr->size == 1) {
        front = q_ptr->rear; // rear is front.
        front->next = NULL; // rear pointed to itself.

        q_ptr->size = 0;
        q_ptr->rear = NULL;
        
        return front;
    }

    // If the queue has 2 elements, dequeue results in rear pointing to itself.
    front = q_ptr->rear->next;
    q_ptr->rear->next = front->next;
    --(q_ptr->size);
    return front;
}


void print_queue(Queue* q_ptr) {
    if (isUninitialized(q_ptr) || isEmpty(q_ptr)) {
        printf("Empty Queue \n.");
    }

    Node* iterator = q_ptr->rear->next; // start at front;
    for(; iterator != q_ptr->rear; iterator = iterator->next)
        printf("%d, ", iterator->data);
    printf("%d\n", iterator->data);
}


static Queue queue;
int main() {
    queue_init(&queue);

    int i;
    for (i = 0; i < 10; ++i) {
        Node* item = (Node *) malloc(sizeof(Node));
        item->data = i;
        enqueue(&queue, item);
    }

    print_queue(&queue);

    while (!isEmpty(&queue)) {
        Node* dequeued_item = dequeue(&queue);
        printf("Freed Node with Data: %d\n", dequeued_item->data);
        free(dequeued_item);
    }

    return EXIT_SUCCESS;
}