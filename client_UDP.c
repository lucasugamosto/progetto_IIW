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

#define portnumber 2021
#define maxline 1024

int sd;				        		//socket descriptor
struct sockaddr_in servaddr;		//struttura per l'indirizzo IP locale e numero di porta locale

int main(int argc, char *argv []) {
	int result, len;
	char buffer[maxline];
	
	//controllo presenza dell'indirizzo IP passato come argomento della funzione
	
	if(argc < 2) {
		printf("nessun indirizzo IP inserito\n");
		exit(-1);
	}
	printf("indirizzo IP inserito\n");

	//creazione della socket per il processo client

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sd == -1) {
		perror("errore creazione socket client");
		exit(-1);
	}
	printf("descrittore della socket creata: %d\n", sd);

	//inizializzazione dell'indirizzo IP e del numero di porta

	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;						//tipo di indirizzo (IPv4)
	servaddr.sin_port = htons(portnumber);				//assegnazione numero di porta del server

	result = inet_aton(argv[1], &(servaddr.sin_addr));	//assegnazione dell'indirizzo del server presente in argv[1]
	if(result == 0) {
		perror("errore conversione dell'indirizzo IP\n");
		exit(-1);
	}
	printf("conversione dell'indirizzo IP riuscita\n");

	while(1) {
		printf("inserisci comando:  ");

		result = scanf("%s", buffer);
		if(result == EOF) {
			perror("errore lettura comando\n");
			exit(-1);
		}
		if(result == 0) {
			perror("errore compilazione nulla\n");
			exit(-1);
		}
		printf("lettura comando convalidata\n");

		//comando LIST

		if(strcmp("list", buffer) == 0) {
			printf("comando 'list' chiamato\n");

			//invio richiesta corrispondente al comando LIST

			result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
			if(result == -1) {
			perror("errore invio richiesta\n");
			exit(-1);
			}

			//ricezione risposta corrispondente al comando LIST

			result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);		//con NULL si intende che non si sa chi invia la risposta
			if(result == -1) {
				perror("errore ricezione risposta\n");
				exit(-1);
			}

			printf("%s\n", buffer);										//stampa a schermo della lista dei file nel server

			printf("fine comando LIST nel processo client\n");
		}

		//comando GET

		else if(strcmp("get", buffer) == 0) {
			printf("comando 'get' chiamato\n");
		}

		//comando PUT

		else if(strcmp("put", buffer) == 0) {
			printf("comando 'put' chiamato\n");
		}

		else {
			perror("errore comando inserito non esistente\n");
			exit(-1);
		}
	}

	return(0);
}
