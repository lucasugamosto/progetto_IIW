#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>
#include <fcntl.h>					//libreria per le funzioni riguardanti i file
#include <sys/stat.h>
#include <math.h>

#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/errno.h>

#include <sys/socket.h>				//definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>				//definisce i tipi di dato per rappresentare gli indirizzi IP in internet
#include <arpa/inet.h>				//definisce le funzioni per manipolare gli indirizzi IP
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>					//libreria per l'implementazione del comando "list"
#include <libgen.h>					//libreria per l'implementazione del comando "list"

#define standard_port 1024  		//porta iniziale per il collegamento con il processo server
#define maxline 124					//dimensione massima del buffer
#define N 4 						//dimensione finestra di ricezione
#define TIMEOUT 10000   			//tempo dopo il quale arriva il segnale di errore SIGALARM
#define PROB_PERDITA 50				//probabilità di perdere i pacchetti (in percentuale)

//struttura per la gestione del GO-BACK-N
typedef struct message_struct {
	int message_pointer;			//definisce l'ultimo byte letto dal messaggio
	char message_buffer[maxline];	//definisce il buffer in cui inserire il contenuto del file
} message;

int sd;				        		//socket descriptor
int port_number, len;
int lunghezza_file, num_message;
char buffer[maxline];				//buffer contenente i messaggi di richiesta e di risposta

struct sockaddr_in servaddr;		//struttura per l'indirizzo IP locale e numero di porta locale
struct dirent *dp;					//struttura per la gestione dei file del server

int value_ack;

DIR *dir;							//descrittore del flusso di directory del server
size_t val;							//variabile per la gestione delle funzioni riguardanti i file

void func_list(char *buffer);
void func_get(char *buffer);
void func_put(char *buffer);
void ricezione_GBN(char *pathname);
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file);
void create_connection(char *buffer, int port_number);
void setTimeout(double time, int id);
void invio_ACK(int valore_ack);

int main(int argc, char *argv[]) {
	int result;
	
	//controllo la presenza dell'indirizzo IP passato come argomento della funzione
	if(argc < 2) {
		perror("nessun indirizzo IP inserito in input\n");
		exit(-1);
	}
	
	//creazione della socket per la connessione con il processo server
	create_connection(argv[1], standard_port);  //settaggio a sd della nuova socket
    
    //comunico al server l'esistenza del nuovo client
	if(sendto(sd, NULL, 0, 0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("errore, comunicazione con il server fallita\n");
		exit(-1);
	}
	
	//svuotamento del buffer e calcolo grandezza della struttura sockaddr
	memset(buffer, 0, sizeof(buffer));
    len = sizeof(servaddr);

	//ricezione del nuovo numero di porta dal server
	result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
	if(result < 0) {
		perror("errore, ricezione 2° numero di porta fallito.\n");
		fflush(stdout);
		close(sd);
		exit(-1);
	}

	//chiusura della precendente connessione (con socket di ascolto) con il server
	close(sd);
	
	//conversione della stringa contenente il nuovo numero di porta
	int port_number = atoi(buffer);
    
    //creazione della nuova socket per la comunicazione con il processo figlio del server
    create_connection(argv[1], port_number);
    
	while(1) {
		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));
		printf("inserisci comando: ");

		//inserimento del comando tramite scanf
		result = scanf("%s", buffer);
        
		if(result == EOF) {
			perror("errore, lettura del comando fallita\n");
			exit(-1);
		}
		else if(result == 0) {
			perror("errore, nessun comando inserito\n");
			exit(-1);
		}

		//comando LIST (richiesta della lista dei file presenti nel server)
		else if(strcmp("list", buffer) == 0) {
			func_list(buffer);
		}

		//comando GET (richiesta di download di un file presente nel server)
		else if(strcmp("get", buffer) == 0) {
			func_get(buffer);
		}

		//comando PUT (richiesta di upload di un file sul server)
		else if(strcmp("put", buffer) == 0) {
			func_put(buffer);
        }
        else {
        	printf("comando inserito non esistente. Riprovare\n");
        }
	}
	return(0);
}

void func_list(char *buffer) {
	int result;

	//invio richiesta corrispondente al comando LIST
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta 'LIST' fallito\n");
		exit(-1);
	}

	//ricezione risposta corrispondente al comando LIST
	result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);          //(NULL intende che non si sa chi invia la risposta)
	if(result == -1) {
		perror("errore, ricezione risposta 'LIST' fallita\n");
		exit(-1);
	}

	while(strlen(buffer) == (maxline-1)) {
		result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);		//(NULL intende che non si sa chi invia la risposta)
		if(result == -1) {
			perror("errore, ricezione risposta 'LIST' fallita\n");
			exit(-1);
		}
		printf("%s", buffer);
	}
	printf("file presenti nel server:\n%s\n", buffer);

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
}

void func_get(char *buffer) {
	int result, c;
	char *pathname;

	//allocazione di memoria per la definizione del percorso del file
	pathname = (char *)malloc(124);
	if(pathname == NULL) {
		perror("errore, allocazione di memoria fallita\n");
		exit(-1);
	}

	//invio richiesta corrispondente al comando GET
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta 'GET' fallito\n");
		exit(-1);
	}

	//svuotamento del buffer e calcolo della grandezza della struttura sockaddr
	memset(buffer, 0, sizeof(buffer));
	len = sizeof(servaddr);

	//inserimento nel buffer del nome del file da scaricare
	printf("File da richiedere al server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	//salvataggio del percorso del file da aprire
	strcpy(pathname, "client UDP/");
    strcat(pathname, buffer);

	//invio richiesta corrispondente al comando GET (invio del percorso del file)
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta 'GET' fallito (2)\n");
		exit(-1);
	}

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    //funzione per la gestione del GO-BACK-N
    ricezione_GBN(pathname);
}

void func_put(char *buffer) {
	int result, c, fd;

	//invio richiesta corrispondente al comando PUT
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta 'PUT' fallito\n");
		exit(-1);
	}
	//invio al server il nome del file da salvare
	memset(buffer, 0, sizeof(buffer));

	//inserimento nel buffer del nome del file da inviare
	printf("file da scrivere sul server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	//apertura della directory del client e controllo dei file presenti in essa
	if((dir = opendir ("client UDP")) == NULL) {
        perror ("errore, lettura file nel client fallita\n");
        exit (-1);
    }
    //si è inizializzata la variabile result a 0
    //se result = 0: file non presente nel client
    //se result = 1: file presente nel client
    result = 0;

	while ((dp = readdir (dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer
            if(strcmp(dp->d_name, buffer) == 0) {		//controllo se il file richiesto è presente nel client

            	//variabile impostata per indicare che il file è presente nel client
            	result = 1;
            	memset(buffer, 0, sizeof(buffer));

            	//creazione del percorso sul lato server dove salvare il file
            	strcpy(buffer, "server UDP/");
            	strcat(buffer, dp->d_name);
                
                //invio il nome del file da voler caricare sul server
            	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            	if(result == -1) {
            		perror("errore, invio nome file fallito\n");
            		exit(-1);
            	}
            	
                memset(buffer, 0, sizeof(buffer));

                //creazione del percorso sul lato client dove prelevare il file
            	strcpy(buffer, "client UDP/");
            	strcat(buffer, dp->d_name);
            	
            	fd = open(buffer, O_RDONLY, 0666);
            	if(fd == -1) {
            		perror("errore, apertura file fallita\n");
            		exit(-1);
            	}

                //calcolo lunghezza del file da inviare
                lunghezza_file = lseek(fd, 0, SEEK_END);

                //inserimento nel buffer della lunghezza del file da inviare
                memset(buffer, 0, sizeof(buffer));
                sprintf(buffer, "%d", lunghezza_file);

                //invio informazioni sulla lunghezza del file al client
                result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                if(result == -1) {
                	perror("errore, invio lunghezza del file fallito\n");
                	exit(-1);
                }

                //riposizionamento del puntatore all'inizio del file per la futura lettura
                lseek(fd, 0, SEEK_SET);

                //calcolo il numero di pacchetti da dover inviare
                num_message = ceil(lunghezza_file/maxline)+1;

                //inserimento nel buffer del numero di pacchetti da inviare
                memset(buffer, 0, sizeof(buffer));
                sprintf(buffer, "%d", num_message);

                //invio informazioni sul numero di pacchetti da inviare al client
                result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                if(result == -1) {
                    perror("errore, invio del numero di pacchetti fallito\n");
                    exit(-1);
                }
    
                //svuotamento del buffer
                memset(buffer, 0, sizeof(buffer));

                message pack[num_message];			//inizializzazione delle strutture 'message'

                //pulizia dei campi di ogni struttura allocata
                for(int i = 0; i < num_message; i++) {
                    memset(pack[i].message_buffer, 0, maxline);
                    pack[i].message_pointer = 0;
                }
            	
            	//funzione utilizzata per l'invio dei pacchetti al server
                invio_GBN(pack, num_message, fd, lunghezza_file);

                //chiusura del descrittore del file precedentemente aperto
            	close(fd);
            }
        }
    }
    if(result == 0) {
       	//caso in cui il file non è presente nel client
        printf("file non presente nel client\n\n");

        result = sendto(sd, NULL, 0, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        if(result == -1) {
        	perror("errore, invio informazioni al server fallita (put)\n");
        	exit(-1);
        }
	}

	//chiusura della directory dir per lo scorrimento e analisi dei file
    closedir(dir);

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
}

/*funzione usata all'interno della funzione 'GET' per la ricezione del file in arrivo dal processo server*/
void ricezione_GBN(char *pathname) {
    int seq_window = 0;
    int valore_atteso = 0;				//valore dell'ultimo byte letto del messaggio atteso
    int lastAckCorrect = 0;				//valore ack dell'ultimo byte ricevuto in ordine
    int prob_rvd_file;					//probabilità di ricezione del pacchetto dal server
    int lastByteReceived = 0;			//valore dell'ultimo byte dell'ultimo messaggio ricevuto
    int fd, i, result;
    int count_retransmit = 0;			//variabile per il conteggio delle ritrasmissioni effettuate
    time_t t;							//variabile per la generazione del valore randomico

    //inizializzazione del genereatore di numeri random
    srand((unsigned) time(&t));

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //ricezione della lunghezza del file da ricevere o NULL in caso file non presente nel server
    result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
    if(result == -1) {
    	perror("errore, ricezione lunghezza del file fallito\n");
    	exit(-1);
    }
    else if(result == 0) {
    	//caso in cui il file non è presente sul server
    	printf("file richiesto non presente nel server\n\n");
    }

    else {
    	//caso in cui il file è presente sul server

    	//lunghezza del file da ricevere dal client
    	lunghezza_file = atoi(buffer);

    	//svuotamento del buffer
    	memset(buffer, 0, sizeof(buffer));

    	//ricezione del numero di messaggi da ricevere in totale
    	result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
    	if(result == -1) {
    		perror("errore, ricezione numero di pacchetti fallito\n");
    		exit(-1);
    	}

    	//creazione e apertura del file per lettura/scrittura
    	fd = open(pathname, O_CREAT|O_RDWR, 0666);
        if(fd == -1) {
            perror("errore, apertura del file fallita\n");
            exit(-1);
        }	
        free(pathname);

    	//salvo nella variabile 'num_message' il numero di messaggi da ricevere
    	num_message = atoi(buffer);

    	//inizializzazione delle num_message strutture da creare
    	message pack[num_message];

    	//svuotamento del buffer
    	memset(buffer, 0, sizeof(buffer));

        //pulizia dei campi di ogni struttura allocata
        for(int i = 0; i < num_message; i++) {
            memset(pack[i].message_buffer, 0, maxline);
            pack[i].message_pointer = 0;
        }

		//ricezione dei messaggi dal server
		i = 0;

		while(i < num_message) {
LOOP:
			printf("\nValore di NUM_MESSAGE: %d\n", num_message);
			printf("Valore di LUNGHEZZA_FILE:%d\n", lunghezza_file);

			//ricezione del contenuto del messaggio i-esimo
			result = recvfrom(sd, pack[i].message_buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
			if(result == -1) {
				perror("errore, ricezione messaggio di risposta fallita\n");
				exit(-1);
			}
			else {
				memset(buffer, 0, sizeof(buffer));

				//ricezione dell'ultimo byte di cui si compone il messaggio da ricevere
				result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
				if(result == -1) {
					perror("errore, ricezione ultimo byte del messaggio fallito\n");
					exit(-1);
				}
				else {
					//salvataggio del valore dell'ultimo byte nella variabile dedicata
					lastByteReceived = atoi(buffer);

					printf("\nlastByteReceived: %d\n", lastByteReceived);

					valore_atteso = (maxline * (i+1));		//valore dell'ultimo byte associato al messaggio i-esimo atteso

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
								printf("messaggio n°.%d PERSO\n", i);
								
								goto ACK_PERSO;
							}
							printf("messaggio n°.%d ARRIVATO\n", i);

							//inserimento del messaggio i-esimo all'interno del file creato nel client
							val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
							if(val == -1) {
								perror("errore, scrittura sul file client fallita\n");
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
							//si scarta il pacchetto arrivato e si manda al server l'ack del pacchetto desiderato

							if(lastByteReceived%maxline == 0) {
								printf("messaggio n°.%d SCARTATO\n", (lastByteReceived/maxline)-1);
							}
							else if(lastByteReceived%maxline > 0) {
								printf("messaggio n°.%d SCARTATO\n", (lastByteReceived/maxline));
							}
							
							//invio ack relativo all'ultimo messaggio ricevuto in ordine
							if(count_retransmit < 3) {
								invio_ACK(lastAckCorrect);
								count_retransmit++;
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
							printf("messaggio n°.%d PERSO\n", i);

							goto ACK_PERSO;
						}

						//inserimento del messaggio i-esimo all'interno del file creato nel client
						val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
						if(val == -1) {
							perror("errore, scrittura sul file client fallita\n");
							exit(-1);
						}
						printf("ultimo messaggio n°.%d ARRIVATO\n", i);

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
				//non invio nulla al server
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
		//lseek(fd, 0, SEEK_SET);

		//chiusura del file descriptor associato al file creato
		close(fd);

		printf("----File salvato nel CLIENT con successo----\n\n");
	}
	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer)+1);
}

void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file) {
    int result;
    int count_ack = 0;			//variabile che tiene traccia del numero di ack ricevuti
    int seq_window = 0;			//variabile utilizzata per la gestione della finestra di ricezione
    int fast_retransmit = 0;	//contatore che conta gli ack uguali
    int ack_prev = 0;			//variabile utilizzata con fast_retransmit per conoscere il valore del precedente ack
    int i;

    /*lettura ed inserimento dei dati del file nella sezione message_buffer
    e valore dell'ultimo byte letto nella sezione message_pointer*/
    for(i = 0; i < num_message; i++) {
    	//creazione dei 'num_message' message_buffer
    	val = read(fd, pack[i].message_buffer, maxline);

    	if(val == -1) {
            perror("errore, lettura dei dati del file fallita\n");
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
    //inizializzo a 0 l'indice per lo scorrimento dei pacchetti
    i = 0;
    len = sizeof(servaddr);
    
    while(1) {       	
SEND:
		
    	if(i == num_message) {
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
        	//caso in cui non tutti i pacchetti sono stati inviati (i < num_message)		
            if(seq_window < N) {
            	//caso in cui la finestra di ricezione non è piena
           		
           		printf("stato di SEND -> invio pacchetto n. %d al server\n", i);

            	//invio contenuto del file (pack[i].message_buffer) al server
            	result = sendto(sd, pack[i].message_buffer, (strlen(pack[i].message_buffer)+1), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

            	if(result == -1) {
            		perror("errore, invio messaggio al server (1) fallito\n");
            		exit(-1);
            	}
            	else {
            		//invio l'ultimo byte di cui si compone il messaggio inviato
            		memset(buffer, 0, sizeof(buffer));
  					sprintf(buffer, "%d", pack[i].message_pointer);

  					result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  					if(result == -1) {
  						perror("errore, invio messaggio al server (2) fallito\n");
  						exit(-1);
            		}
            		else {
  						//si fa partire il timeout associato all'ultimo messaggio inviato al server
                        setTimeout(TIMEOUT, i);

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
				printf("stato di WAIT -> attesa di ack n. %d dal server\n", count_ack);

  				memset(buffer, 0, sizeof(buffer));

  				//variabile utilizzata per la gestione degli errori (definita nella livreria errno.h)
  				errno = 0;

  				//attesa ack per decrementare la finestra di ricezione
  				result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);

  				if(result < 0) {
  					if(errno == EAGAIN || errno == EWOULDBLOCK) {
  						printf("Timeout scaduto, eseguire RITRASMISSIONE\n");

  						//calcolo pacchetto dal quale riniziare la trasmissione
  						i = (value_ack / maxline);
  						printf("ritrasmissione dal pacchetto n°.%d\n", i);

  						//azzeramento delle variabili usate fino ad ora
  						fast_retransmit = 0;
  						seq_window = 0;
  						errno = 0;

  						goto SEND;
  					}
  				}
  				else {
  					//ack ricevuto con successo, controllo il suo valore
  					value_ack = atoi(buffer);

  					if(value_ack > ack_prev) {
  						//arrivo dell'ack successivo desiderato

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
  						
  						//incremento del contatore per la ritrasmissione veloce
  						fast_retransmit++;

  						if(fast_retransmit == 3) {
  							printf("Ricevuti 3 ack duplicati, eseguire FAST RETRANSMIT\n");

  							//calcolo pacchetto dal quale inizia la ritrasmissione
  							i = (value_ack / maxline);
  							printf("ritrasmissione dal pacchetto n°.%d\n", i);

  							//aggiorno le variabili locali
  							fast_retransmit = 0;
  							seq_window = 0;
  							count_ack = i;

  							goto SEND;
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
	printf("fine invio pacchetti al CLIENT\n\n");

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer)+1);
}
 
void create_connection(char *buffer,int port_number) {
    int result;

    //creazione della socket per il processo client
	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1) {
		perror("errore, creazione socket client fallita\n");
		exit(-1);
	}
	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&servaddr, 0, sizeof(servaddr));				//svuotamento iniziale della struttura server
	servaddr.sin_family = AF_INET;								//tipo di indirizzo (IPv4)
	servaddr.sin_port = htons(port_number);						//assegnazione numero di porta del server

	result = inet_aton(buffer, &(servaddr.sin_addr));			//assegnazione dell'indirizzo del server presente in buffer
	if(result == 0) {
		perror("errore, conversione dell'indirizzo IP fallito\n");
		exit(-1);
	}
}

void setTimeout(double time, int id) {
	struct timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = TIMEOUT;

	setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

void invio_ACK(int valore_ack) {
	int result;

	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "%d", valore_ack);

	//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio dell'ack al server fallito\n");
		exit(-1);
	}
	else {
		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));
	}
}