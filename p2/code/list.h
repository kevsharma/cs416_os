#include "thread-worker.h"

/* Singular Linked List */
typedef struct List {
    unsigned short size;
    struct Node *front;
} List;

int isUninitializedList(List *lst_ptr);
int isEmptyList(List *lst_ptr);
void insert(List *lst_ptr, tcb *data);
int contains(List *lst_ptr, worker_t target);
tcb* remove_from(List *lst_ptr, worker_t target);
void print_list(List *lst_ptr);
