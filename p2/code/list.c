#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef unsigned int worker_t;
target_thread_id
typedef struct TCB { // kept as simple as possible, pointer to it so doesn't matter.
	worker_t thread_id;
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


List *lst;

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

/* Returns 1 if the list contains the worker, else returns 0.*/
int contains(List *lst_ptr, worker_t target) {
    Node *ptr = lst_ptr->front;
    while(ptr) {
        if (ptr->data->thread_id == target) {
            return 1;
        }
        ptr = ptr->next;
    }

    return 0;
}

tcb* remove_from(List *lst_ptr, worker_t target) {
    assert(!isEmptyList(lst_ptr));
    assert(contains(lst_ptr, target));

    Node *front = lst_ptr->front;
    tcb *target_tcb;

    if (front->data->thread_id == target) {
        Node *match = front;
        target_tcb = match->data;
        lst_ptr->front = front->next;
        --(lst_ptr->size);

        free(match);
        return target_tcb;
    }

    Node *ptr = front->next;
    Node *prev = front;

    for (; !ptr; prev = prev->next, ptr = ptr->next) {
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

void print_list(List *lst_ptr) {
    Node *ptr = lst_ptr->front;
    printf("Printing lst: ");
    
    while(ptr) {
        printf("%d ", ptr->data->thread_id);
        ptr = ptr->next;
    }

    printf("\n");
}

void test0() {
    List *lst = malloc(sizeof(List));
    free(lst);
}

void test1() {
    List *lst = malloc(sizeof(List));
    lst->size = 0;
    lst->front = NULL;

    tcb* data1 = malloc(sizeof(tcb));
    data1->thread_id = 1;

    tcb* data2 = malloc(sizeof(tcb));
    data2->thread_id = 2;

    insert(lst, data1);
    insert(lst, data2);

    print_list(lst);

    tcb* rt2 = remove_from(lst, 2);
    print_list(lst);
    tcb* rt1 = remove_from(lst, 1);
    print_list(lst);

    // empty list.
    free(rt1);
    free(rt2);
    free(lst);
}

int main() {
    test0();
    test1();
}
