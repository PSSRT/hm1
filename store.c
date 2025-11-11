#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// Dichiarazioni delle variabili condivise definite in filter.c
extern struct {
    double time;
    double noisy;
    double filtered;
} print_q[];

extern int q_head;
extern int q_tail;
extern int q_count;
extern pthread_mutex_t q_mutex;
extern pthread_cond_t cond_nonempty;
extern pthread_cond_t cond_nonfull;

void* store_task(void* arg) {
    FILE* fd = fopen("signal.txt", "w");
    if(!fd) { perror("fopen"); return NULL; }

    double dt = 1.0/5; // 5 Hz
    while(1) {
        for(int i=0; i<10; i++) {
            pthread_mutex_lock(&q_mutex);
            while(q_count==0)
                pthread_cond_wait(&cond_nonempty, &q_mutex);

            struct {
                double time;
                double noisy;
                double filtered;
            } s = print_q[q_head];

            q_head = (q_head+1)%10;
            q_count--;
            pthread_cond_signal(&cond_nonfull);
            pthread_mutex_unlock(&q_mutex);

            fprintf(fd, "%lf, %lf, %lf\n", s.time, s.noisy, s.filtered);
        }
        fflush(fd);
        usleep(dt*1e6);
    }

    fclose(fd);
    return NULL;
}

int main() {
    pthread_t store_tid;
    pthread_create(&store_tid, NULL, store_task, NULL);
    pthread_join(store_tid, NULL);
    return 0;
}
