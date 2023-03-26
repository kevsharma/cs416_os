#include "../my_vm.h"
#include <time.h>
#define num_threads 15

void *pointers[num_threads];
int ids[num_threads];
pthread_t threads[num_threads];
int alloc_size = 10000;
int matrix_size = 5;

void *alloc_mem(void *id_arg) {
    int id = *((int *)id_arg);
    pointers[id] = t_malloc(alloc_size);
    return NULL;
}

void *put_mem(void *id_arg) {
    int val = 1;
    void *va_pointer = pointers[*((int *)id_arg)];
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int address_a = (unsigned int)va_pointer + ((i * matrix_size * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_a, &val, sizeof(int));
	    //val++;
        }
    }
    return NULL;
}

void *mat_mem(void *id_arg) {
    int i = *((int *)id_arg);
    void *a = pointers[i];
    void *b = pointers[i + 1];
    void *c = pointers[i + 2];
    mat_mult(a, b, matrix_size, c);
    return NULL;
}

void *free_mem(void *id_arg) {
    int id = *((int *)id_arg);
    t_free(pointers[id], alloc_size);
}

int main() {
    srand(time(NULL));

    for (int i = 0; i < num_threads; i++)
        ids[i] = i;

    for (int i = 0; i < num_threads; i++)
        pthread_create(&threads[i], NULL, alloc_mem, (void *)&ids[i]);
    
    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    printf("Allocated Pointers: \n");
    for (int i = 0; i < num_threads; i++)
        printf("%x ", (int)pointers[i]);
    printf("\n");
    
    printf("initializing some of the memory by in multiple threads\n");
    for (int i = 0; i < num_threads; i++)
        pthread_create(&threads[i], NULL, put_mem, (void *)&ids[i]);

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    printf("Randomly checking a thread allocation to see if everything worked correctly!\n");
    int rand_id = rand() % num_threads;
    void *a = pointers[rand_id];
    int val = 0;
    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int address_a = (unsigned int)a + ((i * matrix_size * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &val, sizeof(int));
            printf("%d ", val);
        }
        printf("\n");
    }

    printf("Performing matrix multiplications in multiple threads threads!\n");

    for (int i = 0; i < num_threads; i+=3)
        pthread_create(&threads[i], NULL, mat_mem, (void *)&ids[i]);

    for (int i = 0; i < num_threads; i+=3)
        pthread_join(threads[i], NULL);

    printf("Randomly checking a thread allocation to see if everything worked correctly!\n");
    rand_id = (((rand() % (num_threads/3)) + 1) * 3) - 1;
    a = pointers[rand_id];
    val = 0;

    for (int i = 0; i < matrix_size; i++) {
        for (int j = 0; j < matrix_size; j++) {
            int address_a = (unsigned int)a + ((i * matrix_size * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &val, sizeof(int));
            printf("%d ", val);
        }
        printf("\n");
    }
    int old = (int)pointers[0];
    printf("Gonna free everything in multiple threads!\n");
    // ufree(pointers[0], alloc_size);
    //
    for (int i = 0; i < num_threads; i++)
        pthread_create(&threads[i], NULL, free_mem, (void *)&ids[i]);

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    void *temp = (void *)1;
    int flag = 0;
    while (temp != NULL) {
        temp = t_malloc(10);
        if ((int)temp == old) {
            printf("Free Worked!\n");
            flag = 1;
            break;
        }
    }
    if (!flag) {
        printf("Some Problem with free!\n");
    }
}
