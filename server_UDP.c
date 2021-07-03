#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>

#include <sys/socket.h>		//definisce i parametri che iniziano per PF e AF
#include <netinet/in.h>		//definisce i tipi di dato per rappresentare gli indirizzi IP in internet (es. struct sockaddr_in)
#include <arpa/inet.h>		//definisce le funzioni per manipolare gli indirizzi IP (conversione notazione puntata decimale <-> binario)
#include <sys/types.h>
#include <netdb.h>

#include <dirent.h>			//librerie per l'implementazione del comando "list"
#include <libgen.h>

#define portnumber 2021
#define maxline 1024

int sd;				        //socket descriptor
struct sockaddr_in sad;		//struttura per l'indirizzo IP locale e numero di porta locale
DIR *dir;					//descrittore del flusso di directory del server
struct dirent *dp;			//struttura per la gestione dei file del server

int main(int argc, char *argv[1]) {
	int result, len;
	char buffer[maxline];

	//creazione della socket per il processo server (usata solo per l'ascolto di richieste)

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1) {
		perror("errore creazione socket client\n");
		exit(-1);
	}
	printf("descrittore della socket creata: %d\n", sd);

	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&sad, 0, sizeof(sad));
	sad.sin_family = AF_INET;					//tipo di indirizzo (IPv4)
	sad.sin_addr.s_addr = htonl(INADDR_ANY); 	//INADDR_ANY specifica l'accettazione di pacchetti in arrivo da ogni interfaccia di rete
	sad.sin_port = htons(portnumber);			//assegnazione del numero di porta del server

	//inizializzazione del processo locale su cui ricevere i dati

	result = bind(sd, (struct sockaddr *)&sad, sizeof(sad));
	if(result == -1) {
		perror("errore inizializzazione processo\n");
		exit(-1);
	}

	//fase di attesa di richieste da parte dei client UDP e invio risposte

	while(1) {
		len = sizeof(sad);

		//ricezione della richiesta da parte dei client

		result = recvfrom(sd, buffer, maxline, 0, (struct sockaddr *)&sad, &len);
		if(result == -1) {
			perror("errore ricezione richiesta\n");
			exit(-1);
		}

		printf("%s\n", buffer);

		//comando LIST

		if(strcmp("list", buffer) == 0) {
			printf("richiesta della lista dei file nel server\n");

			memset(buffer, 0, sizeof(buffer));					//svuotamento del buffer

			if ((dir = opendir ("server UDP")) == NULL) {		//controllo dei file presenti all'interno del server 
        		perror ("errore lettura file nel server");
        		exit (-1);
    		}
			
			while ((dp = readdir (dir)) != NULL) {				//lettura della directory appena creata contenente i nomi dei file
        		if(dp->d_type == DT_REG) {						//controllo del tipo di elemento da inserire nel buffer
            		//printf("%s\n", dp->d_name);
            		strcat(buffer, dp->d_name);					//inserimento del file i-esimo all'interno del buffer
            		strcat(buffer, "\n");					
        		}
        	}

        	closedir(dir);										//chiusura della directory dir

        	//invio della risposta al client che ne ha fatto richiesta

			result = sendto(sd, buffer, strlen(buffer)+1, 0, (struct sockaddr *)&sad, sizeof(sad));
			if(result == -1) {
				perror("errore invio risposta\n");
				exit(-1);
			}
		}

		printf("nessun comando inserito\n");
	}

	return(0);
}