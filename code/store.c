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

// Variabili per la coda print_q
#define MQ_NAME "/print_q"   // nome della coda condivisa
#define MAX_MSG 10           // massimo numero di messaggi nella coda
mqd_t my_queue;

// Variabili per la coda mse
#define MSE_QUEUE_NAME "/mse_q"
#define T_SAMPLE_MSE 1000000      // 1 Hz
#define MAX_MSG_MSE 10             // massimo numero di messaggi
#define MAX_MSG_SIZE 256
mqd_t queue_mse;

typedef struct {
    struct timespec ts;
    double t;
    double val;
    double noise;
    double filt;
} sample_msg_t;

// Flag di terminazione
volatile sig_atomic_t stop_flag = 0;

// Prototipo funzione parsing linea di comando
void parse_cmdline(int argc, char **argv);

// Handler per SIGINT (Ctrl+C)
static void sigint_handler(int sig) {
    (void)sig;
    stop_flag = 1;
}

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

    while (!stop_flag) {
        wait_next_activation(store);

        //print_q
        sample_msg_t msg;
        ssize_t n;
        // Leggi tutti i messaggi disponibili nella coda (senza bloccare)
        while (1) {
            errno = 0;
            n = mq_receive(my_queue, (char*)&msg, sizeof(msg), NULL);
            if (n == -1) {
                if (errno == EAGAIN)
                    break;  // nessun messaggio disponibile
                else {
                    perror("mq_receive(/print_q)");
                    break;
                }
            }

            // Scrivi i dati sul file rispettando i flag
            fprintf(outfd, "%.9f", msg.t);  // tempo sempre stampato
            if (flag_signal)   fprintf(outfd, ",%.9f", msg.val); else fprintf(outfd, ",");
            if (flag_noise)    fprintf(outfd, ",%.9f", msg.noise); else fprintf(outfd, ",");
            if (flag_filtered) fprintf(outfd, ",%.9f", msg.filt); else fprintf(outfd, ",");
            fprintf(outfd, "\n");
        }

        //mse_q
        ssize_t m;
        char msgm[MAX_MSG_SIZE + 1];  // +1 per sicurezza nel terminatore
        // Leggi tutti i messaggi disponibili nella coda (senza bloccare)
        while (1) {
            errno = 0;
            m = mq_receive(queue_mse, msgm, MAX_MSG_SIZE, NULL);

            if (m == -1) {
                if (errno == EAGAIN)
                    break;  // nessun messaggio disponibile
                else {
                    perror("mq_receive(/mse_q)");
                    break;
                }
            }

           
            // Stampa valore ricevuto
            //printf("[DEBUG] Ricevuto messaggio da /mse_q (%zd byte)\n", m);
            printf("MSE value: %s\n", msgm);
            fflush(stdout);
        }

        fflush(outfd);  // assicurati che i dati siano scritti su disco
    }

    fclose(outfd);
    return NULL;
}

int main(int argc, char **argv) {
    // Parsing della linea di comando
    parse_cmdline(argc, argv);

    // Handler per Ctrl+C
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    //---------------- CREAZIONE CODA -----------------------
    struct mq_attr attr_queue;
    attr_queue.mq_flags = 0;
    attr_queue.mq_maxmsg = MAX_MSG;
    attr_queue.mq_msgsize = sizeof(sample_msg_t);
    attr_queue.mq_curmsgs = 0;

    // Apertura coda print_q in lettura non bloccante
    mq_unlink(MQ_NAME);
    if ((my_queue = mq_open(MQ_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr_queue)) == -1) {
        perror("Errore nella creazione e apertura della coda");
        exit(EXIT_FAILURE);
    }
    printf("[STORE] Coda /print_q aperta in lettura.\n");

    // Apertura coda mse in lettura non bloccante
    struct mq_attr attr_mse;
    attr_mse.mq_flags = 0;
    attr_mse.mq_maxmsg = MAX_MSG_MSE;
    attr_mse.mq_msgsize = MAX_MSG_SIZE;
    attr_mse.mq_curmsgs = 0;

    mq_unlink(MSE_QUEUE_NAME);
    if ((queue_mse = mq_open(MSE_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr_mse)) == -1) {
        perror("Errore nella creazione e apertura della coda");
        exit(EXIT_FAILURE);
    }
    printf("[STORE] Coda /mse_q aperta in lettura.\n");

    //---------------- CREAZIONE THREAD ---------------------
    pthread_t th_store;
    periodic_thread TH_store;

    // Thread storage
    TH_store.index = 1;
    TH_store.period = T_SAMPLE;
    pthread_create(&th_store, NULL, storage, &TH_store);

    // Attendi terminazione con 'q' da tastiera
    printf("Premere 'q' + ENTER o Ctrl-C per terminare.\n");
    while (!stop_flag) {
        if (getchar() == 'q') {
            stop_flag = 1;
            break;
        }
    }

    // Attendi la terminazione del thread
    pthread_join(th_store, NULL);

    // Pulizia finale delle risorse
    mq_close(my_queue);
    mq_unlink(MQ_NAME);
    mq_close(queue_mse);
    mq_unlink(MSE_QUEUE_NAME);

    printf("Terminazione del processo store.\n");
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
