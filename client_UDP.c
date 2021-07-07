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
	int result, len, c;
	char buffer[maxline];			//buffer contenente i messaggi di richiesta e di risposta
	
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
		printf("inserisci comando:  ");

		result = scanf("%s", buffer);										//inserimento del comando tramite scanf
		if(result == EOF) {
			perror("errore, lettura comando fallita\n");
			exit(-1);
		}
		if(result == 0) {
			perror("errore, nessun comando inserito\n");
			exit(-1);
		}

		//comando LIST (richiesta della lista dei file presenti nel server)

		if(strcmp("list", buffer) == 0) {
			printf("comando 'list' chiamato\n");

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
			printf("%s\n", buffer);											//stampa a schermo la lista dei file nel server

			printf("\n");
		}

		//comando GET (richiesta del contenuto dei file presenti nel server)

		else if(strcmp("get", buffer) == 0) {
			printf("comando 'get' chiamato\n");

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
				printf("%s", buffer);										//stampa a schermo del contenuto del file richiesto

				if(strlen(buffer) >= (maxline-1)) {								//controllo se il messaggio arrivato è completo

					//devono arrivare altre parti del file richiesto
					
					while(strlen(buffer) == (maxline-1)) {
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

		//comando PUT (upload di un file sul server)

		else if(strcmp("put", buffer) == 0) {
			printf("comando 'put' chiamato\n");

			memset(buffer, 0, sizeof(buffer));								//svuotamento del buffer

			printf("dati da inserire nel file:\n");
			scanf(" %[^\n]", buffer);										//permette di inserire stringhe contenente spazi

			c = getchar();

			//invio richiesta corrispondente al comando PUT

			result = sendto(sd, buffer, maxline, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
			if(result == -1) {
				perror("errore, invio richiesta fallito\n");
				exit(-1);
			}

			//ricezione risposta corrispondente al comando PUT

			result = recvfrom(sd, buffer, maxline, 0, NULL, NULL);			//con NULL si intende che non si sa chi invia la risposta
			if(result == -1) {
				perror("errore, ricezione risposta fallita\n");
				exit(-1);
			}

			printf("%s\n", buffer);
		}

		//NESSUN COMANDO PRECEDENTE RICHIESTO

		else {
			perror("errore, comando inserito non esistente\n");
		}
	}

	return(0);
}
