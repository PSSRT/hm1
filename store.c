#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

/*  
    store.c rappresenta il server
    
    il server avrà una coda /print_q di 10 elementi 
    la coda può essere fatta con posix
    ad ogni periodo preleva 10 elementi dalla coda e li scrive su signal.txt
    tutto questo avviene all'interno di un task periodico con f = 5Hz

    creo un thread periodico, con i mutex e i parametri necessari, per store_task
    forse non serve che sia un thread? capiamo,
        nell'esempio del server il prof non usa thread ma comunque non lavora su risorse condivise
        serve che sia un thread per usare mutex? si --> dev'essere un thread. bravo paolo
    
    per la coda serve una struct mqdes, da aprire con mq_open
    si possono settare gli attributi della coda mq_attr, non fondamentale
    riceve i messaggi con mq_receive
    mq_notify può essere usato per stampare a video la ricezione credo, capiamo

    una volta ricevuti i messaggi dal client (filter.c), li scrive nel file signal.txt, 10 per volta
    a sto punto live_plot.py li stampa a video, 
    per il corretto funzionamento verificare la formattazione dei segnali scritti nel file di testo

*/



// Dichiarazioni variabili per il server
#define SERVER_QUEUE_NAME   "/print:_q"
#define QUEUE_PERMISSIONS 0660      //quali sono i permessi da mettere?
#define MAX_MESSAGES 10             
#define MAX_MSG_SIZE 256            //quanto devono essere grandi i messaggi?

// Dichiarazioni delle variabili condivise definite in filter.c
extern struct {
    double time;
    double noisy;
    double filtered;
} print_q[];

//implementazione senza posix fatta da jack e clauds e rock and roll
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

/*
int main() {
    pthread_t store_tid;
    pthread_create(&store_tid, NULL, store_task, NULL);
    pthread_join(store_tid, NULL);
    return 0;
}
*/


// implementazione dall'esempio del prof (copiato e incollato)
int main (int argc, char **argv)
{
    mqd_t qd_server, qd_client;  	// descrittori delle code
    long token_number = 1; 	   	// Token passato ad ogni richiesta del CLient

    printf ("Server alive!\n");

    // Vanno definiti gli attributi che andremo ad assegnare alla coda
    struct mq_attr attr;

    attr.mq_flags = 0;				
    attr.mq_maxmsg = MAX_MESSAGES;	
    attr.mq_msgsize = MAX_MSG_SIZE; 
    attr.mq_curmsgs = 0;

    // Apriamo una coda in sola lettura (O_RDONLY) e se non esiste la creiamo (O_CREAT).
    // Il nome dato alla coda è una stringa: "/sp-example-server". Può essere una qualsiasi stringa l'importante è che essa inizi per "/"
    if ((qd_server = mq_open (SERVER_QUEUE_NAME, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
        perror ("Server: mq_open (server)");
        exit (1);
    }
    char in_buffer [MAX_MSG_SIZE];
    char out_buffer [MAX_MSG_SIZE];
    char end []="q";

    while (1) {
        // Ricevo il messaggio più vecchio in coda con priorità più alta
        if (mq_receive (qd_server, in_buffer, MAX_MSG_SIZE, NULL) == -1) {
            perror ("Server: mq_receive");
            exit (1);
        }

        printf ("Server: message received\n");

	    // Verifico la richiesta di chiusura del server
	    if(strncmp(in_buffer,"q", sizeof(in_buffer)) == 0){
		    break;
	    }
	    else {
		    // Rispondo al Client usando una diversa coda. Il nome della coda del client la ricavo dal messaggio inviato dal client stesso
		    if ((qd_client = mq_open (in_buffer, O_WRONLY)) == 1) {
		        perror ("Server: Not able to open client queue");
		        continue;
		    }

		    sprintf (out_buffer, "%ld", token_number);
		    // Invio il token al client	
		    if (mq_send (qd_client, out_buffer, strlen (out_buffer) + 1, 0) == -1) {
		        perror ("Server: Not able to send message to client");
		        continue;
		    }

		    printf ("Server: response sent to client.\n");
		    token_number++;
        }
    }
    
    /* Clear */
    if (mq_close (qd_server) == -1) {
        perror ("Server: mq_close qd_server");
        exit (1);
    }

    if (mq_unlink (SERVER_QUEUE_NAME) == -1) {
        perror ("Server: mq_unlink server queue");
        exit (1);
    }
   
    printf("Server: bye!\n");
}