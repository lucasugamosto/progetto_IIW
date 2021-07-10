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

#define portnumber 2021
#define maxline 1024

int sd;				        		//socket descriptor
struct sockaddr_in servaddr;		//struttura per l'indirizzo IP locale e numero di porta locale
DIR *dir;							//descrittore del flusso di directory del server
struct dirent *dp;					//struttura per la gestione dei file del server
char buffer[maxline];			//buffer contenente i messaggi di richiesta e di risposta

void func_list(char *buffer);
void func_get(char *buffer);
void func_put(char *buffer);

int main(int argc, char *argv []) {
	int result, len, c;
	int fd;							//file descriptor
	char buffer[maxline];			//buffer contenente i messaggi di richiesta e di risposta
	size_t val;
	
	//controllo presenza dell'indirizzo IP passato come argomento della funzione
	
	if(argc < 2) {
		printf("nessun indirizzo IP inserito\n");
		exit(-1);
	}

	//creazione della socket per il processo client

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1) {
		perror("errore, creazione socket client fallita\n");
		exit(-1);
	}

	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;											//tipo di indirizzo (IPv4)
	servaddr.sin_port = htons(portnumber);									//assegnazione numero di porta del server

	result = inet_aton(argv[1], &(servaddr.sin_addr));						//assegnazione dell'indirizzo del server presente in argv[1]
	if(result == 0) {
		perror("errore, conversione dell'indirizzo IP fallito\n");
		exit(-1);
	}

	while(1) {
		memset(buffer, 0, sizeof(buffer));
		printf("inserisci comando:  ");

		result = scanf("%s", buffer);										//inserimento del comando tramite scanf
		if(result == EOF) {
			perror("errore, lettura comando fallita\n");
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

		//comando GET (richiesta del contenuto dei file presenti nel server)

		else if(strcmp("get", buffer) == 0) {
			func_get(buffer);
		}

		//comando PUT (upload di un file sul server)

		else if(strcmp("put", buffer) == 0) {
			func_put(buffer);
        }
	}
	return(0);
}

void func_list(char *buffer) {
	int result;


	//invio richiesta corrispondente al comando LIST

	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta fallito\n");
		exit(-1);
	}

	//ricezione risposta corrispondente al comando LIST

	result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);			//con NULL si intende che non si sa chi invia la risposta
	if(result == -1) {
		perror("errore, ricezione risposta fallita\n");
		exit(-1);
	}

	printf("\nfile contenuti nel server:\n");						
	printf("%s", buffer);											//stampa a schermo la lista dei file nel server

	while(strlen(buffer) == (maxline-1)) {
		result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);		//con NULL si intende che non si sa chi invia la risposta
		if(result == -1) {
			perror("errore, ricezione risposta fallita\n");
			exit(-1);
		}
		printf("%s", buffer);
	}
	printf("\n");
}

void func_get(char *buffer) {
	int result, c, fd;
	size_t val;

	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta fallito\n");
		exit(-1);
	}

	memset(buffer, 0, sizeof(buffer));								//svuotamento del buffer

	printf("nome del file da richiedere al server: ");
	scanf(" %[^\n]", buffer);										//permette di inserire stringhe contenente spazi
	c = getchar();

	//invio richiesta corrispondente al comando GET

	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta fallito\n");
		exit(-1);
	}

	//ricezione risposta corrispondente al comando GET

	result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);			//con NULL si intende che non si sa chi invia la risposta
	if(result == -1) {
		perror("errore, ricezione risposta fallita\n");
		exit(-1);
	}

	else if(result == 0) {
		perror("\nfile non presente nel server\n");
	}
	else {
		printf("\ndati contenuti nel file richiesto:\n");
		printf("%s", buffer);											//stampa a schermo del contenuto del file richiesto

		if(strlen(buffer) >= (maxline-1)) {								//controllo se il messaggio arrivato è completo

		//devono arrivare altre parti del file richiesto
					
			while(strlen(buffer) == (maxline-1)) {
				memset(buffer, 0, sizeof(buffer));

				result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);	//con NULL si intende che non si sa chi invia la risposta
				if(result == -1) {
					perror("errore, ricezione risposta fallita\n");
					exit(-1);
				}
				printf("%s", buffer);
			}
		}
	}
	printf("\n");
}

void func_put(char *buffer) {
	int result, c, fd;
	size_t val;

	//invio richiesta corrispondente al comando PUT

	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if(result == -1) {
		perror("errore, invio richiesta fallito\n");
		exit(-1);
	}

	//ricezione risposta corrispondente al comando PUT

	//invio al server il nome del file da salvare
	memset(buffer, 0, sizeof(buffer));

	printf("file da scrivere sul server: ");
	scanf(" %[^\n]", buffer);
	c = getchar();

	printf("\n");

	if((dir = opendir ("client UDP")) == NULL) {			 //controllo dei file presenti all'interno del client
        perror ("errore, lettura file nel server fallita\n");
        exit (-1);
    }

    result = 0;

	while ((dp = readdir (dir)) != NULL) {					 //lettura della directory appena creata contenente i nomi dei file
        if(dp->d_type == DT_REG) {							 //controllo del tipo di elemento da inserire nel buffer
            if(strcmp(dp->d_name, buffer) == 0) {			 //controllo se file richiesto è presente nel server

            	result = 1;

            	memset(buffer, 0, sizeof(buffer));
            	strcpy(buffer, "server UDP/");
            	strcat(buffer, dp->d_name);

            	result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            	if(result == -1) {
            		perror("errore, invio nome file fallito\n");
            		exit(-1);
            	}
            		
            	//invio dati contenuti nel file descritto da pathname

           		memset(buffer, 0, sizeof(buffer));
           		strcpy(buffer, "client UDP/");
           		strcat(buffer, dp->d_name);

            	fd = open(buffer, O_RDONLY, 0660);
            	if(fd == -1) {
            		perror("errore, apertura file fallita\n");
            		exit(-1);
            	}
     
            	memset(buffer, 0, sizeof(buffer));			 //svuotamento del buffer

            	val = read(fd, buffer, (maxline-1));		 //lettura dei dati dal file ed inserimento del buffer

            	if(val == -1) {
            		perror("errore, lettura fallita\n");
            		exit(-1);
            	}
        		else {
        			result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        			if(result == -1) {
						perror("errore, invio risposta fallito\n");
						exit(-1);
					}
   				}

   				if(strlen(buffer) >= (maxline-1)) {
 					//il file non è stato completamente inviato con un solo messaggio

   					while(strlen(buffer) == (maxline-1)) {			 //finchè il buffer si riempie
   						memset(buffer, 0, sizeof(buffer));			 //svuotamento del buffer

   						if(read(fd, buffer, (maxline-1)) == -1) {    //lettura dei dati dal file ed inserimento del buffer
            				perror("errore, lettura fallita\n");
            				exit(-1);
            			}

						result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
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
       	//caso in cui il file non è presente nel client
        printf("file '%s' non presente nel client\n", buffer);
	}
}
