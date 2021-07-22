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

#include <sys/socket.h>				//definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>				//definisce i tipi di dato per rappresentare gli indirizzi IP in internet
#include <arpa/inet.h>				//definisce le funzioni per manipolare gli indirizzi IP
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>					//libreria per l'implementazione del comando "list"
#include <libgen.h>					//libreria per l'implementazione del comando "list"

#define standard_port 1024  		//porta iniziale per il collegamento con il processo server
#define maxline 256					//dimensione massima del buffer
#define N 4 						//dimensione finestra di ricezione

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
DIR *dir;							//descrittore del flusso di directory del server
size_t val;							//variabile per la gestione delle funzioni riguardanti i file

void func_list(char *buffer);
void func_get(char *buffer);
void func_put(char *buffer);
void ricezione_GBN(char *pathname);
void invio_GBN(message *pack, int num_message, int fd, int *lastByteRead, int lunghezza_file);
void create_connection(char *buffer, int port_number);

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
			perror("errore, nessun comando è stato inserito\n");
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
	printf("file presenti nel server:\n%s", buffer);
	printf("\n");
}

void func_get(char *buffer) {
	int result, c;
	char *pathname;

	//allocazione di memoria per la variabile pathname
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

    printf("file salvato nel client con successo\n\n");
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

	printf("file da scrivere sul server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	//apertura della directory del client e controllo dei file presenti in essa
	if((dir = opendir ("client UDP")) == NULL) {
        perror ("errore, lettura file nel client fallita\n");
        exit (-1);
    }
    result = 0;

	while ((dp = readdir (dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer
            if(strcmp(dp->d_name, buffer) == 0) {		//controllo se il file richiesto è presente nel server

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

                //riposizionamento del puntatore all'inizio del file per la futura lettura
                lseek(fd, 0, SEEK_SET);

                //calcolo il numero di pacchetti da dover inviare
                num_message = ceil(lunghezza_file/(maxline-1)) + 1;

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
                int lastByteRead[num_message];		//array contenente il valore dell'ultimo byte di ogni messaggio

                //pulizia dei campi dell'array lastByteRead
                for(int i = 0; i < num_message; i++) {
                    lastByteRead[i] = 0;
                }

                //pulizia dei campi di ogni struttura allocata
                for(int i = 0; i < num_message; i++) {
                    memset(pack[i].message_buffer, 0, maxline);
                    pack[i].message_pointer = 0;
                }
            	
            	//funzione utilizzata per l'invio dei pacchetti al server
                invio_GBN(pack, num_message, fd, lastByteRead, lunghezza_file);
            }
        }
    }
    //chiusura del descrittore del file e della directory dir
    close(fd);
    closedir(dir);

    if(result == 0) {
       	//caso in cui il file non è presente nel client
        printf("file '%s' non presente nel client\n", buffer);
	}

	printf("file salvato nel server\n\n");
}

/*funzione usata all'interno della funzione 'GET' per la ricezione del file in arrivo dal processo server*/
void ricezione_GBN(char *pathname) {
    int seq_window = 0;
    int valore_atteso = 0;				//valore dell'ultimo byte letto del messaggio atteso
    int lastAckCorrect = 0;				//valore ack dell'ultimo byte ricevuto in ordine
    int lunghezza_file;					//quantità di byte di cui si compone il messaggio da ricevere dal server
    int prob_rvd_file;					//probabilità di ricezione del pacchetto dal server
    int fd, i, result;
    time_t t;							//variabile per la generazione del valore randomico

    //inizializzazione del genereatore di numeri random
    srand((unsigned) time(&t));

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //ricezione della lunghezza del file da ricevere
    result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
    if(result == -1) {
    	perror("errore, ricezione lunghezza del file fallito\n");
    	exit(-1);
    }
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
    else if(result == 0) {
    	//caso in cui il file non è presente sul server
    	printf("file richiesto non presente nel server\n");
    }

    else {
    	//caso in cui il file è presente sul server

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

			printf("valore di i: %d\n", i);
			
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
					//salvataggio del valore dell'ultimo byte nella struttura dedicata
					pack[i].message_pointer = atoi(buffer);

					valore_atteso = (maxline * (i+1));		//valore dell'ultimo byte associato al messaggio i-esimo atteso

					if(valore_atteso < lunghezza_file) {
						//caso in cui non si ricevono messaggi precedenti all'ultimo
						printf("valore_atteso (lato client): %d\n", valore_atteso);

						//controllo esattezza ordine del messaggio ricevuto
						if(valore_atteso == pack[i].message_pointer) {
							//pacchetti arrivato con successo è quello in ordine

							printf("pack[%d].message_pointer: %d\n", i, pack[i].message_pointer);

							//calcolo probabilità di perdita del pacchetto (mancata scrittura nel file)
							prob_rvd_file = (rand() % 100);
							printf("probabilità di ricezione del messaggio %d: %d\n", i, prob_rvd_file);

							if(prob_rvd_file < 50) {
								/*caso in cui si considera il messaggio non arrivato e quindi
								non viene inviato nessun ack di risposta al server*/
								printf("messaggio %d perso, invio ack associato fallito\n", i);

								goto ACK_PERSO;
							}

							//inserimento del messaggio i-esimo all'interno del file creato nel client
							val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
							if(val == -1) {
								perror("errore, scrittura sul file client fallita\n");
								exit(-1);
							}

							//assegnazione del valore dell'ultimo byte alla variabile di ack
							lastAckCorrect = pack[i].message_pointer + 1;

							printf("lastAckCorrect da inviare al server: %d\n", lastAckCorrect);

							//invio ack relativo all'ultimo messaggio ricevuto in ordine
							memset(buffer, 0, sizeof(buffer));
							sprintf(buffer, "%d", lastAckCorrect);

							//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
							result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
							if(result == -1) {
								perror("errore, invio dell'ack al server fallito\n");
								exit(-1);
							}
							else {
								memset(buffer, 0, sizeof(buffer));
								i++;
								seq_window++;
							}
						}
						else {
							//pacchetto arrivato con successo non è quello in ordine
							//si scarta il pacchetto arrivato e si manda al server l'ack del pacchetto desiderato

							printf("pack[%d].message_pointer: %d\n", i, pack[i].message_pointer);
							
							//invio ack relativo all'ultimo messaggio ricevuto in ordine
							memset(buffer, 0, sizeof(buffer));
							sprintf(buffer, "%d", lastAckCorrect);

							//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
							result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
							if(result == -1) {
								perror("errore, invio dell'ack al server fallito\n");
								exit(-1);
							}
							else {
								memset(buffer, 0, sizeof(buffer));
							}
						}
					}
					else {
						printf("atteso ultimo pacchetto che compone il file\n");
						printf("pack[%d].message_pointer: %d\n", i, pack[i].message_pointer);

						//calcolo probabilità di perdita del pacchetto (mancata scrittura nel file)
						prob_rvd_file = (rand() % 100);
						printf("probabilità di ricezione del messaggio %d: %d\n", i, prob_rvd_file);

						if(prob_rvd_file < 50) {
							/*caso in cui si considera il messaggio non arrivato e quindi
							non viene inviato nessun ack di risposta al server*/
							printf("messaggio %d perso, invio ack associato fallito\n", i);

							goto ACK_PERSO;
						}

						//inserimento del messaggio i-esimo all'interno del file creato nel client
						val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
						if(val == -1) {
							perror("errore, scrittura sul file client fallita\n");
							exit(-1);
						}

						//assegnazione del valore dell'ultimo byte alla variabile di ack
						lastAckCorrect = pack[i].message_pointer + 1;

						//invio ack relativo all'ultimo messaggio ricevuto in ordine
						memset(buffer, 0, sizeof(buffer));
						sprintf(buffer, "%d", lastAckCorrect);

						//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
						result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
						if(result == -1) {
							perror("errore, invio dell'ack al server fallito\n");
							exit(-1);
						}
						else {
							memset(buffer, 0, sizeof(buffer));
							i++;
							seq_window++;

							break;
						}
					}	
				}
			}
ACK_PERSO:	
			//non invio nulla al server
			continue;
		}
		//chiusura del file descriptor associato al file creato
		close(fd);
	}
}

void invio_GBN(message *pack, int num_message, int fd, int *lastByteRead, int lunghezza_file) {
    int result;
    int err;
    int seq_window = 0;
    int value_ack = 0;
    int i = 0;

    /*inserimento e invio dei dati del file nella sezione message_buffer
    e valore dell'ultimo byte letto nella sezione message_pointer*/
    while(1) {       	
SEND:
    	if(i == num_message) {
        	i++;
        	goto WAIT;
        }
        else if(i > num_message) {
            if(value_ack < lunghezza_file) {
             	goto WAIT;
            }
            else {
                goto END;
            }        
        }
        else {
            if(seq_window <= N) {
           		//lettura dei dati dal file ed inserimento nel buffer
           		val = read(fd, pack[i].message_buffer, maxline);

            	if(val == -1) {
            		perror("errore, lettura dati del file fallita\n");
            		exit(-1);
            	}
            	else {
                    //inserisco in message_pointer il valore dell'ultimo byte letto
                    if(i == 0) {
                    	//caso d'invio del primo pacchetto
                        pack[i].message_pointer = val;
                    }
                    else {
                    	//caso d'invio di pacchetti successivi al primo
                        pack[i].message_pointer = pack[i-1].message_pointer + val;
                    }

        			//inserisco pack[i].message_pointer nell'array lastByteRead
        			lastByteRead[i] = pack[i].message_pointer;      					

  					result = sendto(sd, pack[i].message_buffer, (strlen(pack[i].message_buffer)+1), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  							
  					if(result == -1) {
  						perror("errore, invio messaggio al server fallito\n");
  						exit(-1);
  					}
  					else {
  						//invio valore dell'ultimo byte letto dal messaggio
  						memset(buffer, 0, sizeof(buffer));
  						sprintf(buffer, "%d", pack[i].message_pointer);

  						result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  						if(result == -1) {
  							perror("errore, invio valore dell'ultimo byte fallito\n");
  							exit(-1);
  						}
  						else {
  							//incremento della finestra di ricezione
  							seq_window++;
                            i++;
                            goto SEND;
  						}
  					}
                }
            }
            else {
WAIT:
  				//caso in cui la finestra di ricezione è piena
  				memset(buffer, 0, sizeof(buffer));

  				//attesa ack per decrementare la finestra di ricezione
  				result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
  								
  				if(result == -1) {
  					perror("errore, ricezione valore ack fallito\n");
  					exit(-1);
  				}
  				else {
  					value_ack = atoi(buffer);
  					seq_window--;
  					goto SEND;
  				}
  			}
   		}
    }
END:

    if(value_ack == (lunghezza_file+1)) {
        memset(buffer, 0, sizeof(buffer));

        printf("tutti i messaggi inviati hanno ricevuto ack\n");
    }
    else {
        memset(buffer, 0, sizeof(buffer));

        printf("non tutti i messaggi inviati hanno ricevuto ack\n");
	}
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