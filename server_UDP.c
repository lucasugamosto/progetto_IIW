#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <fcntl.h>			//libreria per le funzioni riguardanti i file
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#include <sys/socket.h>		//definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>		//definisce i tipi di dato per rappresentare gli indirizzi IP in internet
#include <arpa/inet.h>		//definisce le funzioni per manipolare gli indirizzi IP
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>			//libreria per l'implementazione del comando "list"
#include <libgen.h>			//librerua per l'implementazione del comando "list"

#include <signal.h>
#include <sys/wait.h>

#define standard_port 1024		
#define maxline 256					//dimensione massima dei messaggi inviati e ricevuti
#define N 4 						//dimensione dellafinestra di ricezione
#define TIMEOUT 1   				//tempo dopo il quale arriva il segnale di errore SIGALARM

//struttura per la gestione del GO-BACK-N
typedef struct message_struct {
	int message_pointer;			//definisce l'ultimo byte letto dal messaggio
	char message_buffer[maxline];
} message;

int sd;				        		//socket descriptor
int sd_child;         	 			//child socket descriptor
int indice;							//indice per l'invio dei pacchetti al client con "invio_GBN"
int value_ack, count_ack;
int lunghezza_file, len;			
int port_number = 1025;				//inizializzato al primo numero di porta disponibile
char buffer[maxline];				//buffer per l'inserimento di stringhe di invio/richiesta

struct sockaddr_in sad;				//struttura per l'indirizzo IP locale e numero di porta locale
struct dirent *dp;					//struttura per la gestione dei file del server

struct sigaction sa;				//struttura per la gestione del segnale SIGALARM e dell'handler
struct itimerval timer;				//struttura per la gestione del timeout che dà vita al segnale di errore

DIR *dir;							//descrittore del flusso di directory del server
pid_t pid;
size_t val;

void func_list();
void func_put();
void func_get();
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file);
void ricezione_GBN(int fd);
int create_connection(int port);
void timer_handler(int signum);

int main(int argc, char *argv[]) {
	int result;
	int fd;								//file descriptor

	//creazione della socket per il processo server (usata solo per l'ascolto di richieste)
    sd = create_connection(standard_port);

	//fase di attesa di richieste da parte dei client
	printf("---server in attesa di richieste---\n");

	while(1) {
		//svuotamento del buffer e calcolo grandessa della struttura sockaddr
		memset(buffer, 0, sizeof(buffer));
		len = sizeof(sad);

		//ricezione dell'esistenza dei nuovi processi client al server
		result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
		if(result == -1) {
			perror("errore, ricezione richiesta (1) fallita\n");
			exit(-1);
		} 
		else if(result == 0) {         
            memset(buffer, 0, sizeof(buffer));

            //inserimento del nuovo numero di porta nel buffer per l'invio al client
            sprintf(buffer,"%d",port_number);
            
            //invio al client il nuovo numero di porta 
            result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
                perror("errore, invio risposta fallito\n");
                exit(-1);
            }

            /*//inizializzazione di timer_handler associato al segnale SIGALARM
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = &timer_handler;
            if(sigaction(SIGALRM, &sa, NULL) < 0) {
            	psignal(SIGALRM, "creazione segnale fallito");
            	exit(-1);
            }

            //configurazione delle variabili nella struttura itimerval
            timer.it_interval.tv_sec = TIMEOUT;
            timer.it_interval.tv_usec = 0;
            timer.it_value.tv_sec = TIMEOUT;
            timer.it_value.tv_usec = 0;*/
            
            //creazione del processo figlio per la comunicazione col client
            pid = fork();

			if(pid == -1) {
				perror("errore, creazione processo figlio fallita\n");
				exit(-1);
			}
			if(pid == 0) {
				//processo figlio in esecuzione
                printf("processo figlio con pid: %d e numero di porta: %d\n",getpid(), port_number);
				
				//creazione della nuova socket per il processo figlio e chiusura di quella del processo padre
				sd_child = create_connection(port_number);
				close(sd);
                
                //inizio ricezione delle richieste in arrivo dal client
                while(1) {
                    memset(buffer, 0, sizeof(buffer));
                    
                    //ricezione del comando da far eseguire al server
                    result= recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                    if(result == -1){
                        printf("errore, ricezione richiesta (2) fallito\n");
                        exit(-1);
                    }
                    
                    //comando LIST (invio al client la lista dei file del server)
                    if(strcmp("list", buffer) == 0) {
                    	printf("lista dei file nel server richiesta\n");
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
                    	else {
                    		//caso in cui il file è presente nel client
                        	printf("scrittura di un file sul server richista\n");
                        	func_put();
                        }
                    }

                    //comando GET (invio del file richiesto al client)
                    else if(strcmp("get", buffer) == 0) {
                    	memset(buffer, 0, sizeof(buffer));

                    	//ricezione del nome del file da scrivere sul client
                    	result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
                    	if(result == -1) {
                        	perror("errore, ricezione del nome file fallito\n");
                        	exit(-1);
                    	}
                    	else {
                    		printf("lettura di un file del server richiesta\n");
                    		func_get();
                        }
                    }
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
            printf("errore, ricezione buffer vuoto fallito\n");
            continue; 
        }
    }
    return(0);
}

void func_list() {
	int result;

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));

	//apertura della directory e controllo dei file presenti in essa
	if ((dir = opendir ("server UDP")) == NULL) { 
        perror ("errore, lettura file nel server fallita\n");
        exit (-1);
    }
	
	//lettura della directory contenente i nomi dei file del server		
	while ((dp = readdir (dir)) != NULL) {
		if(dp->d_type == DT_REG) {

			//inserimento del file i-esimo all'interno del buffer
        	strcat(buffer, dp->d_name);
        	strcat(buffer, "\n");

			if(strlen(buffer) >= (maxline-1)) {
				//quando il buffer è pieno lo si invia al client e si svuota
    			result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
				if(result == -1) {
					perror("errore, invio risposta (2) fallito\n");
					exit(-1);
				}
				memset(buffer,0,sizeof(buffer));
			}			
    	}        				
    }
    //chiusura della directory dir
    closedir(dir);

    if(strlen(buffer) > 0) {
    	//invio della risposta al client che ne ha fatto richiesta
		result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
		if(result == -1) {
			perror("errore, invio risposta (1) fallito\n");
			exit(-1);
		}
    }
}

void func_put() {
	int result, fd;

	//creazione del file da ricevere dal client sul server
	fd = open(buffer, O_CREAT|O_RDWR|O_TRUNC, 0666);
	if(fd == -1) {
		perror("errore, creazione file fallita\n");
		exit(-1);
	}

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    //funzione per la ricezione del file dal client
    ricezione_GBN(fd);
}

void func_get() {
	int result, fd;
	int num_message;			//variabile per il numero di messaggi in cui viene diviso il file da inviare
	char *pathname;				//variabile per il percorso del file da cercare e inviare

	//allocazione di memoria per la variabile richiesta nel comando GET
	pathname = (char *)malloc(124);
	if(pathname == NULL) {
		perror("errore, allocazione di memoria fallita\n");
		exit(-1);
	}

	//apertura della directory e controllo dei file presenti in essa
	if ((dir = opendir ("server UDP")) == NULL) {
    	perror ("errore, lettura file nel server fallita\n");
        exit (-1);
    }
    //variabile impostata per indicare che il file non è presente nel server
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
            	perror("errore, apertura del file fallita\n");
            	exit(-1);
        	}
        	free(pathname);

            //calcolo la lunghezza del file da inviare
            lunghezza_file = lseek(fd, 0, SEEK_END);

            //inserimento nel buffer della lunghezza del file da inviare al client
            memset(buffer, 0, sizeof(buffer));			//svuotamento del buffer
            sprintf(buffer, "%d", lunghezza_file);

            //invio la lunghezza del file al client
            result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
            	perror("errore, invio del numero di pacchetti fallito\n");
            	exit(-1);
            }

            //riposizionamento del puntatore all'inizio del file per la lettura
            lseek(fd, 0, SEEK_SET);

            //calcolo il numero di pacchetti da dover inviare al client
            num_message = ceil(lunghezza_file/(maxline-1)) + 1;

            //inserimento nel buffer del numero di pacchetti da inviare al client
            memset(buffer, 0, sizeof(buffer));			//svuotamento del buffer
            sprintf(buffer, "%d", num_message);

            //invio il numero di pacchetti al client
            result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
            if(result == -1) {
            	perror("errore, invio del numero di pacchetti fallito\n");
            	exit(-1);
            }

            //svuotamento del buffer
            memset(buffer, 0, sizeof(buffer));

            //allocazioni delle variabili per la gestione dei messaggi inviati
            message pack[num_message];			//inizializzazione delle strutture 'message'

            //pulizia dei campi di ogni struttura allocata
        	for(int i = 0; i < num_message; i++) {
            	memset(pack[i].message_buffer, 0, maxline);
            	pack[i].message_pointer = 0;
        	}
            
            //funzione per l'invio dei pacchetti al processo ricevente
            invio_GBN(pack, num_message, fd, lunghezza_file);

    		//chiusura del descrittore del file e della directory dir
            close(fd);
            closedir(dir);
        }
    }
    
    if(result == 0) {
        /*caso in cui il file non è presente nel server e viene inviato un buffer vuoto
        per indicare al client la non presenza del file richiesto*/

        result = sendto(sd_child, NULL, 0, 0, (struct sockaddr *)&sad, sizeof(sad));
        if(result == -1) {
            perror("errore, invio risposta fallito\n");
            exit(-1);
        }
    }
}

/*funzione utilizzata da 'func_put' per inserire il file ricevuto dal client nel server e per inviare al client
informazioni sullo stato degli ack*/
void ricezione_GBN(int fd){
    int seq_window = 0;
    int err, num_message;

    //ricezione del numero di messaggi in attesa dal client
    int result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
    if(result == -1) {
    	perror("errore, ricezione numero di pacchetti fallito\n");
    	exit(-1);
    }
    else {
    	//salvataggio nella variabile 'num_message' del numero di messaggi ricevuto tramite buffer
    	num_message = atoi(buffer);

    	//allocazione delle 'num_message' strutture message
    	message pack[num_message];
    	value_ack = 0;

    	//svuotamento del buffer
    	memset(buffer,0, sizeof(buffer));

        //pulizia dei campi di ogni struttura allocata
        for(int i = 0; i < num_message; i++) {
            memset(pack[i].message_buffer, 0, maxline);
            pack[i].message_pointer = 0;
        }

		//ricezione dei messaggi dal client
		for(int i = 0; i < num_message; i++) {

			//ricezione del contenuto del messaggio i-esimo
			result = recvfrom(sd_child, pack[i].message_buffer, maxline, 0, (struct sockaddr *)&sad, &len);
			if(result == -1) {
				perror("errore, ricezione messaggio di risposta fallita\n");
				exit(-1);
			}
			else {
				//inserimento del messaggio i-esimo all'interno del file creato nel server
				val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
				if(val == -1) {
					perror("errore, scrittura sul file server fallita\n");
					exit(-1);
				}
				memset(buffer, 0, sizeof(buffer));

				//ricezione dell'ultimo byte di cui si compone il messaggio appena ricevuto
				result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);

				if(result == -1) {
					perror("errore, ricezione dell'ultimo byte del messaggio appena ricevuto fallito\n");
					exit(-1);
				}

				else {
					pack[i].message_pointer = atoi(buffer);

					//svuotamento del buffer
					memset(buffer, 0, sizeof(buffer));

					//assegnazione del valore dell'ack da inviare al client come risposta
					value_ack = pack[i].message_pointer + 1;

					//invio ack relativo all'ultimo messaggio ricevuto
					sprintf(buffer, "%d", value_ack);

					result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
					if(result == -1) {
						perror("errore, invio ack al client fallito\n");
						exit(-1);
					}
					else {
						//svuotamento del buffer
						memset(buffer, 0, sizeof(buffer));
					}
				}
			}
		}
		//chiusura del descrittore del file creato
		close(fd);
	}
	printf("\n");
}

/*funzione utilizzata da 'func_get' per inviare alla funzione client il file da salvare nella propria cartella
e per gestire la ricezione degli ack ricevuti dal client*/
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file) {
    int result, err;
    int seq_window = 0;			//variabile utilizzata per la gestione della finestra di ricezione
    int lastAckReceived = 0;
    int i, j;

    //inizializzazione di timer_handler associato al segnale SIGALARM
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_handler;
    //sigaction(SIGALRM, &sa, NULL);

    //configurazione delle variabili nella struttura itimerval
    timer.it_interval.tv_sec = TIMEOUT;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = TIMEOUT;
    timer.it_value.tv_usec = 0;

    /*inserimento dei dati del file nella sezione message_buffer
    e valore dell'ultimo byte letto nella sezione message_pointer*/
    for(i = 0; i < num_message; i++) {
    	//creazione dei 'num_message' message_buffer
    	val = read(fd, pack[i].message_buffer, maxline);

    	if(val == -1) {
            perror("errore, lettura dati del file fallita\n");
            exit(-1);
        }
        else {
            //inserisco in message_pointer il valore del primo byte letto 
            if(i == 0) {
                //caso d'invio del primo pacchetto
                pack[i].message_pointer = val;
                printf("\nmessage_buffer[%d] creato\n", i);
                printf("pack[%d].message_pointer: %d (in creazione messaggi)\n", i, pack[i].message_pointer);
            }
            else {
                //caso d'invio dei pacchetti successivi al primo
                pack[i].message_pointer = pack[i-1].message_pointer + val;
                printf("\nmessage_buffer[%d] creato\n", i);
                printf("pack[%d].message_pointer: %d (in creazione messaggi)\n", i, pack[i].message_pointer);
            }
        }
    }

    //invio dei messaggi appena creati e dei message_pointer per la gestione dei byte inviati/ricevuti
    value_ack = 0;
    indice = 0;
    j = 0;
    count_ack = 0;

    while(1) {
SEND:
		sigaction(SIGALRM, &sa, NULL);
		printf("linea 502\n");

        if(indice == num_message) {
        	//caso in cui tutti i pacchetti sono stati inviati
        	if(value_ack < lunghezza_file) {
        		//caso in cui non sono arrivati tutti gli ack
        		goto WAIT; 
        	}
            else {
            	//caso in cui tutti gli ack sono arrivati
            	goto END;
            }     
        }
        else {		
        	//caso indice < num_message (non tutti i pacchetti sono stati inviati)
            if(seq_window < N) {
            	//caso in cui la finestra di ricezione non è piena
            	printf("invio pacchetto %d-esimo | seq_window: %d\n", indice, seq_window);

            	//invio contenuto del file (pack[indice].message_buffer) al client
            	result = sendto(sd_child, pack[indice].message_buffer, (strlen(pack[indice].message_buffer)+1), 0, (struct sockaddr *)&sad, sizeof(sad));

            	if(result == -1) {
            		perror("erore, invio messaggio al client fallito\n");
            		exit(-1);
            	}
            	else {
            		//inizio conteggio timeout per ritrasmissione in caso di non arrivo ack
            		setitimer(ITIMER_REAL, &timer, NULL);
            		printf("linea 531\n");

            		//invio l'ultimo byte di cui si compone il messaggio inviato
            		memset(buffer, 0, sizeof(buffer));
  					sprintf(buffer, "%d", pack[indice].message_pointer);

  					result = sendto(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));

  					if(result == -1) {
  						perror("errore, invio valore dell'ultimo byte fallito\n");
  						exit(-1);
            		}
            		else {
  						//incremento della finestra di ricezione
  						seq_window++;
                        indice++;

                        //invio pacchetto successivo a quello attuale
                        goto SEND;
  					}
  				}
  			}
           	else {
           		////caso in cui la finestra di ricezione è piena
WAIT:
				printf("stato di wait -> ATTESA DI ACK (lato server)\n");

  				memset(buffer, 0, sizeof(buffer));

  				//attesa ack per decrementare la finestra di ricezione
  				result = recvfrom(sd_child, buffer, maxline, 0, (struct sockaddr *)&sad, &len);

  				if(result == -1) {
  					perror("errore, ricezione valore ack fallito\n");
  					//disattivazione del l'allarme impostato precedentemente
  					alarm(0);

  					exit(-1);
  				}
  				else {
  					//ack arrivato con successo, controllo il suo valore

  					//disattivazione dell'allarme impostato precedentemente
  					alarm(0);

  					value_ack = atoi(buffer);
					printf("value_ack arrivato ha valore: %d\n", value_ack);

					//decremento della finestra di ricezione
					seq_window--;
					count_ack++;

  					goto SEND;
  				}
  			}
   		}
    }
END:
    //controllo se sono arrivati tutti gli ack relativi ai messaggi inviati
	for(j = 0; j < num_message; j++) {
		if(value_ack == (pack[j].message_pointer+1)) {
			//ack ricevuto conferma l'arrivo del pacchetto j-esimo
			lastAckReceived = value_ack;
			indice = j;
		}
	}
	if(lastAckReceived == (lunghezza_file+1)) {
		printf("ack associato all'ultimo messaggio arrivato\ntutti i messaggi sono arrivati a destinazione\n");
	}
	else {
		printf("ack arrivato fino al messaggio %d\nlastAckReceived: %d, pack[%d].message_pointer: %d\n", indice, lastAckReceived, indice, pack[indice].message_pointer);
		goto SEND;	
	}
}

int create_connection(int port) {
    int result, des;
	
	//creazione della socket per il processo client
	des = socket(AF_INET, SOCK_DGRAM, 0);
	if(des == -1) {
		perror("errore, creazione socket fallita\n");
		exit(-1);
	}

	//inizializzazione dell'indirizzo IP e del numero di porta
	memset((void *)&sad, 0, sizeof(sad));						//svuotamento iniziale della struttura server
	sad.sin_family = AF_INET;									//tipo di indirizzo (IPv4)
	sad.sin_addr.s_addr = htonl(INADDR_ANY);					//indirizzi IP da ogni interfaccia del blocco 127.0.0/32
	sad.sin_port = htons(port);									//assegnazione numero di porta del server
	
	result = bind(des, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("errore, inizializzazione processo locale fallita\n");
		exit(-1);
	}
	return des;
}

void timer_handler(int signum) {

	printf("ack associato al messaggio %d non ricevuto\n", count_ack);
	printf("ritrasmissione pacchetti dal %d\n", count_ack);

	indice = count_ack;

	return;
}