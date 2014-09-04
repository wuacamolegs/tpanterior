/*
 ============================================================================
 Name        : Programa.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Deargsion : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <parser/metadata_program.h>
#include <parser/parser.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/config.h>
#include <commons/log.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

//#define IP "127.0.0.1"//falta leer el archivo de configuracion y sacar estos datos de ahi
//#define PUERTO "6667"


typedef struct {
	char ip[15];
	char puerto[4];
} t_regConfig;//definicion de la estructura del archivo de configuracion para q sea mas facil leerlo
char *ANSISOP_CONFIG;

char* IP;
char* PUERTO;
t_log* logger;


int main(int cantArgs, char **args) {
	//FILE *archivoConfig;
	FILE *script;
	char caracter, operacion = -1;
	char codigo[100000];
	int num=0;
	char* buffer = malloc(1000);

	int valor, tamanioTexto;
	char* texto = malloc(1);

	//log_create(NULL, "Programa", 1, LOG_LEVEL_TRACE);
	printf("==========\t Programa \t==========\n");

	t_config* configuracion = config_create(getenv("ANSISOP_CONFIG"));
	IP = config_get_string_value(configuracion, "IP");
	PUERTO = config_get_string_value(configuracion, "PUERTO");

	//TODO: Variable de entorno ANSISOP_CONFIG

	//Abro script, leo el contenido
	script = fopen(args[1],"r");

	//empiezo con el socket
	struct addrinfo hints;
	struct addrinfo *serverInfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// Permite que la maquina se encargue de verificar si usamos IPv4 o IPv6
	hints.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP

	getaddrinfo(IP, PUERTO, &hints, &serverInfo);	// Carga en serverInfo los datos de la conexion

	int serverSocket;
	serverSocket = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);

	connect(serverSocket, serverInfo->ai_addr, serverInfo->ai_addrlen);
	freeaddrinfo(serverInfo);	// No lo necesitamos mas


	if (script == NULL)
	{
		printf("\nError de apertura del archivo. \n\n");
    }
	else
	{
		fscanf(script,"%s", buffer);//borro el shellbag
		fgetc(script);
	    while (!feof(script))
	    {
	    	caracter = fgetc(script);
	    	codigo[num]=caracter;
	    	num++;
	    }

	    int tamanio=sizeof(char) * num-1;
	    char mensaje[tamanio+1];
	    memcpy(mensaje, codigo, tamanio);
	    mensaje[tamanio+1] = '\0';
	    //printf("socket %d\n",serverSocket);
	    send(serverSocket, mensaje,tamanio, 0);
	    while (1)
	    {
	    	recv(serverSocket, &operacion, sizeof(char), 0);
	    	if(operacion == 0)	//Imprimir valor
	    	{
	    		recv(serverSocket, &valor, sizeof(int), 0);
	    		printf("%d\n", valor);
	    	}
	    	else if(operacion == 1)	//Imprimir texto
	    	{
	    		recv(serverSocket, &tamanioTexto, sizeof(int), 0);
	    		texto = realloc(texto,tamanioTexto);
	    		recv(serverSocket, texto, tamanioTexto, 0);
	    		texto[tamanioTexto] = '\0';
	    		printf("%s\n", texto);
	    	}
	    	else
	    	{
	    		break;
	    	}
	    }
    }
	free(buffer);
	fclose(script);
	close(serverSocket);

	return 0;
}


