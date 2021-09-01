#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>
#include <fcntl.h>					   //libreria per le funzioni riguardanti i file
#include <sys/stat.h>
#include <math.h>

#include <time.h>					   //libreria utilizzata per il calcolo del timeout adattivo e del numero randomico
#include <sys/time.h>
#include <signal.h>
#include <sys/errno.h>

#include <sys/socket.h>				   //definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>				   //definisce i tipi di dato per rappresentare gli indirizzi IP in internet
#include <arpa/inet.h>				   //definisce le funzioni per manipolare gli indirizzi IP
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>					   //libreria per l'implementazione del comando "list"
#include <libgen.h>					   //libreria per l'implementazione del comando "list"

#define standard_port 1024			   //porta iniziale assunta da ogni client che si collega al server
#define maxline 512					   //dimensione massima dei messaggi inviati e ricevuti
#define N 4 						   //dimensione dellafinestra di ricezione
#define exit_code_client 1             //codice da inviare al server affinchè avvenga la chiusura della socket
#define exit_code_server 2             //codice ricevuto dal server affinchè avvenga la chiusura della socket
#define PROB_PERDITA 10				   //probabilità di perdita del pacchetto nella rete
#define STATIC_TIMEOUT 5000            //tempo dopo il quale arriva il segnale di errore SIGALRM [millisec]

//struttura per la gestione del GO-BACK-N
typedef struct message_struct {
	int message_pointer;			   //definisce l'ultimo byte letto dal messaggio
	char message_buffer[maxline];	   //definisce il buffer in cui inserire il contenuto del file
} message;

int sd;				        		   //socket descriptor
int port_number;					   //numero della porta ricevuto dal server per avviare la comunicazione
int len;							   //contiene il calcolo della dimensione della struttura sockaddr
int lunghezza_file;					   //quantità di byte di cui si compone il file da trasferire
int num_message;					   //numero di messaggi utili per trasferire completamente il file
int value_ack;						   //variabile che tiene conto dell'ultimo byte ricevuto con successo
int chose_timeout;					   //a seconda del suo valore (0 o 1) permette di scegliere il timeout da usare
double ADAPTIVE_TIMEOUT = 1; 	       //valore iniziale dell'intervallo di timeout modificabile con il calcolo di RTT [sec]

char buffer[maxline];				   //buffer contenente i messaggi di richiesta e di risposta

struct sockaddr_in servaddr;		   //struttura per l'indirizzo IP locale e numero di porta locale
struct dirent *dp;					   //struttura per la gestione dei file del server

DIR *dir;							   //descrittore del flusso di directory del server

size_t val;							   //variabile per la gestione delle funzioni riguardanti i file
clock_t start_time[N], end_time[N];    //variabili utilizzate per inizializzare e terminare il conteggio del tempo
struct timeval  tv1, tv2, tv3, tv4;        //variabili utilizzate per la valutazione del tempo di esecuzione della funzione
double sample_RTT;					   //variabile contenente la differenza di tempo tra invio/ricezione pacchetto
double estimated_RTT = 0;
double dev_RTT = 0;

void func_list(char *buffer);
void func_get(char *buffer);
void func_put(char *buffer);
void ricezione_GBN(char *pathname);
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file);
int create_connection(char *buffer, int port_number);
void setTimeout(double time, int id, int tempo_scelto);
void invio_ACK(int valore_ack);
void exit_handler(int signo);

int main(int argc, char *argv[]) {
	int result;

	//gestione del segnale collegato all'interruzione del processo per mezzo dell'utente
	signal(SIGINT, exit_handler);
	
	//controllo la presenza dell'indirizzo IP passato come argomento della funzione
	if(argc < 2) {
		perror("Nessun indirizzo IP inserito in input\n");
		exit(-1);
	}
	
	//creazione della socket per la connessione con il processo server
	sd = create_connection(argv[1], standard_port);
    
    //comunico al server l'esistenza del nuovo client
	if(sendto(sd, NULL, 0, 0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("Errore, comunicazione con il server fallita\n");
		exit(-1);
	}
	
	//svuotamento del buffer e calcolo grandezza della struttura sockaddr
	memset(buffer, 0, sizeof(buffer));
    len = sizeof(servaddr);

	//ricezione del nuovo numero di porta ricevuto dal server
	result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
	if(result < 0) {
		perror("Errore, ricezione del nuovo numero di porta fallito\n");

		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));

		//chiusura della socket e uscita dal processo
		close(sd);
		exit(-1);
	}

	//chiusura della precendente connessione (con socket di ascolto) con il server
	close(sd);
	
	//conversione della stringa contenente il nuovo numero di porta
	port_number = atoi(buffer);

    while(1) {
    	//creazione della nuova socket per la comunicazione con il processo figlio del server
    	sd = create_connection(argv[1], port_number);

		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));

		printf("\nConnessione processo client <-> processo figlio server riuscita (port_number:%d)\n\n", port_number);

		printf("Scegliere tra le seguenti possibili operazioni:\n");
		printf("- list : ritorna i files presenti nel server\n- get  : ricezione del file richiesto, se presente nel server\n");
		printf("- put  : invio del file richiesto, se presente nel client\n- exit : chiusura della connessione col server\n\n");
		printf("Inserisci comando: ");

		//inserimento del comando tramite scanf
		result = scanf("%s", buffer);
        
		if(result == EOF) {
			perror("Errore, lettura del comando fallita\n");
			exit(-1);
		}
		else if(result == 0) {
			perror("Errore, nessun comando inserito\n");
			exit(-1);
		}
		//comando EXIT (uscita dal processo e disconnessione dal processo server)
		else if(strcmp("exit", buffer) == 0) {
			printf("Chiusura della connessione con il processo server richiesta\n");

			//invio buffer contenente il comando "exit" al server per bloccare il processo server
			result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
			if(result == -1) {
				perror("Errore, invio richiesta 'EXIT' fallito\n");
				exit(-1);
			}
			else {
				//svuotamento del buffer
				memset(buffer, 0, sizeof(buffer));

				//chiusura della socket
				close(sd);

				printf("----Chiusura della connessione riuscita----\n");
				exit(1);
			}
		}
		//comando LIST (richiesta della lista dei file presenti nel server)
		else if(strcmp("list", buffer) == 0) {
			func_list(buffer);
		}

		//comando GET (richiesta di download di un file presente nel server)
		else if(strcmp("get", buffer) == 0) {
			//scelta del timeout da considerare per il trasferimento del file
INSERISCI_VALUE_GET:
			printf("\nScegliere tra i seguenti timeout:\n- Timeout statico  -> inserire 0\n- Timeout adattivo -> inserire 1\n");
			printf("\nTimeout da utilizzare: ");
			scanf("%d", &chose_timeout);

			if(chose_timeout != 0 && chose_timeout != 1) {
				printf("\nNumero inserito non corrisponde a nessun timeout. Riprovare\n");
				goto INSERISCI_VALUE_GET;
			}
			func_get(buffer);
		}

		//comando PUT (richiesta di upload di un file sul server)
		else if(strcmp("put", buffer) == 0) {
			//scelta del timeout da considerare per il trasferimento del file
INSERISCI_VALUE_PUT:
			printf("\nScegliere tra i seguenti timeout:\n- Timeout statico  -> inserire 0\n- Timeout adattivo -> inserire 1\n");
			printf("\nTimeout da utilizzare: ");
			scanf("%d", &chose_timeout);


			if(chose_timeout != 0 && chose_timeout != 1) {
				printf("\nNumero inserito non corrisponde a nessun timeout. Riprovare\n");
				goto INSERISCI_VALUE_PUT;
			}
			func_put(buffer);
        }
        else {
        	printf("Comando inserito non esistente. Riprovare\n");
        }
	}
	return(0);
}

/*funzione utilizzata per la richiesta LIST. Consiste nel creare uno o più buffer
contenenti i file presente nel server a cui si fa riferimento*/
void func_list(char *buffer) {
	int result;

	//invio richiesta corrispondente al comando LIST
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio richiesta 'LIST' fallito\n");
		exit(-1);
	}

	//ricezione risposta corrispondente al comando LIST
	result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);          //(NULL intende che non si sa chi invia la risposta)
	if(result == -1) {
		perror("Errore, ricezione risposta 'LIST' fallita\n");
		exit(-1);
	}
	//Controllo che nel buffer non ci sia il codice di uscita
	else if(atoi(buffer) == exit_code_server) {
		printf("Server ha smesso di funzionare\n");
		exit(1);
	}
	else if(strlen(buffer) == 0) {
		printf("Nessun file presente nel server\n");
		goto CHIUSURA_SOCKET;
	}

	printf("\nFile presenti nel server:\n");

	while(strlen(buffer) >= (maxline-1)) {
		result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);		//(NULL intende che non si sa chi invia la risposta)
		if(result == -1) {
			perror("Errore, ricezione risposta 'LIST' fallita\n");
			exit(-1);
		}
		//Controllo che nel buffer non ci sia il codice di uscita
		else if(atoi(buffer) == exit_code_server){
			printf("Server ha smesso di funzionare\n");
			exit(1);
		}
		printf("%s", buffer);
	}
	printf("%s", buffer);

CHIUSURA_SOCKET:
	//svuotamento del buffer e chiusura della socket
	memset(buffer, 0, sizeof(buffer));
	close(sd);
}

/*funzione utilizzata per la richiesta GET. Permette di trasferire un file richiesto dal client
che è presente all'interno del server*/
void func_get(char *buffer) {
	int result, c;
	char *pathname;

	//allocazione di memoria per la definizione del percorso del file
	pathname = (char *)malloc(124);
	if(pathname == NULL) {
		perror("Errore, allocazione di memoria fallita\n");
		exit(-1);
	}
	//invio richiesta corrispondente al comando GET
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio richiesta 'GET' fallito (1)\n");
		exit(-1);
	}
	//invio variabile chose_timeout per far sapere al server quale timeout usare
	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "%d", chose_timeout);

	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio variabile chose_timeout fallito\n");
		exit(-1);
	}

	//svuotamento del buffer e calcolo della grandezza della struttura sockaddr
	memset(buffer, 0, sizeof(buffer));
	len = sizeof(servaddr);

	//inserimento nel buffer del nome del file da scaricare
	printf("\nFile da richiedere al server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	//salvataggio del percorso del file da aprire
	strcpy(pathname, "client UDP/");
    strcat(pathname, buffer);

	//invio richiesta corrispondente al comando GET (invio del nome del file da voler scaricare)
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio richiesta 'GET' fallito (2)\n");
		exit(-1);
	}

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    //funzione per la gestione del GO-BACK-N (parametro 'pathname' è il percorso dove salvare il file)
    ricezione_GBN(pathname);
}

/*funzione utilizzata per la richiesta PUT. Permette di inserire un file inviato dal client
all'interno del server, se e solo se il file è effettivamente presente nel client*/
void func_put(char *buffer) {
	int result, c, fd;

	//invio richiesta corrispondente al comando PUT
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio richiesta 'PUT' fallito\n");
		exit(-1);
	}
	//invio al server il nome del file da salvare
	memset(buffer, 0, sizeof(buffer));

	//inserimento nel buffer del nome del file da inviare
	printf("File da scrivere sul server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	//apertura della directory del client e controllo dei file presenti in essa
	if((dir = opendir ("client UDP")) == NULL) {
        perror ("Errore, lettura file nel client fallita\n");
        exit (-1);
    }
    //si inizializza la variabile result a 0
    //se alla fine result = 0: file non presente nel client
    //se alla fine result = 1: file presente nel client
    result = 0;

	while ((dp = readdir(dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer
            if(strcmp(dp->d_name, buffer) == 0) {		//controllo se il file richiesto è presente nel client

            	//variabile impostata per indicare che il file è presente nel client
            	result = 1;
            	memset(buffer, 0, sizeof(buffer));

            	//creazione del percorso sul lato server dove salvare il file
            	strcpy(buffer, "server UDP/");
            	strcat(buffer, dp->d_name);
                
                //invio il nome del file da voler inviare sul server
            	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            	if(result == -1) {
            		perror("Errore, invio nome file fallito\n");
            		exit(-1);
            	}
            	//svuotamento del buffer
                memset(buffer, 0, sizeof(buffer));

                //creazione del percorso sul lato client dove prelevare il file
            	strcpy(buffer, "client UDP/");
            	strcat(buffer, dp->d_name);
            	
            	fd = open(buffer, O_RDONLY, 0666);
            	if(fd == -1) {
            		perror("Errore, apertura file da inviare fallita\n");
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
                	perror("Errore, invio lunghezza del file fallito\n");
                	exit(-1);
                }

                //riposizionamento del puntatore all'inizio del file per la futura lettura
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

                //inserimento nel buffer del numero di pacchetti da inviare
                memset(buffer, 0, sizeof(buffer));
                sprintf(buffer, "%d", num_message);

                //invio informazioni sul numero di pacchetti da inviare al client
                result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                if(result == -1) {
                    perror("Errore, invio del numero di pacchetti fallito\n");
                    exit(-1);
                }
    
                //svuotamento del buffer
                memset(buffer, 0, sizeof(buffer));

                //inizializzazione delle strutture 'message'
                message pack[num_message];

                //pulizia dei campi di ogni struttura allocata
                for(int i = 0; i < num_message; i++) {
                    memset(pack[i].message_buffer, 0, maxline);
                    pack[i].message_pointer = 0;
                }
            	
            	//funzione utilizzata per l'invio dei pacchetti al server
                invio_GBN(pack, num_message, fd, lunghezza_file);

                //riposizionamento del puntatore all'interno del file
                lseek(fd, 0, SEEK_SET);

                //chiusura del descrittore del file precedentemente aperto
            	close(fd);

                goto END;
            }
        }
    }
END:
    if(result == 0) {
       	//caso in cui il file non è presente nel client
        printf("----File richiesto non presente nel client----\n\n");

        result = sendto(sd, NULL, 0, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        if(result == -1) {
        	perror("Errore, invio informazioni al server fallita (PUT)\n");
        	exit(-1);
        }
	}

	//chiusura della directory dir per lo scorrimento e analisi dei file
    closedir(dir);

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
}

/*funzione utilizzata da 'func_get' per inserire il file ricevuto dal server nel client e per inviare al server
informazioni sullo stato degli ack*/
void ricezione_GBN(char *pathname) {
    int valore_atteso = 0;				//valore che ci si aspetta dal prossimo pacchetto arrivato affinchè sia in ordine
    int lastAckCorrect = 0;				//valore del byte immediatamente successivo da scaricare
    int prob_rvd_file;					//probabilità di ricezione del pacchetto dalla rete
    int lastByteReceived = 0;			//valore dell'ultimo byte dell'ultimo messaggio ricevuto
    int result, fd, i;
    int count_retransmit = 0;			//variabile per il conteggio delle ritrasmissioni effettuate
    time_t t;							//variabile per la generazione del valore randomico della probabilità

    //inizializzazione del genereatore di numeri random
    srand((unsigned) time(&t));

    //svuotamento del buffer
    memset(buffer, 0, sizeof(buffer));

    //ricezione della lunghezza del file da ricevere o NULL in caso file non presente nel server
    result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
    if(result == -1) {
    	perror("Errore, ricezione lunghezza del file fallito\n");
    	exit(-1);
    }
    //Controllo che nel buffer non ci sia il codice di uscita
	else if(atoi(buffer) == exit_code_server) {
		printf("Server ha smesso di funzionare\n");
		exit(1);
	}
    else if(result == 0) {
    	//caso in cui il file non è presente sul server
    	printf("----File richiesto non presente nel server----\n\n");
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
    		perror("Errore, ricezione numero di pacchetti fallito\n");
    		exit(-1);
    	}
    	//Controllo che nel buffer non ci sia il codice di uscita
		else if(atoi(buffer) == exit_code_server) {
			printf("Server ha smesso di funzionare\n");
			exit(1);
		}

    	//creazione e apertura del file per lettura/scrittura
    	fd = open(pathname, O_CREAT|O_RDWR, 0666);
        if(fd == -1) {
            perror("Errore, apertura del file fallita\n");
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
        
        //inizio conteggio tempo 
        gettimeofday(&tv3, NULL);

		while(i < num_message) {
LOOP:
			//ricezione del contenuto del messaggio i-esimo
			result = recvfrom(sd, pack[i].message_buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
			if(result == -1) {
				perror("Errore, ricezione messaggio di risposta fallita\n");
				exit(-1);
			}
			else {
				//svuotamento del buffer
				memset(buffer, 0, sizeof(buffer));

				//ricezione dell'ultimo byte di cui si compone il messaggio da ricevere
				result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);
				if(result == -1) {
					perror("Errore, ricezione ultimo byte del messaggio fallito\n");
					exit(-1);
				}
				//Controllo che nel buffer non ci sia il codice di uscita
				else if(atoi(buffer) == exit_code_server){
					printf("Server ha smesso di funzionare\n");
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
								perror("Errore, scrittura sul file client fallita\n");
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
							printf("messaggio n°.%d PERSO\n", i);

							goto ACK_PERSO;
						}

						//inserimento del messaggio i-esimo all'interno del file creato nel client
						val = write(fd, pack[i].message_buffer, strlen(pack[i].message_buffer));
						if(val == -1) {
							perror("Errore, scrittura sul file client fallita\n");
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

		printf("----File salvato nel CLIENT con successo----\n\n");
	}
	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    //stampo tempo esecuzione
    gettimeofday(&tv4, NULL);//fermo il timer
	printf("Downlaod total time = %f seconds\n\n",(double) (tv4.tv_usec - tv3.tv_usec) / 1000000 +(double) (tv4.tv_sec - tv3.tv_sec));
    

	//chiusura della socket
	close(sd);
}

/*funzione utilizzata da 'func_put' per inviare alla funzione server il file da salvare nella cartella
associata e per gestire la ricezione degli ack ricevuti dal server*/
void invio_GBN(message *pack, int num_message, int fd, int lunghezza_file) {
    int result, i;
    int count_ack = 0;			//variabile che tiene traccia del numero di ack ricevuti
    int seq_window = 0;			//variabile utilizzata per la gestione della finestra di ricezione
    int fast_retransmit = 0;	//contatore che conta gli ack uguali
    int ack_prev = 0;			//variabile utilizzata con fast_retransmit per conoscere il valore del precedente ack

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
    //inizializzo a 0 l'indice per lo scorrimento dei pacchetti
    i = 0;
    len = sizeof(servaddr);
    value_ack = 0;
    //inizio conteggio tempo 
    gettimeofday(&tv3, NULL);
    
    while(1) {       	
SEND:
    	if(i == num_message) {
            //caso in cui tutti i pacchetti sono stati inviati al server 
            i++;
            goto WAIT;          
        }
        else if(i > num_message) {
            //caso in cui tutti i pacchetti sono stati inviati e si attende ricezione ack
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
           		
           		printf("Stato di SEND -> invio pacchetto n.%d al server\n", i);

            	//invio contenuto del file (pack[i].message_buffer) al server
            	result = sendto(sd, pack[i].message_buffer, strlen(pack[i].message_buffer), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

            	if(result == -1) {
            		perror("Errore, invio messaggio al server (1) fallito\n");
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

  					result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  					if(result == -1) {
  						perror("Errore, invio messaggio al server (2) fallito\n");
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
				printf("\nStato di WAIT -> attesa di ack n.%d dal server\n", count_ack);

  				memset(buffer, 0, sizeof(buffer));

  				//variabile utilizzata per la gestione degli errori (definita nella livreria errno.h)
  				errno = 0;

  				//attesa ack per decrementare la finestra di ricezione
  				result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, &len);

				//calcolo indice dell'ack arrivato dal client
                int indice_pacchetto = (atoi(buffer)/maxline)-1;

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
				else if(atoi(buffer) == exit_code_server) {
					printf("Server ha smesso di funzionare\n");
					exit(1);
				}
  				else {
  					//ack ricevuto con successo, controllo il suo valore
  					value_ack = atoi(buffer);

  					if(value_ack > ack_prev) {
  						//arrivo dell'ack successivo desiderato (nuovo ack in ordine)

  						if(chose_timeout == 1) {
  							int indice_pacchetto = (value_ack/maxline)-1;

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
  							i = ((value_ack-1) / maxline);
  							printf("\n3° ack duplicato, FAST RETRANSMIT da pack n°.%d\n", i);

  							//aggiorno le variabili locali
  							fast_retransmit = 0;
  							seq_window = 0;
  							count_ack = i;

  							goto SEND;
  						}
  						
  						if (seq_window<N){ //sono nel caso in cui ho perso l'ack dell'ultimo pacchetto
                            //faccio ripartire il timeout
                            printf("Entro nella condizione ultimo ack perso\n");
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
	printf("----File inviato nel SERVER con successo----\n\n");

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));
    
    gettimeofday(&tv4, NULL);//fermo il timer
    
	printf("Downlaod total time = %f seconds\n\n",(double) (tv4.tv_usec - tv3.tv_usec) / 1000000 +(double) (tv4.tv_sec - tv3.tv_sec));

	//chiusura della socket
	close(sd);
}
 
/*funzione utilizzata nel main per mettere in comunicazione il processo figlio del lato server
con il processo appena creato del lato client*/
int create_connection(char *buffer,int port_number) {
    int result, des;

    //creazione della socket per il processo client
	des = socket(AF_INET, SOCK_DGRAM, 0);
	if(des == -1) {
		perror("Errore, creazione socket client fallita\n");
		exit(-1);
	}
	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&servaddr, 0, sizeof(servaddr));				//svuotamento iniziale della struttura server
	servaddr.sin_family = AF_INET;								//tipo di indirizzo (IPv4)
	servaddr.sin_port = htons(port_number);						//assegnazione numero di porta del server

	result = inet_aton(buffer, &(servaddr.sin_addr));			//assegnazione dell'indirizzo del server presente in buffer
	if(result == 0) {
		perror("Errore, conversione dell'indirizzo IP fallito\n");
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
	setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

/*funzione per la creazione e invio di buffer contenenti il valore di ack
da inviare all'altro processo comunicante*/
void invio_ACK(int valore_ack) {
	int result;

	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "%d", valore_ack);

	//invio ack per conferma dell'ordine corretto di arrivo dei pacchetti al server
	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("Errore, invio dell'ack al server fallito\n");
		exit(-1);
	}
	else {
		printf("lastAckSend: %d\n", valore_ack);
		//svuotamento del buffer
		memset(buffer, 0, sizeof(buffer));
	}
}

/*funzione per la gestione del segnale e invio dello stesso al server affinchè
possa chiudersi da entrambi i lati la connessione socket creata*/
void exit_handler(int signo) {
	int res;

	printf("\nSegnale di interruzione generato dall'utente\n");

	//svuotamento del buffer
	memset(buffer, 0, sizeof(buffer));

	//invio al server il buffer contenente un codice per terminare il processo
	sprintf(buffer, "%d", exit_code_client);
	
	res = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(res == -1) {
		perror("Errore, invio codice terminazione fallito\n");
		exit(-1);
	}
	else {
		//chiusura della socket
		close(sd);
		printf("chiusura connessione associata a port_number: %d riuscita\n", port_number);

		//svuotamento della memoria
		memset(buffer, 0, sizeof(buffer));
		exit(1);
	}
}
