#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>			           //libreria per le funzioni di gestione dei file
#include <sys/stat.h>
#include <math.h>

#include <time.h>                      //libreria utilizzata per il calcolo del timeout adattivo e del numero randomico
#include <sys/time.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <setjmp.h>			           //libreria per le funzioni di salto tra funzioni

#include <sys/socket.h>		           //definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>		           //definisce i tipi di dato per rappresentare gli indirizzi IP in internet
#include <arpa/inet.h>		           //definisce le funzioni per manipolare gli indirizzi IP
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>			           //libreria per l'implementazione del comando "list"
#include <libgen.h>			           //librerua per l'implementazione del comando "list"

#define standard_port 1024			   //porta iniziale assunta da ogni client che si collega al server
#define maxline 512 				   //dimensione massima dei messaggi inviati e ricevuti
#define N 4						   //dimensione dellafinestra di ricezione
#define exit_code_client 1             //codice da inviare al server affinchè avvenga la chiusura della socket
#define exit_code_server 2             //codice ricevuto dal server affinchè avvenga la chiusura della socket
#define PROB_PERDITA 10                //probabilità di perdita del pacchetto nella rete
#define STATIC_TIMEOUT 5000            //tempo dopo il quale arriva il segnale di errore SIGALRM

//struttura per la gestione del GO-BACK-N
typedef struct message_struct {
	int message_pointer;			   //definisce l'ultimo byte letto dal messaggio
	char message_buffer[maxline];
} message;

int sd;				        		   //socket descriptor
int sd_child;         	 			   //child socket descriptor
int fd;								   //file descriptor (per la funzione put)
int lunghezza_file;                    //quantità di byte di cui si compone il file da trasferi
int len;                               //variabile utilizzata per il calcolo della dimensione della struttura sockaddr
int num_message;                       //numero di messaggi utili per trasferire completamente il file
int port_number = 1025;				   //inizializzato al primo numero di porta disponibile
int value_ack;                         //variabile che tiene conto dell'ultimo byte ricevuto con successo
int chose_timeout;                     //a seconda del suo valore (0 o 1) permette di scegliere il timeout da usare
double ADAPTIVE_TIMEOUT = 1;           //valore iniziale dell'intervallo di timeout modificabile con il calcolo di RTT

char buffer[maxline];				   //buffer per l'inserimento di stringhe di invio/richiesta

struct sockaddr_in sad;			 	   //struttura per l'indirizzo IP locale e numero di porta locale
struct dirent *dp;					   //struttura per la gestione dei file del server

DIR *dir;							   //descrittore del flusso di directory del server

pid_t pid;							   //variabile per la creazione del processo pid
size_t val;                            //variabile per la gestione delle funzioni riguardanti i file
clock_t start_time[N], end_time[N];    //variabili utilizzate per inizializzare e terminare il conteggio del tempo
struct timeval  tv1, tv2, tv3, tv4;        //variabili utilizzate per la valutazione del tempo di esecuzione della funzione
double sample_RTT;                     //variabile contenente la differenza di tempo tra invio/ricezione pacchetto
double estimated_RTT = 0;
double dev_RTT = 0;

void func_list();
void func_put();
void func_get();
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file);
void ricezione_GBN(int file_descriptor);
int create_connection(int port);
void setTimeout(double time, int id, int tempo_scelto);
void invio_ACK(int valore_ack);
void exit_handler(int signo);

int main(int argc, char *argv[]) {
	int result;

    //gestione del segnale collegato all'interruzione del processo per mezzo dell'utente
    signal(SIGINT, exit_handler);

	//creazione della socket per il processo server (usata solo per l'ascolto di richieste)
    sd = create_connection(standard_port);

	//fase di attesa di richieste da parte dei client
	printf("----Server in attesa di richieste----\n");

	while(1) {
		//svuotamento del buffer e calcolo grandessa della struttura sockaddr
		memset(buffer, 0, sizeof(buffer));
		len = sizeof(sad);

		//ricezione dell'esistenza dei nuovi processi client al server
		result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
		if(result == -1) {
			perror("Errore, ricezione richiesta (1) fallita\n");
			exit(-1);
		} 
		else if(result == 0) {    
            //svuotamento del buffer     
            memset(buffer, 0, sizeof(buffer));

            //inserimento del nuovo numero di porta nel buffer per l'invio al client
            sprintf(buffer, "%d", port_number);
            
            //invio al client il nuovo numero di porta 
            result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
                perror("Errore, invio risposta fallito\n");
                exit(-1);
            }
            //creazione del processo figlio per la comunicazione col client
            pid = fork();

			if(pid == -1) {
				perror("Errore, creazione processo figlio fallita\n");
				exit(-1);
			}
			if(pid == 0) {
				//processo figlio in esecuzione
                printf("Processo figlio con pid: %d e numero di porta: %d in esecuzione\n",getpid(), port_number);
				
				//creazione della nuova socket per il processo figlio e chiusura di quella del processo padre
                close(sd);
				sd_child = create_connection(port_number);
                
                //inizio ricezione delle richieste in arrivo dal client
                while(1) {
LOOP_RICHIESTE:
                    memset(buffer, 0, sizeof(buffer));
                    
                    //ricezione del comando da far eseguire al server
                    result= recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                    if(result == -1){
                        printf("Errore, ricezione richiesta (2) fallito\n");
                        exit(-1);
                    }
                    //Controllo che nel buffer non ci sia il codice di uscita
                    else if(atoi(buffer) == exit_code_client) {
                        printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
                        exit(-1);
                    }
                    //comando EXIT (chiusura della socket che mette in relazione i due processi)
                    else if(strcmp("exit", buffer) == 0) {
                        printf("\nClient ha richiesto uscita dalla connessione\n");

                        result = close(sd_child);
                        if(result == -1) {
                            perror("Errore, chiusura connessione fallita\n");
                            exit(-1);
                        }
                        else {
                            printf("----Chiusura della connessione riuscita----\n");
                            memset(buffer, 0, sizeof(buffer));
                            exit(1);
                        }
                    }
                    
                    //comando LIST (invio al client la lista dei file del server)
                    else if(strcmp("list", buffer) == 0) {
                    	printf("\nLista dei file nel server richiesta\n");
                    	func_list();
                    }

                    //comando PUT (salvataggio del file inviato nel server)
                    else if(strcmp("put", buffer) == 0) {
                    	memset(buffer, 0, sizeof(buffer));
    				
    					//ricezione del nome del file da scrivere sul server
                    	result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                    	if(result == -1) {
                    		perror("errore, ricezione richiesta (3) fallita\n");
                    		exit(-1);
                    	}
                        //Controllo che nel buffer non ci sia il codice di uscita
                        else if(atoi(buffer) == exit_code_client){
                            printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
                            exit(1);
                        }
                    	else if(result == 0) {
                    		//caso in cui il file non è presente nel client
                    		goto LOOP_RICHIESTE;
                    	}
                    	else {
                    		//caso in cui il file è presente nel client
                        	printf("\nScrittura di un file sul server richiesta\n");
                        	func_put();
                        }
                    }

                    //comando GET (invio del file richiesto al client)
                    else if(strcmp("get", buffer) == 0) {
                    	memset(buffer, 0, sizeof(buffer));

                        //ricezione della variabile chose_timeout dal client
                        result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                        if(result == -1) {
                            perror("Errore, ricezione variabile chose_timeout fallito\n");
                            exit(-1);
                        }
                        //salvataggio della modalità di tiemout nell'apposita variabile
                        chose_timeout = atoi(buffer);

                        memset(buffer, 0, sizeof(buffer));

                    	//ricezione del nome del file da scrivere sul client
                    	result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                    	if(result == -1) {
                        	perror("Errore, ricezione del nome file fallito\n");
                        	exit(-1);
                    	}
                        //Controllo che nel buffer non ci sia il codice di uscita
                        else if(atoi(buffer) == exit_code_client) {
                            printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
                            exit(1);
                        }
                    	else {
                    		printf("\nLettura di un file del server richiesta\n");
                    		func_get();
                        }
                    }
                    //aspetto un secondo prima di chiudere il socket, attendo di inviare e ricevere tutto
                    sleep(1);

                    //ad ogni richiesta viene aperta e chiusa la connessione con il client
                    close(sd_child);
                    sd_child = create_connection(port_number);

                    goto LOOP_RICHIESTE;
                } 
            }
            else {
            	//processo padre in esecuzione

            	//(aggiornamento) incremento della variabile port_number
            	if(port_number < 65535) {
            		//caso port_number non assume valore massimo consentito
            		port_number++;
            	}
            	else {
            		//caso port_number assume valore massimo consentito
            		port_number = 1025;
            	}
            }
        }
        else {
        	//caso result > 0 (ricezione di un buffer non vuoto)
        	//in questo caso non si riceve la comunicazione della presenza di un nuovo client
            printf("Errore, ricezione presenza del client fallito\n");
            continue; 
        }
    }
    return(0);
}

/*funzione utilizzata per la richiesta LIST. Consiste nel creare uno o più buffer
contenenti i file presente nel server a cui si fa riferimento*/
void func_list() {
	int result;

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));

	//apertura della directory e controllo dei file presenti in essa
	if ((dir = opendir ("server UDP")) == NULL) { 
        perror ("Errore, lettura file nel server fallita\n");
        exit (-1);
    }
	
	//lettura della directory contenente i nomi dei file del server		
	while ((dp = readdir(dir)) != NULL) {
		if(dp->d_type == DT_REG) {

			//inserimento del file i-esimo all'interno del buffer
        	strcat(buffer, dp->d_name);
        	strcat(buffer, "\n");

			if(strlen(buffer) >= (maxline-1)) {
				//quando il buffer è pieno lo si invia al client e si svuota
    			result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
				if(result == -1) {
					perror("Errore, invio risposta (2) fallito\n");
					exit(-1);
				}
				memset(buffer,0,sizeof(buffer));
			}			
    	}        				
    }
    if(strlen(buffer) > 0) {
    	//invio della risposta al client che ne ha fatto richiesta
		result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
		if(result == -1) {
			perror("errore, invio risposta (1) fallito\n");
			exit(-1);
		}
    }
    //svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));

    //chiusura della directory dir
    closedir(dir);

    printf("----Lista dei file inviata al client----\n");
}

/*funzione utilizzata per la richiesta PUT. Permette di inserire un file inviato dal client
all'interno del server, se e solo se il file è effettivamente presente nel client*/
void func_put() {
	int result;

	//creazione del file da ricevere dal client sul server
	fd = open(buffer, O_CREAT|O_RDWR|O_TRUNC, 0666);
	if(fd == -1) {
		perror("Errore, creazione file fallita\n");
		exit(-1);
	}

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    //funzione per la ricezione del file dal client
    ricezione_GBN(fd);

    printf("----File salvato nel SERVER con successo----\n\n");
}

/*funzione utilizzata per la richiesta GET. Permette di trasferire un file richiesto dal client
che è presente all'interno del server*/
void func_get() {
	int result;
	char *pathname;				//variabile per il percorso del file da cercare e inviare

	//allocazione di memoria per la creazione del percorso del file
	pathname = (char *)malloc(124);
	if(pathname == NULL) {
		perror("errore, allocazione di memoria fallita\n");
		exit(-1);
	}

	//apertura della directory e controllo dei file presenti in essa
	if ((dir = opendir ("server UDP")) == NULL) {
    	perror ("Errore, lettura file nel server fallita\n");
        exit (-1);
    }
    //si è inizializzata la variabile result a 0
    //se alla fine result = 0: file non presente nel server
    //se alla fine result = 1: file presente nel server
	result = 0;

	while ((dp = readdir (dir)) != NULL) {				//lettura della directory contenente i nomi dei file
        if(strcmp(dp->d_name, buffer) == 0) {			//controllo se file richiesto è presente nel server

        	//variabile impostata per indicare che il file è presente nel server
        	result = 1;

        	//stringa contenente il percorso del file richiesto dal client
        	strcpy(pathname, "server UDP/");
        	strcat(pathname, dp->d_name);

        	//apertura del file richiesto per la lettura
        	fd = open(pathname, O_RDONLY, 0666);
        	if(fd == -1) {
            	perror("Errore, apertura del file fallita\n");
            	exit(-1);
        	}
        	free(pathname);

        	//svuotamento del buffer
        	memset(buffer, 0, sizeof(buffer));

            //calcolo la lunghezza del file da inviare
            lunghezza_file = lseek(fd, 0, SEEK_END);
            sprintf(buffer, "%d", lunghezza_file);

            //invio la lunghezza del file al client
            result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
            	perror("Errore, invio della lunghezza del file fallito\n");
            	exit(-1);
            }

            //riposizionamento del puntatore all'inizio del file per la lettura
            lseek(fd, 0, SEEK_SET);

            //calcolo il numero di pacchetti da dover inviare al client
            if(lunghezza_file % maxline == 0) {
                //caso in cui il file ha dimensione pari ad un multiplo di maxline
                num_message = (lunghezza_file / maxline);
            }
            else if (lunghezza_file % maxline != 0) {
                //caso in cui il file non ha dimensione pari ad un multiplo di maxline
                num_message = (lunghezza_file / maxline) + 1;
            }

            //inserimento nel buffer del numero di pacchetti da inviare al client
            memset(buffer, 0, sizeof(buffer));
            sprintf(buffer, "%d", num_message);

            //invio il numero di pacchetti al client
            result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
            	perror("Errore, invio del numero di pacchetti fallito\n");
            	exit(-1);
            }

            //svuotamento del buffer
            memset(buffer, 0, sizeof(buffer));

            //allocazione delle variabili per la gestione dei messaggi inviati
            message pack[num_message];			//inizializzazione delle strutture 'message'

            //pulizia dei campi di ogni struttura allocata
        	for(int i = 0; i < num_message; i++) {
            	memset(pack[i].message_buffer, 0, maxline);
            	pack[i].message_pointer = 0;
        	}
            
            //funzione per l'invio dei pacchetti al processo ricevente
            invio_GBN(pack, num_message, fd, lunghezza_file);

            //riposizionamento del puntatore all'interno del file
            lseek(fd, 0, SEEK_SET);

    		//chiusura del descrittore del file e della directory dir
            close(fd);
            closedir(dir);

            goto END;
        }
    }
END:
    if(result == 0) {
        /*caso in cui il file non è presente nel server e viene inviato un buffer vuoto
        per indicare al client la non presenza del file richiesto*/

        printf("File richiesto non presente nel server\n");

        result = sendto(sd_child, NULL, 0, 0, (struct sockaddr *)&sad, sizeof(sad));
        if(result == -1) {
            perror("Errore, invio risposta (3) fallito\n");
            exit(-1);
        }
    }
    //svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
}

/*funzione utilizzata da 'func_put' per inserire il file ricevuto dal client nel server e per inviare al client
informazioni sullo stato degli ack*/
void ricezione_GBN(int file_descriptor) {
    int valore_atteso = 0;              //valore che ci si aspetta dal prossimo pacchetto arrivato affinchè sia in ordine
    int lastAckCorrect = 0;             //valore del byte immediatamente successivo da scaricare
    int prob_rvd_file;                  //probabilità di ricezione del pacchetto dalla rete
    int lastByteReceived = 0;           //valore dell'ultimo byte dell'ultimo messaggio ricevuto
    int result, i;
    int count_retransmit = 0;           //variabile per il conteggio delle ritrasmissioni effettuate
    time_t t;                           //variabile per la generazione del valore randomico della probabilità

    //inizializzazione del genereatore di numeri random
    srand((unsigned) time(&t));

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //ricezione della lunghezza del file da ricevere
    result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
    if(result == -1) {
    	perror("Errore, ricezione lunghezza del file fallito\n");
    	exit(-1);
    }
    //Controllo che nel buffer non ci sia il codice di uscita
    else if(atoi(buffer) == exit_code_client) {
        printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
        exit(1);
    }
    //lunghezza del file da ricevere dal client
    lunghezza_file = atoi(buffer);

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //ricezione del numero di messaggi da ricevere in totale
    result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
    if(result == -1) {
    	perror("Errore, ricezione numero di pacchetti fallito\n");
    	exit(-1);
    }
    //Controllo che nel buffer non ci sia il codice di uscita
    else if(atoi(buffer) == exit_code_client){
        printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
        exit(1);
    }
    else {
    	//salvo nella variabile 'num_message' il numero di messaggi da ricevere
    	num_message = atoi(buffer);

    	//inizializzazione delle num_message strutture da creare
    	message pack[num_message];

    	//svuotamento del buffer
    	memset(buffer, 0, sizeof(buffer));

        //pulizia dei campi di ogni struttura allocata
        for(i = 0; i < num_message; i++) {
            memset(pack[i].message_buffer, 0, maxline);
            pack[i].message_pointer = 0;
        }

		//ricezione dei messaggi dal client
		i = 0;
        
        //inizio conteggio tempo 
        gettimeofday(&tv3, NULL);

		while(i < num_message) {
LOOP:
			//ricezione del contenuto del messaggio i-esimo
			result = recvfrom(sd_child, pack[i].message_buffer, maxline, 0, (struct sockaddr *)&sad, &len);
			if(result == -1) {
				perror("Errore, ricezione messaggio di risposta fallita\n");
				exit(-1);
			}
			else {
				memset(buffer, 0, sizeof(buffer));

				//ricezione dell'ultimo byte di cui si compone il messaggio da ricevere
				result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
				if(result == -1) {
					perror("Errore, ricezione ultimo byte del messaggio fallito\n");
					exit(-1);
				}
                //Controllo che nel buffer non ci sia il codice di uscita
                else if(atoi(buffer) == exit_code_client){
                    printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
                    exit(1);
                }
				else {
					//salvataggio del valore dell'ultimo byte nella variabile dedicata
					lastByteReceived = atoi(buffer);

					printf("\nlastByteReceived: %d\n", lastByteReceived);
                    //valore dell'ultimo byte associato al messaggio i-esimo atteso
					valore_atteso = (maxline * (i + 1));

					if(valore_atteso < lunghezza_file) {
						//caso in cui si ricevono messaggi precedenti all'ultimo

						//controllo esattezza ordine del messaggio ricevuto
						if(lastByteReceived == valore_atteso) {
							//pacchetti arrivato con successo è quello in ordine
							count_retransmit = 0;

							//calcolo probabilità di perdita del pacchetto (mancata scrittura nel file)
							prob_rvd_file = (rand() % 100);

							if(prob_rvd_file < PROB_PERDITA) {
								/*caso in cui si considera il messaggio non arrivato e quindi
								non viene inviato nessun ack di risposta al server*/
								printf("Messaggio n°.%d PERSO\n", i);

								goto ACK_PERSO;
							}
							printf("Messaggio n°.%d ARRIVATO\n", i);

							//inserimento del messaggio i-esimo all'interno del file creato nel client
							val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
							if(val == -1) {
								perror("Errore, scrittura sul file server fallita\n");
								exit(-1);
							}

							//assegnazione del valore del prossimo byte da ricevere alla variabile di ack
							lastAckCorrect = lastByteReceived + 1;

							//invio ack relativo all'ultimo messaggio ricevuto in ordine
							invio_ACK(lastAckCorrect);
							
							//incremento della variabile per la ricezione del successivo messaggio
							i++;

							goto LOOP;
						}
						else {
							//pacchetto arrivato con successo non è quello in ordine
							//si scarta il pacchetto arrivato e si manda al client l'ack del pacchetto desiderato

							if(lastByteReceived % maxline == 0) {
								printf("Messaggio n°.%d SCARTATO\n", (lastByteReceived / maxline) - 1);
							}
							else if(lastByteReceived % maxline > 0) {
								printf("Messaggio n°.%d SCARTATO\n", (lastByteReceived / maxline));
							}
							
							//invio ack relativo all'ultimo messaggio ricevuto in ordine
							if(count_retransmit < 3) {
								invio_ACK(lastAckCorrect);
								count_retransmit++;
                                goto LOOP;
							}
							else {
								//già sono stati inviati 3 ack duplicati
								goto LOOP;
							}
						}
					}
					else {
						//caso in cui si riceve l'ultimo messaggio che compone il file
						count_retransmit = 0;

						//calcolo probabilità di perdita del pacchetto (mancata scrittura nel file)
						prob_rvd_file = (rand() % 100);

						if(prob_rvd_file < PROB_PERDITA) {
							/*caso in cui si considera il messaggio non arrivato e quindi
							non viene inviato nessun ack di risposta al server*/
							printf("Messaggio n°.%d PERSO\n", i);

							goto ACK_PERSO;
						}

						//inserimento del messaggio i-esimo all'interno del file creato nel client
						val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
						if(val == -1) {
							perror("Errore, scrittura sul file client fallita\n");
							exit(-1);
						}
						printf("Messaggio n°.%d ARRIVATO\n", i);

						//assegnazione del valore del successivo byte da inviare alla variabile di ack
						lastAckCorrect = lastByteReceived + 1;

						//invio ack relativo all'ultimo messaggio ricevuto in ordine
						invio_ACK(lastAckCorrect);

						break;
					}		
				}
			}
ACK_PERSO:	
			if(i == 0) {
				//pacchetto perso è il primo -> non invio nulla al server
				continue;
			}
			else {
				if(count_retransmit < 3) {
					invio_ACK(lastAckCorrect);
					count_retransmit++;
				}
				else {
					//già sono stati inviati 3 ack duplicati, non invio altri
					goto LOOP;
				}
			}
		}
        //riposizionamento del puntatore all'interno del file
        lseek(fd, 0, SEEK_SET);        

		//chiusura del file descriptor associato al file creato
		close(fd);
	}
	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    gettimeofday(&tv4, NULL);//fermo il timer
    
	printf("Downlaod total time = %f seconds\n\n",(double) (tv4.tv_usec - tv3.tv_usec) / 1000000 +(double) (tv4.tv_sec - tv3.tv_sec));
}

/*funzione utilizzata da 'func_get' per inviare alla funzione client il file da salvare nella propria cartella
e per gestire la ricezione degli ack ricevuti dal client*/
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file) {
    int result, i;
    int count_ack = 0;          //variabile che tiene traccia del numero di ack ricevuti
    int seq_window = 0;         //variabile utilizzata per la gestione della finestra di ricezione
    int fast_retransmit = 0;    //contatore che conta gli ack uguali
    int ack_prev = 0;           //variabile utilizzata con fast_retransmit per conoscere il valore del precedente ack

    /*lettura ed inserimento dei dati del file nella sezione message_buffer
    e valore dell'ultimo byte letto nella sezione message_pointer*/
    for(i = 0; i < num_message; i++) {
    	//creazione dei 'num_message' message_buffer
    	val = read(fd, pack[i].message_buffer, maxline);

    	if(val == -1) {
            perror("Errore, lettura dei dati del file fallita\n");
            exit(-1);
        }
        else {
            //inserisco in message_pointer il valore del primo byte letto 
            if(i == 0) {
                //caso d'invio del primo pacchetto
                pack[i].message_pointer = val;
            }
            else {
                //caso d'invio dei pacchetti successivi al primo
                pack[i].message_pointer = pack[i-1].message_pointer + val;
            }
        }
    }

    //invio dei messaggi appena creati e dei message_pointer per la gestione dei byte inviati/ricevuti
    value_ack = 0;
    i = 0;
    
    //inizio conteggio tempo 
    gettimeofday(&tv3, NULL);

    while(1) {
SEND:	
        if(i == num_message) {
            //caso in cui tutti i pacchetti sono stati inviati al client
            i++;
            goto WAIT;        
        } 
        else if(i > num_message) {
            //caso in cui tutti i pacchetti sono stati inviati e si aspetta di terminare il processo    
            if(value_ack < lunghezza_file) {
                goto WAIT;
            }
            else {
                goto END;
            }      
        }
        else {		
        	//caso in cui non tutti i pacchetti sono stati inviati (i < num_message)
            if(seq_window < N) {
            	//caso in cui la finestra di ricezione non è piena

            	printf("Stato di SEND -> invio pacchetto n.%d al client\n", i);

            	//invio contenuto del file (pack[i].message_buffer) al client
            	result = sendto(sd_child, pack[i].message_buffer, strlen(pack[i].message_buffer), 0, (struct sockaddr *)&sad, sizeof(sad));

            	if(result == -1) {
            		perror("Errore, invio messaggio al client (1) fallito\n");
            		exit(-1);
            	}
            	else {
                    if(chose_timeout == 1) {
                        //si fa partire il conteggio del tempo di andata e ritorno (RTT)
                        start_time[i%N] = clock();
                    }

            		//invio l'ultimo byte di cui si compone il messaggio inviato
            		memset(buffer, 0, sizeof(buffer));
  					sprintf(buffer, "%d", pack[i].message_pointer);

  					result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));

  					if(result == -1) {
  						perror("Errore, invio messaggio al client (2) fallito\n");
  						exit(-1);
            		}
            		else {
  						//si fa partire il timeout associato all'ultimo messaggio inviato al client
                        if(chose_timeout == 0) {
                            //caso di utilizzo del timeout statico (sempre lo stesso valore)
                            setTimeout(STATIC_TIMEOUT, i, chose_timeout);
                        }
                        else if(chose_timeout == 1) {
                            //caso di utilizzo del timeout adattivo (varia a seconda dei ritardi nella rete)
                            setTimeout(ADAPTIVE_TIMEOUT, i, chose_timeout);
                        }

  						//incremento della finestra di ricezione
  						seq_window++;
                        i++;

                        //invio pacchetto successivo
                        goto SEND;
  					}
  				}
  			}
           	else {
           		//caso in cui la finestra di ricezione è piena
WAIT:
				printf("\nStato di WAIT -> attesa di ack n.%d dal client\n", count_ack);

  				memset(buffer, 0, sizeof(buffer));

  				//variabile utilizzata per la gestione degli errori (definita nella livreria errno.h)
  				errno = 0;

                //printf("\nprima di result associato a messaggio %d\n", count_ack);

  				//attesa ack per decrementare la finestra di ricezione
  				result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);

                //printf("\nresult associato a messaggio %d = %d\n",count_ack, result);

  				if(result < 0) {
  					if(errno == EAGAIN || errno == EWOULDBLOCK) {

  						//calcolo pacchetto dal quale riniziare la trasmissione poichè è scaduto il timeout
  						i = (value_ack / maxline);
  						printf("\nTimeout scaduto, RITRASMISSIONE da pack n°.%d\n", i);

  						//azzeramento delle variabili usate fino ad ora
  						fast_retransmit = 0;
  						seq_window = 0;
  						errno = 0;

  						goto SEND;
  					}
  				}
                //Controllo che nel buffer non ci sia il codice di uscita
                else if(atoi(buffer) == exit_code_client) {
                    printf("\nClient ha smesso di funzionare (sd_child:%d)\n", sd_child);
                    exit(1);
                }
  				else {
  					//ack ricevuto con successo, controllo il suo valore
  					value_ack = atoi(buffer);

  					if(value_ack > ack_prev) {
  						//arrivo dell'ack successivo desiderato

                        if(chose_timeout == 1) {
                            int indice_pacchetto = (value_ack/maxline)-1;
                            
                            printf("Stampo indice pacchetto del timeout - [%d]\n",indice_pacchetto);

                            //si fa terminare il conteggio del tempo di andata e ritorno (RTT);
                            end_time[indice_pacchetto%N] = clock() - start_time[indice_pacchetto%N];
                            
                            //calcolo del sample_RTT, estimated_RTT e dev_RTT (espressi in secondi)
                            sample_RTT = (float) end_time[indice_pacchetto%N] / CLOCKS_PER_SEC;
                            estimated_RTT = ((1 - 0.125) * estimated_RTT) + (0.125 * sample_RTT);

                            if(sample_RTT > estimated_RTT) {
                                dev_RTT = ((1 - 0.25) * dev_RTT) + (sample_RTT - estimated_RTT);
                            }
                            else {
                                dev_RTT = ((1 - 0.25) * dev_RTT) + (estimated_RTT - sample_RTT);
                            }
                            //aggiornamento dell'intervallo di timeout (in secondi)
                            ADAPTIVE_TIMEOUT = estimated_RTT + (4 * dev_RTT);

                            printf("Ack associato al messaggio n.%d arrivato -> new ADAPTIVE_TIMEOUT [ms]: %f\n", count_ack, ADAPTIVE_TIMEOUT*1000);
                        }

  						//aggiorno la variabile ack_prev
  						ack_prev = value_ack;

						//aggiornamento delle variabili per la gestione dei messaggi
						seq_window--;
						fast_retransmit = 0;
						count_ack++;

  						goto SEND;
  					}
  					else if(value_ack == ack_prev) {
  						//arrivo di un ack già ricevuto in passato
                        printf("Ack associato al messaggio n.%d arrivato di nuovo\n", count_ack-1);

  						//incremento del contatore per la ritrasmissione veloce
  						fast_retransmit++;

                        //printf("variabile 'fast_retransmit' vale: %d\n", fast_retransmit);

  						if(fast_retransmit == 3) {

  							//calcolo pacchetto dal quale inizia la ritrasmissione
  							i = (value_ack / maxline);
  							printf("\n3° ack duplicato, FAST RETRANSMIT da pack n°.%d\n", i);

  							//aggiorno le variabili locali
  							fast_retransmit = 0;
  							seq_window = 0;
  							count_ack = i;

  							goto SEND;
  						}
  						
  						if (seq_window<N){ //sono nel caso in cui ho perso l'ack dell'ultimo pacchetto
                            //faccio ripartire il timeout
                            
                            if(chose_timeout == 0) {
                            //caso di utilizzo del timeout statico (sempre lo stesso valore)
                            setTimeout(STATIC_TIMEOUT, i, chose_timeout);
                            }
                            else if(chose_timeout == 1) {
                            //caso di utilizzo del timeout adattivo (varia a seconda dei ritardi nella rete)
                            setTimeout(ADAPTIVE_TIMEOUT, i, chose_timeout);
                            }
                        }
  						else {
 							//caso in cui gli ack duplicati non sono 3
  							goto SEND;
  						}
  					}
  				}
  			}
   		}
    }
END:
	printf("----File inviato nel CLIENT con successo----\n\n");

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    gettimeofday(&tv4, NULL);//fermo il timer
    
	printf("Downlaod total time = %f seconds\n\n",(double) (tv4.tv_usec - tv3.tv_usec) / 1000000 +(double) (tv4.tv_sec - tv3.tv_sec));
}

/*funzione utilizzata nel main per mettere in comunicazione il processo figlio del lato server
con il processo appena creato del lato client*/
int create_connection(int port) {
    int result, des;
	
	//creazione della socket per la comunicazione con il processo client
	des = socket(AF_INET, SOCK_DGRAM, 0);
	if(des == -1) {
		perror("Errore, creazione socket fallita\n");
		exit(-1);
	}

	//inizializzazione dell'indirizzo IP e del numero di porta
	memset((void *)&sad, 0, sizeof(sad));						//svuotamento iniziale della struttura server
	sad.sin_family = AF_INET;									//tipo di indirizzo (IPv4)
	sad.sin_addr.s_addr = htonl(INADDR_ANY);					//indirizzi IP da ogni interfaccia del blocco 127.0.0/32
	sad.sin_port = htons(port);									//assegnazione numero di porta del server
	
	result = bind(des, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("Errore, inizializzazione processo locale fallita\n");
		exit(-1);
	}
	return des;
}

/*funzione per la gestione del timer associato al messaggio inviato e per
l'eventuale gestione del segnale causato da un evento di timeout*/
void setTimeout(double time, int id, int tempo_scelto) {
	struct timeval timeout;

    if(tempo_scelto == 0) {
        //nel caso si utilizzi il timeout statico, il tempo è espresso in millisecondi
        timeout.tv_sec = 0;
	    timeout.tv_usec = time;
    }
    else if(tempo_scelto == 1) {
        //nel caso si utilizzi il timeout adattivo, in tempo è calcolato in secondi
        timeout.tv_sec = 0;
        timeout.tv_usec = time*10000000;
    }
	setsockopt(sd_child, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

/*funzione per la creazione e invio di buffer contenenti il valore di ack
da inviare all'altro processo comunicante*/
void invio_ACK(int valore_ack) {
	int result;

	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "%d", valore_ack);

	//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
	result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("Errore, invio dell'ack al client fallito\n");
		exit(-1);
	}
	else {
        printf("lastAckSend: %d\n", valore_ack);
		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));
	}
}

/*funzione per la gestione del segnale e invio dello stesso al client affinchè
possa chiudersi da entrambi i lati la connessione socket creata*/
void exit_handler(int signo) {
    int res;

    printf("\nSegnale di interruzione generato dall'utente\n");

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //invio al server il buffer contenente un codice per terminare il processo
    sprintf(buffer, "%d", exit_code_server);
    
    res = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
    if(res == -1) {
        perror("Errore, invio codice terminazione fallito\n");
        exit(-1);
    }
    else {
        //chiusura della socket
        close(sd_child);
        close(sd);
        printf("Chiusura di tutte le socket associate ai vari processi client\n");

        //svuotamento della memoria
        memset(buffer, 0, sizeof(buffer));
        exit(1);
    }
}
