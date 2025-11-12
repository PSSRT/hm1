#include <pthread.h>
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
#include <errno.h>
#include <mqueue.h>

// Proprietà del segnale
#define SIG_SAMPLE SIGRTMIN
#define SIG_HZ 1.0
#define OUTFILE "signal.txt"
#define T_SAMPLE 20000  // 50 Hz

// Proprietà per la coda principale
#define MQ_NAME "/print_q"
#define MAX_MSG 10

// Proprietà per la coda MSE
#define MSE_QUEUE_NAME "/mse_q"
#define QUEUE_PERMISSIONS 0660
#define MAX_MSG_SIZE 256
#define MAX_MESSAGES 10
#define T_SAMPLE_MSE 1000000  // 1 Hz

// Filtro Butterworth 2nd-order, cutoff a 2Hz @ fc = 50Hz
#define BUTTERFILT_ORD 2
double b[3] = {0.0134, 0.0267, 0.0134};
double a[3] = {1.0000, -1.6475, 0.7009};

// Calcolo MSE
#define N_SAMPLES 50

// Struttura messaggio per la coda principale
typedef struct {
    struct timespec ts;
    double t;
    double val;
    double noise;
    double filt;
} sample_msg_t;

// Variabili globali per le code
mqd_t my_queue;

// Variabili per le risorse condivise
double sig_noise;
double sig_val;
double sig_filt;
double t = 0.0;

// Buffer circolari per il calcolo MSE
double sig_original[N_SAMPLES];
int index_sig_og = 0;
double sig_filtered_buf[N_SAMPLES];
int index_sig_filt = 0;

// Mutex per le risorse condivise
pthread_mutex_t mutex_noise;
pthread_mutex_t mutex_val;
pthread_mutex_t mutex_filter;
pthread_mutex_t mutex_time;
pthread_mutex_t mutex_sig_buffers;

// Prototipi di funzioni
double get_butter(double cur, double *a, double *b);
double get_mean_filter(double cur);

static int first_mean = 0;

// Thread di generazione del segnale
void *generation(void *parameter)
{
    periodic_thread *gen = (periodic_thread *)parameter;
    start_periodic_timer(gen, gen->period);
    
    const double Ts = T_SAMPLE / 1e6;  // Periodo di campionamento in secondi

    while (1) {
        wait_next_activation(gen);
        
        double t_local;
        pthread_mutex_lock(&mutex_time);
        t_local = t;
        pthread_mutex_unlock(&mutex_time);

        // Genera il segnale originale
        double sig_val_local = sin(2 * M_PI * SIG_HZ * t_local);

        pthread_mutex_lock(&mutex_val);
        sig_val = sig_val_local;
        pthread_mutex_unlock(&mutex_val);

        // Salva nel buffer per MSE
        pthread_mutex_lock(&mutex_sig_buffers);
        sig_original[index_sig_og % N_SAMPLES] = sig_val_local;
        index_sig_og = (index_sig_og + 1) % N_SAMPLES;
        pthread_mutex_unlock(&mutex_sig_buffers);

        // Aggiungi rumore al segnale
        double sig_noise_local = sig_val_local + 0.5 * cos(2 * M_PI * 10 * t_local);
        sig_noise_local += 0.9 * cos(2 * M_PI * 4 * t_local);
        sig_noise_local += 0.9 * cos(2 * M_PI * 12 * t_local);
        sig_noise_local += 0.8 * cos(2 * M_PI * 15 * t_local);
        sig_noise_local += 0.7 * cos(2 * M_PI * 18 * t_local);

        pthread_mutex_lock(&mutex_noise);
        sig_noise = sig_noise_local;
        pthread_mutex_unlock(&mutex_noise);
        
        pthread_mutex_lock(&mutex_time);
        t += Ts;
        pthread_mutex_unlock(&mutex_time);
    }
}

// Thread di filtraggio del segnale
void *filtering(void *parameter)
{
    periodic_thread *filter = (periodic_thread *)parameter;
    start_periodic_timer(filter, filter->period);

    // Configurazione della coda
    struct mq_attr attr_queue;
    attr_queue.mq_flags = 0;
    attr_queue.mq_maxmsg = MAX_MSG;
    attr_queue.mq_msgsize = sizeof(sample_msg_t);
    attr_queue.mq_curmsgs = 0;

    if ((my_queue = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr_queue)) == -1) {
        perror("Errore nella creazione e apertura della coda\n");
        exit(1);
    }
    printf("Producer mq_open -> %d\n", (int)my_queue);

    while (1) {
        wait_next_activation(filter);
        
        double sig_noise_local;
        pthread_mutex_lock(&mutex_noise);
        sig_noise_local = sig_noise;
        pthread_mutex_unlock(&mutex_noise);

        // Filtra il segnale
        double sig_filt_local = get_mean_filter(sig_noise_local);
        // Oppure usa: double sig_filt_local = get_butter(sig_noise_local, a, b);
        
        pthread_mutex_lock(&mutex_filter);
        sig_filt = sig_filt_local;
        pthread_mutex_unlock(&mutex_filter);

        // Salva nel buffer per MSE
        pthread_mutex_lock(&mutex_sig_buffers);
        sig_filtered_buf[index_sig_filt % N_SAMPLES] = sig_filt_local;
        index_sig_filt = (index_sig_filt + 1) % N_SAMPLES;
        pthread_mutex_unlock(&mutex_sig_buffers);

        // Crea il messaggio da mandare nella coda
        sample_msg_t msg;
        clock_gettime(CLOCK_REALTIME, &msg.ts);
 
        pthread_mutex_lock(&mutex_time);
        msg.t = t;
        pthread_mutex_unlock(&mutex_time);
    
        pthread_mutex_lock(&mutex_val);
        msg.val = sig_val;
        pthread_mutex_unlock(&mutex_val);
        
        pthread_mutex_lock(&mutex_noise);
        msg.noise = sig_noise;
        pthread_mutex_unlock(&mutex_noise);
        
        pthread_mutex_lock(&mutex_filter);
        msg.filt = sig_filt;
        pthread_mutex_unlock(&mutex_filter);
    
        // Invia il messaggio
        if (mq_send(my_queue, (const char *)&msg, sizeof(msg), 0) == -1) {
            perror("mq_send");
            exit(EXIT_FAILURE);
        }
    }
}

// Thread per il calcolo del MSE
void *calculate_mse(void *param)
{
    periodic_thread *mse = (periodic_thread *)param;
    start_periodic_timer(mse, mse->period);

    mqd_t mq_mse;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    mq_mse = mq_open(MSE_QUEUE_NAME, O_WRONLY | O_CREAT, QUEUE_PERMISSIONS, &attr);
    if (mq_mse == -1) {
        perror("calculate_mse: mq_open");
        pthread_exit(NULL);
    }

    double local_original[N_SAMPLES];
    double local_filtered[N_SAMPLES];

    while (1) {
        wait_next_activation(mse);

        // Copia i buffer localmente
        pthread_mutex_lock(&mutex_sig_buffers);
        memcpy(local_original, sig_original, sizeof(sig_original));
        memcpy(local_filtered, sig_filtered_buf, sizeof(sig_filtered_buf));
        pthread_mutex_unlock(&mutex_sig_buffers);

        // Calcola MSE
        double mse_val = 0.0;
        for (int i = 0; i < N_SAMPLES; i++) {
            double diff = local_original[i] - local_filtered[i];
            mse_val += diff * diff;
        }
        mse_val /= N_SAMPLES;

        // Invia MSE alla coda
        char msg[MAX_MSG_SIZE];
        snprintf(msg, sizeof(msg), "%f", mse_val);
        if (mq_send(mq_mse, msg, strlen(msg) + 1, 0) == -1) {
            perror("calculate_mse: mq_send");
        }
    }
    
    mq_close(mq_mse);
    return NULL;
}

// Thread per leggere i valori MSE (debug)
void *read_mse(void *param)
{
    (void)param;
    mqd_t mq_mse = mq_open(MSE_QUEUE_NAME, O_RDONLY);
    if (mq_mse == -1) {
        perror("mq_open read");
        return NULL;
    }

    char msg[MAX_MSG_SIZE];
    while (1) {
        ssize_t n = mq_receive(mq_mse, msg, sizeof(msg), NULL);
        if (n >= 0) {
            printf("MSE value: %s\n", msg);
            fflush(stdout);
        }
    }

    mq_close(mq_mse);
    return NULL;
}

// Thread per leggere i valori dalla coda print_q (debug)
void *read_print_queue(void *param)
{
    (void)param;
    
    // Apri la coda in sola lettura
    mqd_t mq_print = mq_open(MQ_NAME, O_RDONLY);
    if (mq_print == -1) {
        perror("read_print_queue: mq_open");
        return NULL;
    }
    
    printf("Consumer mq_open -> %d\n", (int)mq_print);
    printf("Lettura dalla coda %s avviata...\n", MQ_NAME);
    
    sample_msg_t msg;
    
    while (1) {
        ssize_t n = mq_receive(mq_print, (char *)&msg, sizeof(msg), NULL);
        if (n >= 0) {
            printf("----------------------------------------\n");
            printf("Timestamp: %ld.%09ld\n", msg.ts.tv_sec, msg.ts.tv_nsec);
            printf("Time (t): %.6f s\n", msg.t);
            printf("Original Signal: %.6f\n", msg.val);
            printf("Noisy Signal: %.6f\n", msg.noise);
            printf("Filtered Signal: %.6f\n", msg.filt);
            printf("----------------------------------------\n");
            fflush(stdout);
        } else {
            perror("read_print_queue: mq_receive");
        }
    }
    
    mq_close(mq_print);
    return NULL;
}

int main()
{
    // Configurazione dei mutex con Priority Ceiling Protocol
    int ceiling = 80;
    pthread_mutexattr_t mymutexattr;
    pthread_mutexattr_init(&mymutexattr);
    pthread_mutexattr_setprotocol(&mymutexattr, PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mymutexattr, ceiling);

    // Inizializzazione dei mutex
    pthread_mutex_init(&mutex_noise, &mymutexattr);
    pthread_mutex_init(&mutex_val, &mymutexattr);
    pthread_mutex_init(&mutex_filter, &mymutexattr);
    pthread_mutex_init(&mutex_time, &mymutexattr);
    pthread_mutex_init(&mutex_sig_buffers, &mymutexattr);

    pthread_mutexattr_destroy(&mymutexattr);

    // Configurazione thread
    pthread_t th_gen, th_filter, th_mse, th_read_mse, th_read_print;
    periodic_thread TH_gen, TH_filter, TH_mse;
    pthread_attr_t attr;
    struct sched_param par;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    // Thread generazione segnale
    TH_gen.index = 1;
    TH_gen.period = T_SAMPLE;
    TH_gen.priority = 80;
    par.sched_priority = TH_gen.priority;
    pthread_attr_setschedparam(&attr, &par);
    pthread_create(&th_gen, &attr, generation, &TH_gen);

    // Thread filtraggio segnale
    TH_filter.index = 2;
    TH_filter.period = T_SAMPLE;
    TH_filter.priority = 70;
    par.sched_priority = TH_filter.priority;
    pthread_attr_setschedparam(&attr, &par);
    pthread_create(&th_filter, &attr, filtering, &TH_filter);

    // Thread calcolo MSE
    TH_mse.index = 3;
    TH_mse.period = T_SAMPLE_MSE;
    TH_mse.priority = 60;
    par.sched_priority = TH_mse.priority;
    pthread_attr_setschedparam(&attr, &par);
    pthread_create(&th_mse, &attr, calculate_mse, &TH_mse);

    // Thread lettura MSE (debug)
    pthread_create(&th_read_mse, NULL, read_mse, NULL);
    
    // Thread lettura coda print_q (debug)
    pthread_create(&th_read_print, NULL, read_print_queue, NULL);

    pthread_attr_destroy(&attr);

    printf("Sistema avviato. Premi 'q' per uscire.\n");
    
    while (1) {
        if (getchar() == 'q') {
            printf("Processo terminato con successo!\n");
            break;
        }
    }

    // Chiusura code
    mq_close(my_queue);
    mq_unlink(MQ_NAME);
    mq_unlink(MSE_QUEUE_NAME);

    return 0;
}

// Implementazione filtro Butterworth
double get_butter(double cur, double *a, double *b)
{
    double retval;
    int i;

    static double in[BUTTERFILT_ORD + 1];
    static double out[BUTTERFILT_ORD + 1];
    
    // Shift dei campioni
    for (i = BUTTERFILT_ORD; i > 0; --i) {
        in[i] = in[i - 1];
        out[i] = out[i - 1];
    }
    in[0] = cur;

    // Calcola valore filtrato
    retval = 0;
    for (i = 0; i < BUTTERFILT_ORD + 1; ++i) {
        retval += in[i] * b[i];
        if (i > 0)
            retval -= out[i] * a[i];
    }
    out[0] = retval;

    return retval;
}

// Implementazione filtro media mobile
double get_mean_filter(double cur)
{
    double retval;
    static double vec_mean[2];
    
    // Shift dei campioni
    vec_mean[1] = vec_mean[0];
    vec_mean[0] = cur;

    // Calcola valore filtrato
    if (first_mean == 0) {
        retval = vec_mean[0];
        first_mean++;
    } else {
        retval = (vec_mean[0] + vec_mean[1]) / 2;
    }
    
    return retval;
}