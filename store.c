#include <pthread.h>      // thread periodici
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include "rt-lib.h"
#include <errno.h>         // per il controllo degli errori
#include <mqueue.h>        // per la creazione e gestione delle code POSIX

#define T_SAMPLE 200000    // periodo in microsecondi (5 Hz)
#define OUTFILE "signal.txt"

#define USAGE_STR \
    "Usage: %s [-s] [-n] [-f]\n" \
    "\t -s: plot original signal\n" \
    "\t -n: plot noisy signal\n" \
    "\t -f: plot filtered signal\n" \
    ""


// Flag per il plotting
int flag_signal = 0;
int flag_noise = 0;
int flag_filtered = 0;

// Variabili per la coda
#define MQ_NAME "/print_q"   // nome della coda condivisa
#define MAX_MSG 10           // massimo numero di messaggi nella coda

typedef struct {
    struct timespec ts;
    double t;
    double val;
    double noise;
    double filt;
} sample_msg_t;

mqd_t my_queue;

// Prototipo funzione parsing linea di comando
void parse_cmdline(int argc, char **argv);

// Thread di storage
void *storage(void *parameter) {
    periodic_thread *store = (periodic_thread *) parameter;
    start_periodic_timer(store, store->period);

    // Apertura file di output
    int outfile = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    FILE *outfd = fdopen(outfile, "w");

    if (outfile < 0 || !outfd) {
        perror("Unable to open/create output file. Exiting.");
        return (void*)EXIT_FAILURE;
    }

    printf("[STORE] Thread di storage avviato (5 Hz)\n");

    while (1) {
        wait_next_activation(store);

        sample_msg_t msg;
        ssize_t n;

        // Leggi tutti i messaggi disponibili nella coda (senza bloccare)
        while ((n = mq_receive(my_queue, (char*)&msg, sizeof(msg), NULL)) != -1) {

            // Scrivi i dati sul file rispettando i flag
            fprintf(outfd, "%.9f", msg.t);  // tempo sempre stampato
            if (flag_signal)   fprintf(outfd, ",%.9f", msg.val); else fprintf(outfd, ",");
            if (flag_noise)    fprintf(outfd, ",%.9f", msg.noise); else fprintf(outfd, ",");
            if (flag_filtered) fprintf(outfd, ",%.9f", msg.filt); else fprintf(outfd, ",");
            fprintf(outfd, "\n");
        }

        // Se la coda era vuota, errno == EAGAIN (non è un errore)
        if (errno != EAGAIN)
            perror("mq_receive");

        fflush(outfd);  // assicurati che i dati siano scritti su disco
    }

    fclose(outfd);
    return NULL;
}

int main(int argc, char **argv) {
    // Parsing della linea di comando
    parse_cmdline(argc, argv);

    //---------------- CREAZIONE CODA -----------------------
    struct mq_attr attr_queue;
    attr_queue.mq_flags = 0;
    attr_queue.mq_maxmsg = MAX_MSG;
    attr_queue.mq_msgsize = sizeof(sample_msg_t);
    attr_queue.mq_curmsgs = 0;

    // Apertura coda in lettura non bloccante
    if ((my_queue = mq_open(MQ_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0644, &attr_queue)) == -1) {
        perror("Errore nella creazione e apertura della coda");
        exit(EXIT_FAILURE);
    }
    printf("[STORE] Coda /print_q aperta in lettura.\n");

    //---------------- CREAZIONE THREAD ---------------------
    pthread_t th_store;
    periodic_thread TH_store;

    pthread_attr_t attr;
    struct sched_param par;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    // Thread storage
    TH_store.index = 1;
    TH_store.period = T_SAMPLE;
    TH_store.priority = 60;  // priorità minore di filter (che è 80)
    par.sched_priority = TH_store.priority;
    pthread_attr_setschedparam(&attr, &par);
    pthread_create(&th_store, &attr, storage, &TH_store);

    pthread_attr_destroy(&attr);

    // Attendi terminazione con 'q' da tastiera
    while (1) {
        if (getchar() == 'q') {
            printf("Terminazione del processo store.\n");
            break;
        }
    }

    mq_close(my_queue);
    mq_unlink(MQ_NAME);
    return 0;
}

void parse_cmdline(int argc, char **argv) {
    int opt;

    while ((opt = getopt(argc, argv, "snf")) != -1) {
        switch (opt) {
            case 's': flag_signal = 1; break;
            case 'n': flag_noise = 1; break;
            case 'f': flag_filtered = 1; break;
            default:
                fprintf(stderr, USAGE_STR, argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Se nessun flag specificato, abilita tutti
    if ((flag_signal | flag_noise | flag_filtered) == 0) {
        flag_signal = flag_noise = flag_filtered = 1;
    }
}
