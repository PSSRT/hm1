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

#define SIG_SAMPLE SIGRTMIN
#define SIG_HZ 1.0
#define OUTFILE "signal.txt"
#define T_SAMPLE 20000


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

static int first_mean=0;

double get_butter(double cur, double * a, double * b);
double get_mean_filter(double cur);

/* Global flags reflecting the command line parameters */
int flag_signal = 0;
int flag_noise = 0;
int flag_filtered = 0;

//dichiarazione variabili per le risorse condivise
double sig_noise;
double sig_val;
double sig_filt;

pthread_mutex_t mutex_noise;
pthread_mutex_t mutex_val;
pthread_mutex_t mutex_filter;


void* generation (void * parameter)
{
    //si usa la libreria pthread.h per creare generare il thread
    periodic_thread *gen = (periodic_thread *) parameter;//non necessario 'struct' perche nel file rt-lib.h è definita come typedef
    start_periodic_timer(gen, gen->period);
    // Generate signal
    const double Ts = T_SAMPLE/1e6;         //sample time in secondi
    double t = 0.0;

    while(1){
        wait_next_activation(gen);
        
        pthread_mutex_lock(&mutex_val);//wait sulla risorsa sig_val
        sig_val = sin(2*M_PI*SIG_HZ*t);

        // Add noise to signal
        pthread_mutex_lock(&mutex_noise);//wait sulla risorsa sig_noise

        sig_noise = sig_val + 0.5*cos(2*M_PI*10*t);
        sig_noise += 0.9*cos(2*M_PI*4*t);
        sig_noise += 0.9*cos(2*M_PI*12*t);
        sig_noise += 0.8*cos(2*M_PI*15*t);
        sig_noise += 0.7*cos(2*M_PI*18*t);

        pthread_mutex_unlock(&mutex_noise); //signal sulla risorsa sig_noise
        pthread_mutex_unlock(&mutex_val);//signal sulla risorsa sig_val

        t += Ts; /* Sampling period in s */
        
    }	
}



void* filtering (void * parameter)
{
    periodic_thread *filter= (periodic_thread *) parameter;
    start_periodic_timer(filter, filter->period);

    // Filtering signal
    while(1){
        wait_next_activation(filter);
        
        pthread_mutex_lock(&mutex_noise);//wait sulla risorsa sig_noise
        pthread_mutex_lock(&mutex_filter);//wait sulla risorsa sig_filt
        //sig_filt = get_butter(sig_noise, a, b);
	    sig_filt = get_mean_filter(sig_noise);
        pthread_mutex_unlock(&mutex_filter);//signal sulla risorsa sig_filt
        pthread_mutex_unlock(&mutex_noise); //signal sulla risorsa sig_noise
    }	
}


int main()
{
    //implemantazione dei mutex, si usa come protocollo il Priority ceiling
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


    // distruzione attributo dei mutex
    pthread_mutexattr_destroy(&mymutexattr);

    //implementazione dei thread
    pthread_t th_gen;       //thd1 = th_gen
    pthread_t th_filter;

    periodic_thread TH_gen;
    periodic_thread TH_filter;

    //IMPLEMENTAZIONE THREAD
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

    while(1) {
        if(getchar()=='q') break;
    }

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

