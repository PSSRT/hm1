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
#define SIG_HZ 1.0
#define OUTFILE "signal.txt"
#define T_SAMPLE 20000 // 50 Hz

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

mqd_t my_queue;


//variabili per le risorse condivise
double sig_noise;
double sig_val;
double sig_filt;
double t = 0.0; 

pthread_mutex_t mutex_noise;
pthread_mutex_t mutex_val;
pthread_mutex_t mutex_filter;
pthread_mutex_t mutex_time;

//PROTOTIPI DI FUNZIONI
double get_butter(double cur, double * a, double * b);
double get_mean_filter(double cur);

static int first_mean=0;

void* generation (void * parameter)
{
    //si usa la libreria pthread.h per creare generare il thread
    periodic_thread *gen = (periodic_thread *) parameter;//non necessario 'struct' perche nel file rt-lib.h è definita come typedef
    start_periodic_timer(gen, gen->period);
    //Generate signal
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
        
        pthread_mutex_lock(&mutex_time);
        t += Ts; /* Sampling period in s */
        pthread_mutex_unlock(&mutex_time);

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
    
        printf("Sono del thread filtering \n");
        // 4) invio UN messaggio che contiene tutti i campi
        if (mq_send(my_queue, (const char*)&msg, sizeof(msg), 0) == -1) {
            perror("mq_send");
            // se apri O_NONBLOCK e la coda è piena -> errno == EAGAIN
            exit(EXIT_FAILURE);
        }
    }	
}


int main()
{
    //---------------IMPLEMENTAZIONE DEI MUTEX----------------------- 
    // si usa come protocollo il Priority ceiling
    int ceiling = 80; // si setta il ceiling al massimo delle priorità tra tutti i thread che possono prendere quel mutex
    pthread_mutexattr_t mymutexattr; //attributo generale per una variabile mutex 
    pthread_mutexattr_init(&mymutexattr); //inizializazzione della variabile
    pthread_mutexattr_setprotocol(&mymutexattr, PTHREAD_PRIO_PROTECT);
    pthread_mutexattr_setprioceiling(&mymutexattr, ceiling);

    //inizializzazione del mutex per la variabile sig_noise
    pthread_mutex_init(&mutex_noise, &mymutexattr);
    //inizializzazione del mutex per la variabile sig_val
    pthread_mutex_init(&mutex_val, &mymutexattr);
    //inizializzazione del mutex per la variabile sig_filter
    pthread_mutex_init(&mutex_filter, &mymutexattr);
    //inizializzazione del mutex per la variabile t
    pthread_mutex_init(&mutex_time, &mymutexattr);

    // distruzione attributo dei mutex
    pthread_mutexattr_destroy(&mymutexattr);

    //---------------IMPLEMENTAZIONE CODA----------------------- 
    //paramentri della coda
    struct mq_attr attr_queue;
    attr_queue.mq_flags = 0;
    attr_queue.mq_maxmsg = MAX_MSG;
    attr_queue.mq_msgsize = sizeof(sample_msg_t);
    printf("%ld", attr_queue.mq_msgsize);
    attr_queue.mq_curmsgs = 0;


    if((my_queue = mq_open(MQ_NAME, O_CREAT | O_WRONLY , 0644, &attr_queue)) == -1){//accesso alla coda in sola scrittura (apertira non bloccante)
        perror("Errore nella creazione e apertura della coda \n");
        exit(1);
    }
    printf("producer mq_open -> %d\n", (int)my_queue);


    //---------------IMPLEMENTAZIONE DEI THREAD----------------------- 
    pthread_t th_gen;       //thd1 = th_gen
    pthread_t th_filter;

    periodic_thread TH_gen;
    periodic_thread TH_filter;

    //implementazione attributo per i thread
    pthread_attr_t attr;
    struct sched_param par;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr,SCHED_FIFO);
    pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);

    //THREAD GENERAZIONE SEGNALE
    TH_gen.index = 1;
    TH_gen.period = T_SAMPLE;
    TH_gen.priority = 80;
    par.sched_priority= TH_gen.priority;
    pthread_attr_setschedparam(&attr,&par);
    pthread_create(&th_gen, &attr, generation, &TH_gen);


    //THREAD FILTRAGGIO SEGNALE
    TH_filter.index = 2;
    TH_filter.period = T_SAMPLE;
    TH_filter.priority = 70;
    par.sched_priority= TH_filter.priority;
    pthread_attr_setschedparam(&attr,&par);
    pthread_create(&th_filter, &attr, filtering, &TH_filter);
    
    //distruzione attributo dei thread 
    pthread_attr_destroy(&attr);

    //pthread_join(th_gen, NULL);//attesa sulla fine dei thread per non chiudere la coda preventivamente
    //pthread_join(th_filter, NULL);
    
    while(1) {
        if(getchar()=='q'){
        printf("Processo terminato con sucesso!\n");
        break;}
    }

    mq_close(my_queue);
    mq_unlink(MQ_NAME);

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

	// Compute filtered value
	if (first_mean == 0){
		retval = vec_mean[0];
		first_mean ++;
	}
	else{
		retval = (vec_mean[0] + vec_mean[1])/2;	
	}
	return retval;
}

