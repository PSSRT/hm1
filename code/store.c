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

#define T_SAMPLE 20000      //50 Hz
#define OUTFILE "signal.txt"

/* Global flags reflecting the command line parameters */
int flag_signal = 0;
int flag_noise = 0;
int flag_filtered = 0;

void parse_cmdline(int argc, char ** argv);

void * storage (void * parameter)
{
    periodic_thread *store = (periodic_thread *) parameter;//non necessario 'struct' perche nel file rt-lib.h è definita come typedef
    start_periodic_timer(store, store->period);
    
    int outfile;
    FILE * outfd;

    // Command line input parsing
	parse_cmdline(argc, argv);

    // File Opening
	outfile = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	outfd = fdopen(outfile, "w");
    
	if (outfile < 0 || !outfd) {
		perror("Unable to open/create output file. Exiting.");
		return EXIT_FAILURE;
	}

    while(1){
        wait_next_activation(store);
        
        // Write values in txt file
        fprintf(outfd, "%lf,", glob_time);
        
        if (flag_signal)
            fprintf(outfd, "%lf,", sig_val);
        else
            fprintf(outfd, ",");
        
        if (flag_noise)
            fprintf(outfd, "%lf,", sig_noise);
        else
            fprintf(outfd, ",");

        if (flag_filtered)
            fprintf(outfd, "%lf,", sig_filt);
        else
            fprintf(outfd, ",");

        fprintf(outfd, "\n");
        fflush(outfd);
        
        // Debug
        printf("glob_time: %lf, sig: %lf, sig_noise: %lf, sig_filter: %lf\n", glob_time, sig_val, sig_noise, sig_filt);


    }
}

int main()
{
    pthread_t th_store;
    periodic_thread TH_store;

    pthread_attr_t attr;
    struct sched_param par;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr,SCHED_FIFO);
    pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);

    //THREAD STORAGE
    TH_gen.index = 1;
    TH_gen.period = T_SAMPLE;
    TH_gen.priority = 80;
    par.sched_priority= TH_gen.priority;
    pthread_attr_setschedparam(&attr,&par);
    pthread_create(&th_store, &attr, storage, &TH_store);

    return 0;
}

void parse_cmdline(int argc, char ** argv)
{
	int opt;
	
	while ((opt = getopt(argc, argv, "snf")) != -1) {
		switch (opt) {
		case 's':
			flag_signal = 1;
			break;
		case 'n':
			flag_noise = 1;
			break;
		case 'f':
			flag_filtered = 1;
			break;
		default: /* '?' */
			fprintf(stderr, USAGE_STR, argv[0]);
			exit(EXIT_FAILURE);
		}
	}
	
	if ((flag_signal | flag_noise | flag_filtered) == 0)
	{
		flag_signal = flag_noise = flag_filtered = 1;
	}
}


/*      int outfile;
        FILE * outfd;
        
        int f_sample = F_SAMPLE; // Frequency of sampling in Hz 
        double t_sample = (1.0/f_sample) * 1000 * 1000 * 1000; // Sampling period in ns 

        // Command line input parsing
        parse_cmdline(argc, argv);
        
        // File Opening
        outfile = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        outfd = fdopen(outfile, "w");
        
        if (outfile < 0 || !outfd) {
            perror("Unable to open/create output file. Exiting.");
            return EXIT_FAILURE;
        }
*/