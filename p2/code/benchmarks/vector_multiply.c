#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "../thread-worker.h"

#define DEFAULT_THREAD_NUM 2
#define VECTOR_SIZE 3000000

/* Global variables */
pthread_mutex_t   mutex;
int thread_num;
int* counter;
pthread_t *thread;
int r[VECTOR_SIZE];
int s[VECTOR_SIZE];
int sum = 0;

/* A CPU-bound task to do vector multiplication */
void vector_multiply(void* arg) {
	int i = 0;
	int n = *((int*) arg);
	
	for (i = n; i < VECTOR_SIZE; i += thread_num) {
		pthread_mutex_lock(&mutex);
		sum += r[i] * s[i];
		pthread_mutex_unlock(&mutex);	
	}

	pthread_exit(NULL);
}

void verify() {
	int i = 0;
	sum = 0;
	for (i = 0; i < VECTOR_SIZE; i += 1) {
		sum += r[i] * s[i];	
	}
	//printf("verified sum is: %d\n", sum);
}

int main(int argc, char **argv) {
	
	int i = 0;

	if (argc == 1) {
		thread_num = DEFAULT_THREAD_NUM;
	} else {
		if (argv[1] < 1) {
			printf("enter a valid thread number\n");
			return 0;
		} else {
			thread_num = atoi(argv[1]);
		}
	}

	// initialize counter
	counter = (int*)malloc(thread_num*sizeof(int));
	for (i = 0; i < thread_num; ++i)
		counter[i] = i;

	// initialize pthread_t
	thread = (pthread_t*)malloc(thread_num*sizeof(pthread_t));

	// initialize data array
	for (i = 0; i < VECTOR_SIZE; ++i) {
		r[i] = i;
		s[i] = i;
	}

	pthread_mutex_init(&mutex, NULL);

	struct timespec start, end;
        clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < thread_num; ++i)
		pthread_create(&thread[i], NULL, &vector_multiply, &counter[i]);

	for (i = 0; i < thread_num; ++i)
		pthread_join(thread[i], NULL);

        fprintf(stderr, "***************************\n");

        clock_gettime(CLOCK_REALTIME, &end);

        printf("Total run time: %lu micro-seconds\n",
               (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000);

	pthread_mutex_destroy(&mutex);
	verify();

	// Free memory on Heap
	free(thread);
	free(counter);

#ifdef USE_WORKERS
        fprintf(stderr , "Total sum is: %d\n", sum);
        print_app_stats();
        fprintf(stderr, "***************************\n");
#endif


	return 0;
}
