#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TICK_PERIOD 5000
#define NSEC_PER_SEC 1000000000ULL

/* periodic thread */
struct periodic_thread {
	int index;
	struct timespec r;
	int period;
	int wcet;
	int priority;
};

/* encoder */
struct enc_str
{
	unsigned int slit;		//valori oscillanti tra 0 e 1
	unsigned int home_slit;	//1 se in home, 0 altrimenti
	pthread_mutex_t lock;
};
static struct enc_str enc_data;

struct _cound_time_str{
	unsigned int count;
	unsigned long int time_diff;
	pthread_mutex_t lock;
};
static struct _cound_time_str count_time_data;

struct _slack_str{
	unsigned long int slack_time_task1;
	unsigned long int slack_time_task2;
	pthread_mutex_t lock;
};
static struct _slack_str slack_data;



/***************************** Functions ************************************/

static inline void timespec_add_us(struct timespec *t, uint64_t d)
{
    d *= 1000;
    t->tv_nsec += d;
    t->tv_sec += t->tv_nsec / NSEC_PER_SEC;
    t->tv_nsec %= NSEC_PER_SEC;
}

void wait_next_activation(struct periodic_thread * thd)
{
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &(thd->r), NULL);
    timespec_add_us(&(thd->r), thd->period);
}

void start_periodic_timer(struct periodic_thread * thd, uint64_t offs)
{
    clock_gettime(CLOCK_REALTIME, &(thd->r));
    timespec_add_us(&(thd->r), offs);
}

unsigned long int difference_ns(struct timespec *ts1, struct timespec *ts2){
	long int diff_sec, diff_nsec;
	diff_sec =(ts1->tv_sec - ts2->tv_sec);
	diff_nsec = (ts1->tv_nsec - ts2->tv_nsec);
	return diff_sec*NSEC_PER_SEC + diff_nsec;
}

/* return 1 if t1>t2, 0 otherwise*/
int compare_time(struct timespec *t1,struct timespec *t2){
    if(t1->tv_sec > t2->tv_sec){
        return 1;
    }
    else if(t1->tv_sec == t2->tv_sec && t1->tv_nsec > t2->tv_nsec){
        return 1;
    }
    else{
        return 0;
    }
}
/***************************** THREADS ************************************/

void* rt_task1(void* parameter){
	struct periodic_thread *th = (struct periodic_thread *) parameter;
	start_periodic_timer(th,th->period);
	
	pthread_mutex_lock(&count_time_data.lock);
	count_time_data.count = 0;
	pthread_mutex_unlock(&count_time_data.lock);	

	int last_value=0;
	struct timespec finish_time;
	
	while (1) {
		wait_next_activation(th);
		
		pthread_mutex_lock(&enc_data.lock);
		if( last_value == 0 && enc_data.slit == 1){
			last_value = 1;
			
			pthread_mutex_lock(&count_time_data.lock);
			count_time_data.count++;
			pthread_mutex_unlock(&count_time_data.lock);
			
		}
		else if(last_value == 1 && enc_data.slit == 0){
			last_value = 0;
		}
		pthread_mutex_unlock(&enc_data.lock);
		
		/* Slack Time */
		clock_gettime(CLOCK_REALTIME, &finish_time);
		if(compare_time(&(th->r),&finish_time)){
			pthread_mutex_lock(&slack_data.lock);
			slack_data.slack_time_task1 = difference_ns(&(th->r),&finish_time);
			pthread_mutex_unlock(&slack_data.lock);
		}
		else{
			printf("DEADLINE MISS  finish time:%ld s %ld ns\t  deadline:%ld s %ld ns\n", finish_time.tv_sec,finish_time.tv_nsec,(th->r).tv_sec,(th->r).tv_nsec);
		}
	}
}

void* rt_task2(void* parameter){
	struct periodic_thread *th = (struct periodic_thread *) parameter;
	start_periodic_timer(th,th->period);
	
	struct timespec time_home;
	struct timespec last_time_home;
	
	int first_measure = 1;
	int last_home_slit = 0;
	
	struct timespec finish_time;
		
	while (1) {
		wait_next_activation(th);
		
		pthread_mutex_lock(&enc_data.lock);
		
		if(enc_data.home_slit == 1 && last_home_slit == 0){
			last_home_slit = 1;
			if(first_measure){
				clock_gettime(CLOCK_REALTIME, &last_time_home);
				first_measure = 0;
			}
			else{	
				clock_gettime(CLOCK_REALTIME, &time_home);
					
				pthread_mutex_lock(&count_time_data.lock);
				count_time_data.time_diff = difference_ns(&time_home,&last_time_home);
				pthread_mutex_unlock(&count_time_data.lock);
			
				last_time_home = time_home;	
			}
		}
		else if(enc_data.home_slit == 0){
			last_home_slit = 0;
		}
		
		pthread_mutex_unlock(&enc_data.lock);
		
		/* Slack Time */
		clock_gettime(CLOCK_REALTIME, &finish_time);
		if(compare_time(&(th->r),&finish_time)){
			pthread_mutex_lock(&slack_data.lock);
			slack_data.slack_time_task2 = difference_ns(&(th->r),&finish_time);
			pthread_mutex_unlock(&slack_data.lock);
		}
		else{
			printf("DEADLINE MISS  finish time:%ld s %ld ns\t  deadline:%ld s %ld ns\n", finish_time.tv_sec,finish_time.tv_nsec,(th->r).tv_sec,(th->r).tv_nsec);
		}
		
	}	
}

void* scope(void* parameter){
	struct periodic_thread *th = (struct periodic_thread *) parameter;
	start_periodic_timer(th,th->period);	
	
	unsigned int count=0;
	float diff_us = 0;
	unsigned int rpm = 0;
	
	while (1) {
		wait_next_activation(th);
		
		pthread_mutex_lock(&count_time_data.lock);
		count = count_time_data.count;
		diff_us = count_time_data.time_diff / 1000;			//difference in microseconds
		pthread_mutex_unlock(&count_time_data.lock);
		
		printf("Rising Edge Counter : %d\t",count);
		
		rpm = (unsigned int)(60*1000000/diff_us);
		
		//printf("diff : %f\t",diff_us);				//DEBUG
		printf("RPM : %u\n",rpm);	
	}
	
}

void* diagnostic(void* parameter){
	struct periodic_thread *th = (struct periodic_thread *) parameter;
	start_periodic_timer(th,th->period);	
	
	unsigned long int avg_slack=0;
	int i = 0;
	int rounds = 10;
	
	while (1) {
		wait_next_activation(th);
			
		pthread_mutex_lock(&slack_data.lock);
		
		avg_slack += (slack_data.slack_time_task1 + slack_data.slack_time_task2)/2000; 	//average in microseconds

		pthread_mutex_unlock(&slack_data.lock);
		i++;
		if(i == rounds){
			avg_slack = avg_slack/rounds;
			printf("**********SLACK TIME: %ld us**********\n",avg_slack);
			i = 0;
		}
		
	}
}

void* enc(void* parameter){
	printf("Encoder Start\n");
	
	pthread_mutex_lock(&enc_data.lock);
	
	enc_data.slit = 0;
	enc_data.home_slit = 0;
	
	pthread_mutex_unlock(&enc_data.lock);
	
	
	unsigned int count = 0;
	unsigned int slit_count = 0;		
	unsigned int prev_slit = 0;
	
	/* Randomized period (75-750 RPM) */
	srand(time(NULL));
	unsigned int semi_per = (rand() % 10) + 1;	
	//semi_per = 5;								//DEBUG
	
	struct periodic_thread *th = (struct periodic_thread *) parameter;
	start_periodic_timer(th,TICK_PERIOD);
	
	while (1) {
		wait_next_activation(th);
		
		pthread_mutex_lock(&enc_data.lock);
		
		prev_slit = enc_data.slit;
		if (count%semi_per == 0) {
			enc_data.slit++;
			enc_data.slit%=2;
		}

		if (prev_slit==0&&enc_data.slit==1) 					//fronte di salita
			slit_count=(++slit_count)%8;

		if (slit_count==0) enc_data.home_slit=enc_data.slit;
		else enc_data.home_slit=0;

		//printf("%d:\t\t %d %d\n",count,enc_data.slit,enc_data.home_slit);	//DEBUG encoder
		count++;
		
		pthread_mutex_unlock(&enc_data.lock);

	}
	
}

int main(){
	struct periodic_thread th_e;
	struct periodic_thread th_rt1;
	struct periodic_thread th_rt2;
	struct periodic_thread th_buddy;
	struct periodic_thread th_d;
	
	pthread_t thread_enc;
	pthread_t thread_rt1;
	pthread_t thread_rt2;
	pthread_t thread_buddy;
	pthread_t thread_diagn;
	
	pthread_attr_t myattr;
	struct sched_param myparam;
	
	pthread_mutexattr_t mutex_attr;
	
	/* Mutex */
	pthread_mutexattr_init (&mutex_attr);
	pthread_mutexattr_setprotocol (&mutex_attr, PTHREAD_PRIO_INHERIT);	// can be PTHREAD_PRIO_NONE, PTHREAD_PRIO_INHERIT, PTHREAD_PRIO_PROTECT
	//pthread_mutexattr_setprioceiling(&mutex_attr, pceiling);		// if PTHREAD_PRIO_PROTECT mutex (set pceiling)
	pthread_mutex_init(&enc_data.lock,&mutex_attr);
	pthread_mutex_init(&count_time_data.lock,&mutex_attr);
	pthread_mutex_init(&slack_data.lock,&mutex_attr);
	
	
	/* Encoder periodic thread */
	th_e.index = 1;
	th_e.period = TICK_PERIOD;
	th_e.priority = 11;
	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	myparam.sched_priority = th_e.priority;
	pthread_attr_setschedparam(&myattr, &myparam);  
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED);
	
	pthread_create(&thread_enc, &myattr, enc, &th_e);
	
	
	/* RT1 periodic thread */
	th_rt1.index = 2;
	th_rt1.period = TICK_PERIOD/2;
	th_rt1.priority = 12;
	myparam.sched_priority = th_rt1.priority;
	pthread_attr_setschedparam(&myattr, &myparam);  	
	 
	pthread_create(&thread_rt1, &myattr, rt_task1, &th_rt1);
	
	
	/* RT2 periodic thread */
	th_rt2.index = 3;
	th_rt2.period = TICK_PERIOD/2;
	th_rt2.priority = 12;
	myparam.sched_priority = th_rt2.priority;
	pthread_attr_setschedparam(&myattr, &myparam);  	
	 
	pthread_create(&thread_rt2, &myattr, rt_task2, &th_rt2);
	
	
	/* buddy thread */
	th_buddy.index = 4;
	th_buddy.period = TICK_PERIOD*100;
		
	pthread_create(&thread_buddy,NULL,scope,&th_buddy);
	
	/* Diagnostic periodic thread */
	th_d.index = 5;
	th_d.period = TICK_PERIOD*100;
	th_d.priority = 10;
	myparam.sched_priority = th_d.priority;
	pthread_attr_setschedparam(&myattr, &myparam);  	
	 
	pthread_create(&thread_diagn, &myattr, diagnostic, &th_d);
	
	
	/* Clear attr*/
	pthread_mutexattr_destroy(&mutex_attr);
	pthread_attr_destroy(&myattr);
	
	/* Wait user exit commands*/
	while (1) {
   		if (getchar() == 'q') break;
  	}
	
	
	//printf("rising edge: %d\n",rising_edge.count);		//DEBUG
	//printf("time_diff: %ld\n",round_time.time_diff);		//DEBUG
	
  	/* Clear */
	pthread_mutex_destroy(&enc_data.lock); 
  	pthread_mutex_destroy(&count_time_data.lock);
  	pthread_mutex_destroy(&slack_data.lock);
  	
  	pthread_cancel(thread_enc);
  	pthread_cancel(thread_rt1);
  	pthread_cancel(thread_rt2);
  	pthread_cancel(thread_diagn);
  	pthread_join(thread_enc,NULL);
  	pthread_join(thread_rt1,NULL);
  	pthread_join(thread_rt2,NULL);
  	pthread_join(thread_diagn,0);
  	
  	printf("EXIT!\n");

	return 0;
}
