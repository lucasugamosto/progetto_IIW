#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <fcntl.h>			//libreria per le funzioni riguardanti i file
#include <sys/stat.h>

#include <sys/socket.h>		//definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>		//definisce i tipi di dato per rappresentare gli indirizzi IP in internet (es. struct sockaddr_in)
#include <arpa/inet.h>		//definisce le funzioni per manipolare gli indirizzi IP (conversione notazione puntata decimale <-> binario)
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>			//librerie per l'implementazione del comando "list"
#include <libgen.h>

#include <signal.h>
#include <sys/wait.h>

#define portnumber 2021
#define maxline 1024

int sd;				        //socket descriptor
int len, status;
char buffer[maxline];
struct sockaddr_in sad;		//struttura per l'indirizzo IP locale e numero di porta locale
struct dirent *dp;			//struttura per la gestione dei file del server
DIR *dir;					//descrittore del flusso di directory del server
pid_t pid;

void func_list();
void func_put();
void func_get();

int main(int argc, char *argv[]) {
	int result;
	int fd;					//file descriptor
	size_t val;

	//creazione della socket per il processo server (usata solo per l'ascolto di richieste)

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1) {
		perror("errore, creazione socket fallita\n");
		exit(-1);
	}

	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&sad, 0, sizeof(sad));
	sad.sin_family = AF_INET;					//tipo di indirizzo (IPv4)
	sad.sin_addr.s_addr = htonl(INADDR_ANY); 	//INADDR_ANY specifica l'accettazione di pacchetti in arrivo da ogni interfaccia di rete
	sad.sin_port = htons(portnumber);			//assegnazione del numero di porta del server

	//inizializzazione del processo locale su cui ricevere i dati

	result = bind(sd, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("errore, inizializzazione processo locale fallita\n");
		exit(-1);
	}

	//fase di attesa di richieste da parte dei client UDP e invio risposte
	printf("-----server in attesa di richieste-----\n");

	while(1) {
		len = sizeof(sad);

		//ricezione della richiesta da parte dei client

		result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
		if(result == -1) {
			perror("errore, ricezione richiesta fallita\n");
			exit(-1);
		}

		//comando LIST

		if(strcmp("list", buffer) == 0) {
			//creazione processi figli per la gestione delle richieste
			pid = fork();

			if(pid == -1) {
				perror("errore, creazione processo figlio fallito\n");
				exit(-1);
			}

			else if(pid == 0) {
				printf("processo figlio in esecuzione\n");
				printf("richiesta della lista dei file nel server\n(processo server figlio %d)\n", getpid());
				func_list();
				exit(0);
			}
			else {
				printf("processo padre, attesa terminazione processo figlio\n");
				wait(&status);
			}
		}

		//comando PUT

		else if(strcmp("put", buffer) == 0) {
			//creazione processi figli per la gestione delle richieste

			memset(buffer, 0, sizeof(buffer));

			result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);       //ricezione nome del file dal client
			if(result == -1) {
				perror("errore, ricezione richiesta fallita\n");
				exit(-1);
			}
			else {
				//caso in cui il file è presente nel client
				pid = fork();

				if(pid == -1) {
					perror("errore, creazione processo figlio fallito\n");
					exit(-1);
				}

				else if(pid == 0) {
					printf("processo figlio in esecuzione\n");
					printf("richiesta la scrittura di un file sul server\n(processo server figlio %d)\n", getpid());
					func_put();
					exit(0);
				}
				else {
				printf("processo padre, attesa terminazione processo figlio\n");
				wait(&status);
				}
			}
		}

		//comando GET

		else if (strcmp("get", buffer) == 0) {
			//creazione processi figli per la gestione delle richieste

			memset(buffer, 0, sizeof(buffer));

			//ricezione nome del file da leggere
			len = sizeof(sad);

			result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
			if(result == -1) {
				perror("errore, nome file non ricevuto\n");
				exit(-1);
			}

			pid = fork();

			if(pid == -1) {
				perror("errore, creazione processo figlio fallito\n");
				exit(-1);
			}

			else if(pid == 0) {
				printf("socket processo padre: %d\n", sd);
				printf("processo figlio in esecuzione\n");
				printf("richiesta lettura di un file del server\n(processo server figlio %d)\n", getpid());
				func_get();
				exit(0);
			}
			else {
				printf("socket processo padre: %d\n", sd);
				printf("processo padre, attesa terminazione processo figlio\n");
				wait(&status);
			}
		}
	}
	return(0);
}

void func_list() {
	int result;

	memset(buffer, 0, sizeof(buffer));					//svuotamento del buffer

	sleep(10);

	if ((dir = opendir ("server UDP")) == NULL) {		//controllo dei file presenti all'interno del server 
        perror ("errore, lettura file nel server fallita\n");
        exit (-1);
    }
			
	while ((dp = readdir (dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer

            strcat(buffer, dp->d_name);					//inserimento del file i-esimo all'interno del buffer
            strcat(buffer, "\n");				
        }

        if(strlen(buffer) == (maxline-1)) {				//quando il buffer è pieno lo si invia al client e si svuota
        	result = sendto(sd, buffer, strlen(buffer)+1, 0, (struct sockaddr *)&sad, sizeof(sad));
			if(result == -1) {
				perror("errore, invio risposta fallito\n");
				exit(-1);
			}
			memset(buffer,0,sizeof(buffer));			//svuotamento del buffer
		}			
    }

    closedir(dir);										//chiusura della directory dir

    //invio della risposta al client che ne ha fatto richiesta

	result = sendto(sd, buffer, strlen(buffer)+1, 0, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("errore, invio risposta fallito\n");
		exit(-1);
	}
}

void func_put() {
	int result, fd;

	sleep(10);

	fd = open(buffer, O_CREAT|O_RDWR, 0666);							//creazione del file sul server
	if(fd == -1) {
		perror("errore, creazione file fallita\n");
		exit(-1);
	}

	memset(buffer, 0, sizeof(buffer));

	result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);	//ricezione contenuto da inserire nel file creato
	if(result == -1) {
		perror("errore, ricezione contenuto file fallito\n");
		exit(-1);
	}

	if(write(fd, buffer, strlen(buffer)) == -1) {								//scrittura del contenuto sul file creato
		perror("errore, scrittura sul file fallita\n");
		exit(-1);
	}

	if(strlen(buffer) >= (maxline-1)) {
		//il file non è stato completamente ricevuto con un solo messaggio

  		while(strlen(buffer) == (maxline-1)) {			 //finchè il buffer si riempie
   			memset(buffer, 0, sizeof(buffer));			 //svuotamento del buffer

   			result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
			if(result == -1) {
				perror("errore, ricezione contenuto file fallito\n");
				exit(-1);
			}

   			if(write(fd, buffer, strlen(buffer)) == -1) {    //lettura dei dati dal file ed inserimento del buffer
            	perror("errore, lettura fallita\n");
            	exit(-1);
   			}	
   		}
   	}
}

void func_get() {
	int result, fd;
	char *pathname;
	size_t val;

	//allocazione di memoria per la variabile richiesta nel comando GET

	sleep(10);

	pathname = (char *)malloc(124);
	if(pathname == NULL) {
		perror("errore, allocazione di memoria fallita\n");
		exit(-1);
	}

	if ((dir = opendir ("server UDP")) == NULL) {				//controllo dei file presenti all'interno del server 
    	perror ("errore, lettura file nel server fallita\n");
        exit (-1);
    }

	result = 0;											//impongo variabile per definire la presenza o meno del file

	while ((dp = readdir (dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer

        	if(strcmp(dp->d_name, buffer) == 0) {		//controllo se file richiesto è presente nel server

        		result = 1;

        		strcpy(pathname, "server UDP/");
        		strcat(pathname, dp->d_name);

        		fd = open(pathname, O_RDONLY, 0660);	 		 		//apertura del file richiesto dal client
        		if(fd == -1) {
            		perror("errore, apertura del file fallita\n");
            		exit(-1);
        		}
        		free(pathname);

            	memset(buffer, 0, sizeof(buffer));			 		//svuotamento del buffer

            	val = read(fd, buffer, (maxline-1));		 		//lettura dei dati dal file ed inserimento del buffer

            	if(val == -1) {
            		perror("errore, lettura fallita\n");
            		exit(-1);
            	}
        		else {
        			result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
        			if(result == -1) {
						perror("errore, invio risposta fallito\n");
						exit(-1);
					}
   				}

   				if(strlen(buffer) >= (maxline-1)){
				//il file non è stato completamente inviato con un solo messaggio

   					while(strlen(buffer) == (maxline-1)) {			 //finchè il buffer si riempie
   						memset(buffer, 0, sizeof(buffer));			 //svuotamento del buffer

   						if(read(fd, buffer, (maxline-1)) == -1) {    //lettura dei dati dal file ed inserimento del buffer
            				perror("errore, lettura fallita\n");
            				exit(-1);
            			}

						result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&sad, sizeof(sad));
        				if(result == -1) {
							perror("errore, invio risposta fallito\n");
							exit(-1);
						}
   					}	
   				}
   			}
    	}
    }
    closedir(dir);						//chiusura della directory dir

    if(result == 0) {
       	//caso in cui il file non è presente nel server e viene inviato un buffer vuoto

        result = sendto(sd, NULL, 0, 0, (struct sockaddr *)&sad, sizeof(sad));
        if(result == -1) {
        	perror("errore, invio risposta fallito\n");
        	exit(-1);
        }
	}
}