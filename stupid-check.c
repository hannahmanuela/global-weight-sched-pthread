#define _GNU_SOURCE
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>


#define NUM_CORES 56
#define TICK_LENGTH 50

pthread_mutex_t log_lock;


void schedule() {

    int a = 0;
    for (int i = 0; i < 1000; i++) {
        a++;
    }

}

void run_core(void* core_num_ptr) {

    int core_id = (int)core_num_ptr;

    struct process *pool = NULL;

    // pin to an actual core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    while (1) {

        struct timespec start_cpu, end_cpu;
        struct timespec start_wall, end_wall;
        clock_gettime(CLOCK_MONOTONIC_RAW, &start_wall);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start_cpu);

        schedule();

        clock_gettime(CLOCK_MONOTONIC_RAW, &end_wall);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end_cpu);
        long long wall_us = (end_wall.tv_sec - start_wall.tv_sec) * 1000000LL + (end_wall.tv_nsec - start_wall.tv_nsec) / 1000;
        long long cpu_us = (end_cpu.tv_sec - start_cpu.tv_sec) * 1000000LL + (end_cpu.tv_nsec - start_cpu.tv_nsec) / 1000;

        FILE *f = fopen("schedule_time.txt", "a");
        if (wall_us > 10) {
            fprintf(f, "====== here long =======\n");
        }
        fprintf(f, "total us: %lld, ru: %lld\n", wall_us, cpu_us);
        fclose(f);

        usleep(TICK_LENGTH);
        
    }


}


void main() {
    pthread_mutex_init(&log_lock, NULL);

    FILE *f = fopen("schedule_time.txt", "w");
    fclose(f);

    pthread_t threads[NUM_CORES]; 

    for (int i = 0; i < NUM_CORES; i ++) {
        pthread_create(&threads[i], NULL, run_core, (void*)i);
        // usleep(200);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(threads[i], NULL);
    }

}





