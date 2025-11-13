#include <pthread.h>  //inserimento della libreria pthread per la creazione di thread periodici
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
#include <errno.h> //per il controllo degli errorri
#include <mqueue.h> //inclusione della libreria per la creazione e gestione di code per la comunicazione tar filter.c e store.c

#define SIG_SAMPLE SIGRTMIN
#define N_SAMPLES 50
#define SIG_HZ 1.0
#define T_SAMPLE 20000      //50 Hz

#define USAGE_STR				\
	"Usage: %s [-s] [-n] [-f]\n"		\
	"\t -s: plot original signal\n"		\
	"\t -n: plot noisy signal\n"		\
	"\t -f: plot filtered signal\n"		\
	""
	
// 2nd-order Butterw. filter, cutoff at 2Hz @ fc = 50Hz
#define BUTTERFILT_ORD 2
double b [3] = {0.0134,    0.0267,    0.0134};
double a [3] = {1.0000,   -1.6475,    0.7009};

//Variabili per la coda
#define MQ_NAME "/print_q" //nome della coda
#define MAX_MSG 10 //massimo numero di messaggi 

typedef struct{
    struct timespec ts;
    double t;
    double val;
    double noise;
    double filt;
}sample_msg_t;

mqd_t queue_sig;

static int first_mean=0;

double get_butter(double cur, double * a, double * b);
double get_mean_filter(double cur);

//Variabili per il calcolo di mse
// #define MSE_QUEUE_NAME "/mse_q"
// #define T_SAMPLE_MSE 1000000      //1 Hz
// #define MAX_MSG_MSE 8 //massimo numero di messaggi
// #define MAX_MSG_SIZE 256
// mqd_t queue_mse;


//dichiarazione variabili per le risorse condivise
double sig_noise;
double sig_val;
double sig_filt;
double t=0.0;

// double sig_original[N_SAMPLES];
// int idx_sig_og = 0;
// double sig_filtered[N_SAMPLES];
// int idx_sig_filt = 0;

pthread_mutex_t mutex_noise;    //-> protegge la risorsa condivisa sig_noise
pthread_mutex_t mutex_val;      //-> protegge la risorsa condivisa sig_val
pthread_mutex_t mutex_filter;   //-> protegge la risorsa condivisa sig_filt
pthread_mutex_t mutex_time;     //-> protegge la risorsa condivisa t
//pthread_mutex_t mutex_mse;      //-> protegge le risorse condivise sig_original e sig_filtered che sono due buffer da 50 campioni



void* generation (void * parameter)
{
    //si usa la libreria pthread.h per creare generare il thread
    periodic_thread *gen = (periodic_thread *) parameter;//non necessario 'struct' perche nel file rt-lib.h è definita come typedef
    start_periodic_timer(gen, gen->period);
    // Generate signal
    const double Ts = T_SAMPLE/1e6;         //sample time in secondi    

    while(1){
        wait_next_activation(gen);
        double t_local;
        pthread_mutex_lock(&mutex_time);
        t_local = t; /* Sampling period in s */
        pthread_mutex_unlock(&mutex_time);

        double sig_val_local = sin(2*M_PI*SIG_HZ*t_local);//creo variabile local in modo tale che il lock resti tenuto per il minimo indispensabile

        pthread_mutex_lock(&mutex_val);//wait sulla risorsa sig_val
        sig_val = sig_val_local; //assegno alla risorsa condivisa ail valore della variabile locale
        pthread_mutex_unlock(&mutex_val);//signal sulla risorsa sig_val

        // Add noise to signal
        double sig_noise_local;//creo variabile local in modo tale che il lock resti tenuto per il minimo indispensabile

        sig_noise_local = sig_val_local + 0.5*cos(2*M_PI*10*t_local);
        sig_noise_local += 0.9*cos(2*M_PI*4*t_local);
        sig_noise_local += 0.9*cos(2*M_PI*12*t_local);
        sig_noise_local += 0.8*cos(2*M_PI*15*t_local);
        sig_noise_local += 0.7*cos(2*M_PI*18*t_local);

        pthread_mutex_lock(&mutex_noise);//wait sulla risorsa sig_noise
        sig_noise = sig_noise_local;//assegno alla risorsa condivisa ail valore della variabile locale
        pthread_mutex_unlock(&mutex_noise); //signal sulla risorsa sig_noise

        //salva il segnale originale nel buffer 
        // pthread_mutex_lock(&mutex_mse);  //wait sul buffer
        // sig_original[idx_sig_og % N_SAMPLES] = sig_val_local;
        // idx_sig_og = (idx_sig_og + 1) % N_SAMPLES;
        // pthread_mutex_unlock(&mutex_mse);    //signal sul buffer

        
        pthread_mutex_lock(&mutex_time);
        t += Ts; /* Sampling period in s */
        pthread_mutex_unlock(&mutex_time);
        printf("Thread 1 in corso\n");
    }	
}

void* filtering (void * parameter)
{
    periodic_thread *filter= (periodic_thread *) parameter;
    start_periodic_timer(filter, filter->period);

    // Filtering signal
    while(1){
        wait_next_activation(filter);
        double sig_noise_local;//creo variabile local in modo tale che il lock resti tenuto per il minimo indispensabile
        pthread_mutex_lock(&mutex_noise);//wait sulla risorsa sig_noise
        sig_noise_local = sig_noise;//assegno alla risorsa condivisa ail valore della variabile locale
        pthread_mutex_unlock(&mutex_noise); //signal sulla risorsa sig_noise

        double sig_filt_local = get_mean_filter(sig_noise_local);//creo variabile local in modo tale che il lock resti tenuto per il minimo indispensabile
        pthread_mutex_lock(&mutex_filter);//wait sulla risorsa sig_filt
        //sig_filt = get_butter(sig_filt_local, a, b);
	    sig_filt = sig_filt_local;//assegno alla risorsa condivisa ail valore della variabile locale
        pthread_mutex_unlock(&mutex_filter);//signal sulla risorsa sig_filt

        //salva il segnale originale nel buffer 
        // pthread_mutex_lock(&mutex_mse);  //wait sul buffer
        // sig_filtered[idx_sig_filt % N_SAMPLES] = sig_filt_local;
        // idx_sig_filt = (idx_sig_filt + 1) % N_SAMPLES;
        // pthread_mutex_unlock(&mutex_mse);    //signal sul buffer

        //creazione del messaggio da mandare nella coda 
        sample_msg_t msg;
        clock_gettime(CLOCK_REALTIME, &msg.ts);  // o CLOCK_MONOTONIC, come preferisci
 
        pthread_mutex_lock(&mutex_time);
        msg.t = t;
        pthread_mutex_unlock(&mutex_time);
    
        pthread_mutex_lock(&mutex_val);
        msg.val = sig_val;
        pthread_mutex_lock(&mutex_noise);
        msg.noise = sig_noise;
        pthread_mutex_lock(&mutex_filter);
        msg.filt = sig_filt;
        pthread_mutex_unlock(&mutex_filter);
        pthread_mutex_unlock(&mutex_noise);
        pthread_mutex_unlock(&mutex_val);
    
        printf("Thread 2 in corso\n");
        // 4) invio UN messaggio che contiene tutti i campi
        if (mq_send(queue_sig, (const char*)&msg, sizeof(msg), 0) == -1) {
            perror("mq_send"); // se apri O_NONBLOCK e la coda è piena -> errno == EAGAIN
            exit(EXIT_FAILURE);
        }
    }
}


// void* calculate_mse (void * parameter)
// {
//     periodic_thread *mse= (periodic_thread *) parameter;
//     start_periodic_timer(mse, mse->period);

//     double local_original[N_SAMPLES];
//     double local_filtered[N_SAMPLES];

//     while(1)
//     {
//         wait_next_activation(mse);
//         pthread_mutex_lock(&mutex_mse);
//         memcpy(local_original, sig_original, sizeof(sig_original));
//         memcpy(local_filtered, sig_filtered, sizeof(sig_filtered));
//         pthread_mutex_unlock(&mutex_mse);

//         double mse_val = 0.0;
//         double diff;
//         for (int i=0; i<N_SAMPLES; i++){
//             diff = local_original[i] - local_filtered[i];
//             mse_val = mse_val + diff*diff;
//         }
//         mse_val = mse_val/N_SAMPLES;

//         printf("Thread 3 in corso\n");

//         //sending the message on the queue
//         char msg[MAX_MSG_SIZE];
//         snprintf(msg,sizeof(msg),"%f",mse_val);
//         printf("%s\n", msg);
//         if(mq_send(queue_mse,msg,strlen(msg)+1,0) == -1){
//             perror("calculate_mse:mq_send");
//             exit(EXIT_FAILURE);
//         }
//     }
// }

int main()
{
    //implementazione dei mutex, si usa come protocollo il Priority ceiling
    int ceiling = 80; // si setta il ceiling al massimo delle priorità tra tutti i thread che possono prendere quel mutex
    pthread_mutexattr_t mymutexattr; //attributo generale per una variabile mutex 
    pthread_mutexattr_init(&mymutexattr); //inizializazzione della variabile
    pthread_mutexattr_setprotocol(&mymutexattr, PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mymutexattr, ceiling);

    pthread_mutex_init(&mutex_noise, &mymutexattr);   //inizializzazione del mutex per la variabile sig_noise
    pthread_mutex_init(&mutex_val, &mymutexattr);     //inizializzazione del mutex per la variabile sig_val
    pthread_mutex_init(&mutex_filter, &mymutexattr);  //inizializzazione del mutex per la variabile sig_filter
    pthread_mutex_init(&mutex_time, &mymutexattr);    //inizializzazione del mutex per la variabile t
    //pthread_mutex_init(&mutex_mse, &mymutexattr);     //inizializzazione del mutex per la variabile mse


    // distruzione attributo dei mutex
    pthread_mutexattr_destroy(&mymutexattr);

    
    //---------------IMPLEMENTAZIONE CODA SEGNALI----------------------- 
    //paramentri della coda
    struct mq_attr attr_queue;
    attr_queue.mq_flags = 0;
    attr_queue.mq_maxmsg = MAX_MSG;
    attr_queue.mq_msgsize = sizeof(sample_msg_t);   //48 byte
    attr_queue.mq_curmsgs = 0;


    if((queue_sig = mq_open(MQ_NAME, O_CREAT | O_WRONLY , 0644, &attr_queue)) == -1){//accesso alla coda in sola scrittura (apertura non bloccante)
        perror("Errore nella creazione e apertura della coda del segnale \n");
        exit(1);
    }

    printf("coda del segnale creata con successo!");

    //---------------IMPLEMENTAZIONE CODA MSE----------------------- 
    //paramentri della coda
    // struct mq_attr attr_queue_mse;
    // attr_queue_mse.mq_flags = 0;
    // attr_queue_mse.mq_maxmsg = MAX_MSG_MSE;
    // attr_queue_mse.mq_msgsize = MAX_MSG_SIZE;      //256 byte
    // attr_queue_mse.mq_curmsgs = 0;


    // if((queue_mse = mq_open(MSE_QUEUE_NAME, O_CREAT | O_WRONLY , 0660, &attr_queue_mse)) == -1){//accesso alla coda in sola scrittura (apertura non bloccante)
    //     perror("Errore nella creazione e apertura della coda dell'errore quadratico medio\n");
    //     exit(1);
    // }

    // printf("coda dell'errore quadratico medio creata con successo!");
    
    //implementazione dei thread
    pthread_t th_gen;       //thd1 = th_gen
    pthread_t th_filter;
    //pthread_t th_mse;

    periodic_thread * TH_gen = malloc(sizeof(periodic_thread));
    periodic_thread * TH_filter = malloc(sizeof(periodic_thread));    
    //periodic_thread * TH_mse = malloc(sizeof(periodic_thread));

    //IMPLEMENTAZIONE THREAD
    pthread_attr_t attr;
    struct sched_param par;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr,SCHED_FIFO);
    pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);

    //THREAD GENERAZIONE SEGNALE
    TH_gen->index = 1;
    TH_gen->period = T_SAMPLE;
    TH_gen->priority = 80;
    par.sched_priority= TH_gen->priority;
    pthread_attr_setschedparam(&attr,&par);
    pthread_create(&th_gen, &attr, generation, TH_gen);


    //THREAD FILTRAGGIO SEGNALE
    TH_filter->index = 2;
    TH_filter->period = T_SAMPLE;
    TH_filter->priority = 80;
    par.sched_priority= TH_filter->priority;
    pthread_attr_setschedparam(&attr,&par);
    pthread_create(&th_filter, &attr, filtering, TH_filter);

    
    //THREAD CALCOLO MSE
    // TH_mse->index = 2;
    // TH_mse->period = T_SAMPLE_MSE;
    // TH_mse->priority = 60;
    // par.sched_priority= TH_mse->priority;
    // pthread_attr_setschedparam(&attr,&par);
    // pthread_create(&th_mse, &attr, calculate_mse, TH_mse);
    
    //distruzione attributo dei thread 
    pthread_attr_destroy(&attr);
    while(1) {
        if(getchar()=='q'){
            printf("Processo terminato con sucesso!\n");
            break;
        }
    }

    mq_close(queue_sig);
    mq_unlink(MQ_NAME);

    // mq_close(queue_mse);
    // mq_unlink(MSE_QUEUE_NAME);
    return 0;
}



double get_butter(double cur, double * a, double * b)
{
	double retval;
	int i;

	static double in[BUTTERFILT_ORD+1];
	static double out[BUTTERFILT_ORD+1];
	
	// Perform sample shift
	for (i = BUTTERFILT_ORD; i > 0; --i) {
		in[i] = in[i-1];
		out[i] = out[i-1];
	}
	in[0] = cur;

	// Compute filtered value
	retval = 0;
	for (i = 0; i < BUTTERFILT_ORD+1; ++i) {
		retval += in[i] * b[i];
		if (i > 0)
			retval -= out[i] * a[i];
	}
	out[0] = retval;

	return retval;
}

double get_mean_filter(double cur)
{
	double retval;

	static double vec_mean[2];
	
	// Perform sample shift
	vec_mean[1] = vec_mean[0];
	vec_mean[0] = cur;

	//printf("in[0]: %f, in[1]: %f\n", in[0], in[1]); //DEBUG
	if (first_mean == 0){
		retval = vec_mean[0];
		first_mean ++;
	}
	else{
		retval = (vec_mean[0] + vec_mean[1])/2;	
	}
	return retval;
}
