/*
 ============================================================================
 Name        : umv.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/string.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <commons/config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <semaphore.h>
#include <commons/log.h>


#define PUERTO "6668"
#define BACKLOG 5			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo

/* Tabla de segmentos */
typedef struct segmento
{
	int idProceso;
	int idSegmento;
	int base;
	int tamanio;
	void* dirInicio;
	struct segmento *siguiente;
} t_segmento;

enum{
	kernel,
	cpu,
	programa
};

enum{
	operSolicitarBytes,
	operCrearSegmento,
	operDestruirSegmentos,
	operEnviarBytes
};

/* Funciones */
void* solicitarBytes(int base, int offset, int tamanio);
int enviarBytes(int base, int offset, int tamanio, void* buffer);
void* mainConsola();
void destruirSegmentos(int id);
void* mainEsperarConexiones();
int crearSegmento(int idProceso, int tamanio);
t_segmento* buscarSegmento(int base);
void* posicionarSegmento(int algoritmo, int tamanio);
void* first_fit(int tamanio);
void* worst_fit(int tamanio);
int nuevoIDSegmento(int idProceso);
void insertarSegmento(t_segmento* segmento);
void* compactar(void);
void dump(bool mostrarPantalla);
void mostrarEstructuras(bool mostrarPantalla);
void mostrarMemoria(bool mostrarPantalla);
void mostrarContenidoDeMemoria(bool mostrarPantalla, int offset, int tamanio);
void imprimirSegmento(t_segmento* segmento);
int cambioProcesoActivo(int idProceso);
int handshake(int id);
void* f_hiloKernel(void* socketCliente);
void* f_hiloCpu(void* socketCliente);


/* Variables globales */
void* memPpal;
void* finMemPpal;
t_segmento* tablaSegmentos = NULL;
int algoritmo = 0;	// 0 = FirstFit
int procesoActivo = 2;
int retardo = 0;
t_log* logi;

/* Semaforos */
sem_t s_cambioProcesoActivo;
sem_t s_TablaSegmentos;
sem_t s_cpu;


int main (int cantArgs, char** args)
{
	pthread_t consola, esperarConexiones;
	int rhConsola, rhEsperarConexiones;
	int i;
	sem_init(&s_cambioProcesoActivo,0,1);
	sem_init(&s_TablaSegmentos,0,1);
	sem_init(&s_cpu,0,1);

	printf("==========\t UMV \t==========\n");

	t_config* configuracion = config_create(args[1]);
	logi = log_create(args[2], "UMV", 0, LOG_LEVEL_TRACE);
	int sizeMem = config_get_int_value(configuracion, "sizeMemoria");

	memPpal = malloc (sizeMem);	//El tamaño va por configuracion
	char* aux = (char*) memPpal;
	finMemPpal = memPpal + sizeMem; //El tamaño va por configuracion
	for(i=0; i<sizeMem; i++){aux[i] = 0;}

	rhConsola = pthread_create(&consola, NULL, mainConsola, NULL);
	rhEsperarConexiones = pthread_create(&esperarConexiones, NULL, mainEsperarConexiones, NULL);

	pthread_join(consola, NULL);
	pthread_join(esperarConexiones, NULL);

	printf("%d",rhConsola);
	printf("%d",rhEsperarConexiones);

	exit(0);
}

/* Hilo consola */
void* mainConsola()
{
	char* parametros = malloc(1000);
	char mostrarPantalla;
	int offset;
	int tamanio;
	int confirmacion;
	while(1)
	{
		gets(parametros);
		//scanf("%s",parametros);
		//sleep(retardo);
		log_info(logi, "Llego una solicitud desde consola");
		if(string_equals_ignore_case(parametros,"man"))
		{
			printf("Operaciones:\n");
			printf("\toperacion solicitar [pid] [base] [offset] [tamanio]\n");
			printf("\toperacion escribir [pid] [base] [offset] [tamanio]\n");
			printf("\toperacion crear-segmento [pid] [tamanio]\n");
			printf("\toperacion destruir-segmentos [pid]\n");
			printf("\tretardo [tiempo]\n");
			printf("\talgoritmo first-fit / worst-fit\n");
			printf("\tcompactacion\n");
			printf("\tdump (tabla-segmentos / mostrar-memoria / contenido-memoria)\n");
			continue;
		}
		if(string_starts_with(parametros,"operacion "))
		{
			char* resto = string_substring_from(parametros,10);
			if(string_starts_with(resto,"solicitar "))
			{
				char* resto2 = string_substring_from(resto,10);
				char* pid;
				char* tamanio;
				char* base;
				char* offset;
				pid = strtok(resto2, " ");
				base = strtok(NULL, " ");
				offset = strtok(NULL, " ");
				tamanio = strtok(NULL, " ");
				if (pid != 0 || tamanio != 0)
				{
					char* buffer = malloc(atoi(tamanio));
					cambioProcesoActivo(atoi(pid));
					buffer = solicitarBytes(atoi(base), atoi(offset), atoi(tamanio));
					if(buffer == NULL)
					{
						printf("SEGMENTATION FAULT\n");
					}
					else
					{
						printf("%s",buffer);
					}
					free(buffer);
				}
				else
				{
					printf("Argumentos incorrectos\n");
				}
				continue;
			}
			if(string_starts_with(resto,"escribir "))
			{
				char* resto2 = string_substring_from(resto,9);
				char* pid;
				char* tamanio;
				char* base;
				char* offset;
				pid = strtok(resto2, " ");
				base = strtok(NULL, " ");
				offset = strtok(NULL, " ");
				tamanio = strtok(NULL, " ");

				if (pid != 0 || tamanio != 0)
				{
					//TODO: tratar de cambiar el 128
					void* buffer = malloc (128);
					printf("Ingrese bytes a escribir: ");
					gets(buffer);
					printf("\n");
					cambioProcesoActivo(atoi(pid));
					confirmacion = enviarBytes(atoi(base), atoi(offset), atoi(tamanio), buffer);
					if(confirmacion != -1)
					{
						printf("Los bytes se enviaron correctamente\n");
					}
					else
					{
						printf("SEGMENTATION FAULT\n");
					}
					free(buffer);
				}
				else
				{
					printf("Argumentos incorrectos\n");
				}
				continue;
			}
			if(string_starts_with(resto,"crear-segmento "))
			{
				char* resto2 = string_substring_from(resto,15);
				char* pid;
				char* tamanio;
				pid = strtok(resto2, " ");
				tamanio = strtok(NULL, " ");
				if (tamanio != 0 || pid != 0)
				{
					confirmacion = crearSegmento(atoi(pid), atoi(tamanio));
					if(confirmacion != -1)
					{
						printf("Segmento creado\n");
					}
					else
					{
						printf("MEMORY OVERLOAD\n");
					}
				}
				else
				{
					printf("Argumentos incorrectos\n");

				}
				continue;

			}
			if(string_starts_with(resto,"destruir-segmentos "))
			{
				char* resto2 = string_substring_from(resto,19);
				if(atoi(resto2) != 0)
				{
					destruirSegmentos(atoi(resto2));
					printf("Segmentos destruidos\n");
				}
				else
				{
					printf("Argumentos incorrectos.\n");
				}
				continue;
			}
			printf("Argumento incorrecto\n");
			continue;
		}
		if(string_starts_with(parametros,"retardo "))
		{
			char* resto = string_substring_from(parametros,8);
			retardo = atoi(resto);
			printf("Retardo Cambiado\n");
			continue;
		}
		if(string_starts_with(parametros,"algoritmo "))
		{
			char* resto = string_substring_from(parametros,10);
			if(string_equals_ignore_case(resto,"worst-fit"))
			{
				algoritmo = 1;
				printf("Algoritmo cambiado a Worst-Fit\n");
				continue;
			}
			if(string_equals_ignore_case(resto,"first-fit"))
			{
				algoritmo = 0;
				printf("Algoritmo cambiado a First-Fit\n");
				continue;
			}
			printf("Argumento incorrecto\n");
			continue;
		}
		if(string_equals_ignore_case(parametros,"compactacion"))
		{
			sem_wait(&s_TablaSegmentos);
			compactar();
			sem_post(&s_TablaSegmentos);
			printf("Compactado.\n");
			continue;
		}
		if(string_equals_ignore_case(parametros,"dump"))
		{
			printf("Desea mostrarlo en pantalla? (S/N): ");
			scanf("%c", &mostrarPantalla);
			printf("\n");
			if(mostrarPantalla == 's' || mostrarPantalla == 'S') dump(1);
			if(mostrarPantalla == 'n' || mostrarPantalla == 'N') dump(0);
			printf("Fin de dump\n");
			continue;
		}
		if(string_starts_with(parametros,"dump "))
		{
			char* resto = string_substring_from(parametros,5);
			printf("Desea mostrarlo en pantalla? (S/N): ");
			scanf("%c", &mostrarPantalla);
			printf("\n");
			if(string_equals_ignore_case(resto,"tabla-segmentos"))
			{
				if(mostrarPantalla == 's' || mostrarPantalla == 'S') mostrarEstructuras(1);
				if(mostrarPantalla == 'n' || mostrarPantalla == 'N') mostrarEstructuras(0);
				printf("Fin de dump\n");
				continue;
			}
			if(string_equals_ignore_case(resto,"mostrar-memoria"))
			{
				if(mostrarPantalla == 's' || mostrarPantalla == 'S') mostrarMemoria(1);
				if(mostrarPantalla == 'n' || mostrarPantalla == 'N') mostrarMemoria(0);
				printf("Fin de dump\n");
				continue;
			}
			if(string_equals_ignore_case(resto,"contenido-memoria"))
			{
				printf("Ingrese offset: ");
				scanf("%d", &offset);
				printf("Ingrese tamanio (negativo para mostrar hasta el fin): ");
				scanf("%d", &tamanio);
				if(tamanio <= 0) tamanio = finMemPpal - memPpal;
				if(mostrarPantalla == 's' || mostrarPantalla == 'S') mostrarContenidoDeMemoria(1, offset, tamanio);
				if(mostrarPantalla == 'n' || mostrarPantalla == 'N') mostrarContenidoDeMemoria(0, offset, tamanio);
				printf("Fin de dump\n");
				continue;
			}
		}
		else
		{
			printf("Argumentos incorrectos.\n");
			continue;
		}
	}
	return NULL;

}

/* Hilo para escuchar conexiones */
void* mainEsperarConexiones()
{
	pthread_t hiloKernel, hiloCpu;
	struct addrinfo hints;
	struct addrinfo *serverInfo;
	int escucharConexiones;
	char id = -1;
	char confirmacion;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// No importa si uso IPv4 o IPv6
	hints.ai_flags = AI_PASSIVE;		// Asigna el address del localhost: 127.0.0.1
	hints.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP
	getaddrinfo(NULL, PUERTO, &hints, &serverInfo); // Notar que le pasamos NULL como IP, ya que le indicamos que use localhost en AI_PASSIVE
	escucharConexiones = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
	bind(escucharConexiones,serverInfo->ai_addr, serverInfo->ai_addrlen);
	while(1)
	{
		id = -1;
		confirmacion = -1;
		listen(escucharConexiones, BACKLOG);		// IMPORTANTE: listen() es una syscall BLOQUEANTE.
		struct sockaddr_in programa;			// Esta estructura contendra los datos de la conexion del cliente. IP, puerto, etc.
		socklen_t addrlen = sizeof(programa);
		int socketCliente = accept(escucharConexiones, (struct sockaddr *) &programa, &addrlen);
		recv(socketCliente, &id, sizeof(char), 0);
		confirmacion = handshake(id);
		send(socketCliente, &confirmacion, sizeof(char), 0);
		if (confirmacion == 1)
		{
			if (id == kernel)
			{
				log_info(logi, "Se conecto el kernel con socket %d", socketCliente);
				printf("Se conecto el kernel por el socket %d\n", socketCliente);
				pthread_create(&hiloKernel, NULL, f_hiloKernel, (void*)socketCliente);
				continue;
			}
			if (id == cpu)
			{
				log_info(logi, "Se conecto un cpu con socket %d", socketCliente);
				printf("Se conecto un cpu por el socket %d\n", socketCliente);
				pthread_create(&hiloCpu, NULL, f_hiloCpu, (void*)socketCliente);
				continue;
			}
		}
		else
		{
			log_error(logi, "Se trato de conectar un dispositivo desconocido");
			printf("Se trato de conectar un dispositivo desconocido\n");
			continue;
		}
	}
	return NULL;
}

void* f_hiloKernel(void* socketCliente)
{
	int socketKernel = (int)socketCliente;
	int status = 1;
	int mensaje[5];
	int respuesta;
	char operacion;
	char confirmacion;
	int pid, base, offset, tamanio;
	char* buffer;
	while(status != 0)
	{
		recv(socketKernel, &operacion, sizeof(char), 0);
		usleep(retardo*1000);
		//sem_wait(&s_cpu);
		log_info(logi, "Llego una operacion del kernel");
		if (operacion == operCrearSegmento)
		{
			confirmacion = 1;
			send(socketKernel, &confirmacion, sizeof(char), 0);
			status = recv(socketKernel, mensaje, 2*sizeof(int), 0);
			if(status != 0)
			{
				respuesta = crearSegmento(mensaje[0], mensaje[1]);
				send(socketKernel, &respuesta, sizeof(int), 0);
//				if(respuesta != -1) log_trace(logi, "Se envio la base del nuevo segmento al kernel");
//				if(respuesta == -1) log_trace(logi, "Se le informo al kernel del memory overload");
			}
		}
		else if(operacion == operEnviarBytes)
		{
			confirmacion = 1;
			send(socketKernel, &confirmacion, sizeof(char), 0);
			status = recv(socketKernel, &pid, sizeof(int), 0);
			status = recv(socketKernel, &base, sizeof(int), 0);
			status = recv(socketKernel, &offset, sizeof(int), 0);
			status = recv(socketKernel, &tamanio, sizeof(int), 0);
			buffer = malloc(tamanio);
			status = recv(socketKernel, buffer, tamanio, 0);
			cambioProcesoActivo(pid);
			enviarBytes(base, offset, tamanio, buffer);
			free(buffer);
			send(socketKernel,&confirmacion,sizeof(char),0);
//			log_trace(logi, "Se envio confirmacion al kernel");
		}
		else if(operacion == operDestruirSegmentos)
		{
			confirmacion = 1;
			send(socketKernel, &confirmacion, sizeof(char), 0);
			status = recv(socketKernel, &pid, sizeof(int), 0);
			if(status != 0)
			{
				destruirSegmentos(pid);
			}
		}
		//sem_post(&s_cpu);
	}
	//exit(0);
	return 0;
}

void* f_hiloCpu(void* socketCliente)
{
	int socketCPU = (int)socketCliente;
	int status = 1;
	int mensaje[4];
	char operacion;
	char confirmacion;
	char enviarError;
	char haySegFault;
	int hiceWait = 1;
	int pid, base, offset, tamanio;
	void* buffer;
	while(status != 0)
	{
		if(recv(socketCPU, &operacion, sizeof(char), 0) == 0)
		{
			log_info(logi, "Se desconecto el cpu %d", socketCPU);
			printf("Se desconecto el cpu %d\n", socketCPU);
			hiceWait = 0;
			break;
		}
		//sem_wait(&s_cpu);
		//mostrarContenidoDeMemoria(0,finMemPpal-memPpal);
		log_info(logi, "Llego una operacion del cpu %d", socketCPU);
		if (operacion == operSolicitarBytes)
		{
			confirmacion = 1;
			send(socketCPU, &confirmacion, sizeof(char), 0);
			usleep(retardo*1000);
			if(recv(socketCPU, mensaje, 4*sizeof(int), 0) == 0)
			{
				log_info(logi, "Se desconecto el cpu %d", socketCPU);
				printf("Se desconecto el cpu %d\n", socketCPU);
				break;
			}
			buffer = malloc(mensaje[3]);
			cambioProcesoActivo(mensaje[0]);
			buffer = solicitarBytes(mensaje[1], mensaje[2], mensaje[3]);
			if (buffer == NULL)
			{
				haySegFault = 1;
				send(socketCPU, &haySegFault, sizeof(char), 0);
//				log_trace(logi, "Se le informo al cpu %d del segmentation fault", socketCPU);
			}
			else
			{
				haySegFault = 0;
				send(socketCPU, &haySegFault, sizeof(char), 0);
				send(socketCPU, buffer, mensaje[3], 0);
//				log_trace(logi, "Se le enviaron al cpu %d los bytes solicitados", socketCPU);
			}
			free(buffer);

		}
		else if(operacion == operEnviarBytes)
		{
			confirmacion = 1;
			send(socketCPU, &confirmacion, sizeof(char), 0);
			usleep(retardo*1000);
			if(recv(socketCPU, &pid, sizeof(int), 0) == 0)
			{
				log_info(logi, "Se desconecto el cpu %d", socketCPU);
				printf("Se desconecto el cpu %d\n", socketCPU);
				break;
			}
			status = recv(socketCPU, &base, sizeof(int), 0);
			status = recv(socketCPU, &offset, sizeof(int), 0);
			status = recv(socketCPU, &tamanio, sizeof(int), 0);
			buffer = malloc(tamanio);
			status = recv(socketCPU, buffer, tamanio, 0);
			cambioProcesoActivo(pid);
			enviarError = enviarBytes(base, offset, tamanio, buffer);
			if(send(socketCPU, &enviarError, sizeof(char), 0) == 0)
			{
				log_info(logi, "Se desconecto el cpu %d", socketCPU);
				printf("Se desconecto el cpu %d\n", socketCPU);
				break;
			}
//			log_trace(logi, "Se le envio confirmacion al cpu %d", socketCPU);
			free(buffer);
		}
		else if(operacion == operDestruirSegmentos)
		{
			confirmacion = 1;
			send(socketCPU, &confirmacion, sizeof(char), 0);
			usleep(retardo*1000);
			status = recv(socketCPU, &pid, sizeof(int), 0);
			if(status != 0)
			{
				destruirSegmentos(pid);
			}
			else
			{
				log_info(logi, "Se desconecto el cpu %d", socketCPU);
				printf("Se desconecto el cpu %d\n", socketCPU);
				break;
			}
		}
		else
		{
			log_error(logi, "OPERACION INCORRECTA");
		}
	}
	if(hiceWait)
	{
		//sem_post(&s_cpu);
	}
	//exit(0);
	close(socketCPU);
	log_trace(logi, "Termine conexion con el cpu %d", socketCPU);
	return 0;
}

int handshake(int id)
{
	if (id == kernel || id == cpu)
	{
		return 1;
	}
	return 0;
}

void destruirSegmentos( int id){
	//mostrarEstructuras();
	//usleep(retardo*1000);
	log_trace(logi, "Se solicita destruir los segmentos del programa %d", id);
	sem_wait(&s_TablaSegmentos);
	t_segmento* aux = NULL;
	t_segmento* auxSiguiente = tablaSegmentos;
	while (auxSiguiente != NULL)
	{
		if(auxSiguiente->idProceso == id)
		{
			if (auxSiguiente == tablaSegmentos)
			{
				tablaSegmentos = auxSiguiente->siguiente;
				free(auxSiguiente);
				auxSiguiente = tablaSegmentos;
			}
			else
			{
				if (auxSiguiente->siguiente == NULL)
				{
					aux->siguiente = NULL;
					free(auxSiguiente);
				}
				else
				{
					aux->siguiente = auxSiguiente->siguiente;
					free(auxSiguiente);
				}
				auxSiguiente = aux->siguiente;
			}
		}
		else
		{
			aux = auxSiguiente;
			auxSiguiente = auxSiguiente->siguiente;
		}
	}
//	log_trace(logi, "Se destruyeron los segmentos del programa %d", id);
	sem_post(&s_TablaSegmentos);
	//mostrarEstructuras();
	return;
}

/* Funcion solicitar bytes */
void* solicitarBytes(int base, int offset, int tamanio)
{
	//usleep(retardo*1000);
	log_trace(logi, "Se solicitan los bytes del programa %d, base %d, desde %d, tamanio %d", procesoActivo, base, offset, tamanio);
	sem_wait(&s_TablaSegmentos);
	void* pComienzo;
	t_segmento* segmentoBuscado = buscarSegmento(base);
	if ((segmentoBuscado == NULL) || (offset + tamanio > segmentoBuscado->tamanio))
	{
		log_error(logi, "SEGMENTATION FAULT");
		printf("SEGMENTATION FAULT\n");
		sem_post(&s_cambioProcesoActivo);
		sem_post(&s_TablaSegmentos);
		return NULL;
	}
	pComienzo = segmentoBuscado->dirInicio + offset;
	void* buffer= malloc(tamanio);
	memcpy(buffer,pComienzo,tamanio);
	sem_post(&s_cambioProcesoActivo);
	sem_post(&s_TablaSegmentos);
	return buffer;
}

int enviarBytes(int base, int offset, int tamanio, void* buffer)
{
	//usleep(retardo*1000);
	log_trace(logi, "Se solicita enviar bytes al programa %d, base %d, desde %d, tamanio %d", procesoActivo, base, offset, tamanio);
	sem_wait(&s_TablaSegmentos);
	void* pComienzo;
	t_segmento* segmentoBuscado = buscarSegmento(base);
	if ((segmentoBuscado == NULL) || (offset + tamanio > segmentoBuscado->tamanio))
	{
		log_error(logi, "SEGMENTATION FAULT");
		printf("SEGMENTATION FAULT\n");
		sem_post(&s_cambioProcesoActivo);
		sem_post(&s_TablaSegmentos);
		return -1;
	}
	pComienzo = segmentoBuscado->dirInicio + offset;
	memcpy(pComienzo,buffer,tamanio);
	sem_post(&s_cambioProcesoActivo);
	sem_post(&s_TablaSegmentos);
	return 0;
}

int crearSegmento(int idProceso, int tamanio)
{
	//usleep(retardo*1000);
	log_trace(logi, "Se solicita crear un segmento del programa %d con tamanio %d", idProceso, tamanio);
	sem_wait(&s_TablaSegmentos);
	void* inicioNuevo = posicionarSegmento(algoritmo,tamanio);
	t_segmento* segmentoNuevo;
	if(inicioNuevo == NULL)
	{
		log_error(logi, "MEMORY OVERLOAD");
		printf("MEMORY OVERLOAD\n");
		sem_post(&s_TablaSegmentos);
		return -1;
	}
	segmentoNuevo = malloc(sizeof(t_segmento));
	segmentoNuevo->idProceso = idProceso;
	segmentoNuevo->idSegmento = nuevoIDSegmento(idProceso);
	segmentoNuevo->base = rand()%10001;
	segmentoNuevo->tamanio = tamanio;
	segmentoNuevo->dirInicio = inicioNuevo;
	insertarSegmento(segmentoNuevo);
	sem_post(&s_TablaSegmentos);
//	log_info(logi, "El segmento se creo correctamente");
	return segmentoNuevo->base;
}

t_segmento* buscarSegmento(int base)
{
//	log_trace(logi, "Se busca el segmento del programa %d, base %d", procesoActivo, base);
	t_segmento* aux = tablaSegmentos;
	while(aux != NULL)
	{
		if(aux->base == base && aux->idProceso == procesoActivo)
		{
//			log_info(logi, "Se encontro el segmento");
			return aux;
		}
		aux = aux->siguiente;
	}
//	log_error(logi, "No se encontro el segmento");
	return NULL;
}

void* posicionarSegmento(int algoritmo, int tamanio)
{
	void* inicioNuevo;
	if(algoritmo == 0)
	{
		inicioNuevo = first_fit(tamanio);
		return inicioNuevo;
	}
	else
	{
		inicioNuevo = worst_fit(tamanio);
		return inicioNuevo;
	}
}

/* Algoritmos First fit y Worst fit */
void* first_fit(int tamanio)
{
//	log_trace(logi, "Posicionando segmento de tamanio %d en memoria segun First Fit", tamanio);
	int compactado = 0;
	t_segmento* aux;
	aux = tablaSegmentos;
	void* auxinicio = memPpal;
	while(aux != NULL)
	{
		if(tamanio <= (int)(aux->dirInicio - auxinicio))
		{
//			log_info(logi, "Se posiciono el segmento correctamente");
			return auxinicio;
		}
		else
		{
			auxinicio = aux->dirInicio + aux->tamanio;
			aux = aux->siguiente;
		}
	}
	while(compactado <= 1)
	{
		if(tamanio <= (int)(finMemPpal - auxinicio))
		{
//			log_info(logi, "Se posiciono el segmento correctamente");
			return auxinicio;
		}
		else
		{
			if(compactado == 0)
			{
				auxinicio = compactar();
				compactado = 1;
			}
			else
			{
				return 0;
			}
		}
	}
	return 0;
}

void* worst_fit(int tamanio)
{
//	log_trace(logi, "Posicionando segmento de tamanio %d en memoria segun Worst Fit", tamanio);
	int compactado = 0;
	t_segmento* aux = tablaSegmentos;
	void* auxinicio = memPpal;
	int tamMax = 0;
	void* dirTamMax = NULL;
	if(tablaSegmentos == NULL)
	{
		if(tamanio < (int)(finMemPpal - memPpal))
		{
//			log_info(logi, "Se posiciono el segmento correctamente");
			return memPpal;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		tamMax = (int) (aux->dirInicio - auxinicio);
		dirTamMax = auxinicio;
		while(aux != NULL)
		{
			if(tamMax < (int)(aux->dirInicio - auxinicio))
			{
				tamMax = (int)(aux->dirInicio - auxinicio);
				dirTamMax = auxinicio;
			}
			else
			{
				auxinicio = aux->dirInicio + aux->tamanio;
				aux = aux->siguiente;
			}
		}
		if(tamMax < (int)(finMemPpal - auxinicio))
		{
			tamMax = (int)(finMemPpal - auxinicio);
			dirTamMax = auxinicio;
		}
		while(compactado <= 1)
		{
			if(tamMax >= tamanio)
			{
//				log_info(logi, "Se posiciono el segmento correctamente");
				return dirTamMax;
			}
			else
			{
				if(compactado == 0)
				{
					dirTamMax = compactar();
					compactado = 1;
					tamMax = finMemPpal - dirTamMax;
				}
				else
				{
					return 0;
				}
			}
		}
	}
	return 0;
}

int nuevoIDSegmento(int idProceso)
{
	t_segmento* aux = tablaSegmentos;
	int idSegmento = 0;
	while(aux != NULL)
	{
		if((aux->idProceso == idProceso) && (aux->idSegmento >= idSegmento))
		{
			idSegmento = aux->idSegmento + 1;
		}
		aux = aux->siguiente;
	}
	return idSegmento;
}

void insertarSegmento(t_segmento* segmento)
{
//	log_trace(logi, "Insertando segmento en tabla segun su posicion en memoria");
	t_segmento* auxAnterior = tablaSegmentos;
	t_segmento* aux;
	if(tablaSegmentos == NULL)
	{
		tablaSegmentos = segmento;
		tablaSegmentos->siguiente = NULL;
		return;
	}
	else
	{
		if (tablaSegmentos->dirInicio > segmento->dirInicio)
		{
			segmento->siguiente = tablaSegmentos;
			tablaSegmentos = segmento;
			return;
		}
		else
		{
			auxAnterior = tablaSegmentos;
			aux = auxAnterior->siguiente;
			while (aux != NULL)
			{
				if (aux->dirInicio > segmento->dirInicio)
				{
					segmento->siguiente = aux;
					auxAnterior->siguiente = segmento;
					return;
				}
				else
				{
					auxAnterior = aux;
					aux = aux->siguiente;
				}
			}
			auxAnterior->siguiente = segmento;
			segmento->siguiente = NULL;
			return;
		}
	}
}

void* compactar(void)
{
	log_trace(logi, "Se solicita compactar");
	t_segmento* auxSegmento = tablaSegmentos;
	void* aux = memPpal;

	while(auxSegmento != NULL)
	{
		if (aux != auxSegmento->dirInicio)
		{
			memcpy(aux,auxSegmento->dirInicio,auxSegmento->tamanio);
			auxSegmento->dirInicio = aux;
		}
		aux = auxSegmento->dirInicio+auxSegmento->tamanio;
		auxSegmento = auxSegmento->siguiente;
	}
//	log_info(logi, "Compactacion completada");
	return aux;
}

void dump(bool mostrarPantalla)
{
	log_trace(logi, "Se solicito dump de memoria completo");
	sem_wait(&s_TablaSegmentos);
	mostrarEstructuras(mostrarPantalla);
	mostrarMemoria(mostrarPantalla);
	mostrarContenidoDeMemoria(mostrarPantalla,0,finMemPpal-memPpal);
	sem_post(&s_TablaSegmentos);
}

void mostrarEstructuras(bool mostrarPantalla)
{
	t_segmento* auxSegmento = tablaSegmentos;
	log_info(logi, "Tabla de Segmentos:");
	if(mostrarPantalla) printf("Tabla de Segmentos:\n");
	//printf("\nTabla de Segmentos:\n\n");
		while (auxSegmento != NULL)
		{
			log_info(logi, "Proceso: %d \t Segmento: %d \t Base: %d" ,auxSegmento->idProceso, auxSegmento->idSegmento, auxSegmento->base);
			if(mostrarPantalla) printf("Proceso: %d \t Segmento: %d \t Base: %d\n" ,auxSegmento->idProceso, auxSegmento->idSegmento, auxSegmento->base);
			auxSegmento = auxSegmento->siguiente;
		}
	log_info(logi, "----------------------------------------------");
	if(mostrarPantalla) printf("----------------------------------------------\n");
	//printf("--------------------\n");
}

void mostrarMemoria(bool mostrarPantalla)
{
	t_segmento* auxSegmento = tablaSegmentos;
	int tamanioSegmentos = 0;
	void* contador = memPpal;
	log_info(logi, "Memoria principal:\n\n");
	if(mostrarPantalla) printf("Memoria principal:\n\n");
	while (contador < finMemPpal)
	{
		if(auxSegmento != NULL && contador == auxSegmento->dirInicio)
		{
			log_info(logi, "Desde la posicion %p hasta la %p pertenecen al programa %d, segmento %d",auxSegmento->dirInicio
					,auxSegmento->dirInicio+auxSegmento->tamanio
					,auxSegmento->idProceso
					,auxSegmento->idSegmento);
			if(mostrarPantalla) printf("Desde la posicion %p hasta la %p pertenecen al programa %d, segmento %d\n",auxSegmento->dirInicio
																						,auxSegmento->dirInicio+auxSegmento->tamanio
																						,auxSegmento->idProceso
																						,auxSegmento->idSegmento);
			contador = contador + auxSegmento->tamanio;
			auxSegmento = auxSegmento->siguiente;
		}
		else
		{
			if(auxSegmento != NULL)
			{
				log_info(logi, "Desde la posicion %p hasta la %p esta libre",contador, auxSegmento->dirInicio);
				if(mostrarPantalla) printf("Desde la posicion %p hasta la %p esta libre\n",contador, auxSegmento->dirInicio);
				contador = auxSegmento->dirInicio;
			}
			else
			{
				log_info(logi, "Desde la posicion %p hasta la %p esta libre",contador, finMemPpal);
				if(mostrarPantalla) printf("Desde la posicion %p hasta la %p esta libre\n",contador, finMemPpal);
				contador = finMemPpal;
			}
//			log_info(logi, "%p -- Libre",contador);
//			if(mostrarPantalla) printf("%p -- Libre\n",contador);
//			contador++;
		}
	}
	auxSegmento = tablaSegmentos;
	while(auxSegmento != NULL)
	{
		tamanioSegmentos += auxSegmento->tamanio;
		auxSegmento = auxSegmento->siguiente;
	}
	log_info(logi, "\nMemoria libre: %d bytes de %d bytes",(finMemPpal - memPpal) - tamanioSegmentos, finMemPpal - memPpal);
	log_info(logi, "-------------------------------------");
	if(mostrarPantalla) printf("\nMemoria libre: %d bytes de %d bytes\n\n",(finMemPpal - memPpal) - tamanioSegmentos, finMemPpal - memPpal);
	if(mostrarPantalla) printf("-------------------------------------\n");
}

void mostrarContenidoDeMemoria(bool mostrarPantalla, int offset, int tamanio)
{
	char* contador = memPpal + offset;
	int i;
	log_info(logi, "Contenido de la memoria desde %p hasta %p:", contador,contador+tamanio-1);
	if(mostrarPantalla) printf("Contenido de la memoria desde %p hasta %p:\n", contador,contador+tamanio-1);
	//printf("Contenido de la memoria desde %p hasta %p:\n\n", contador,contador+tamanio-1);
	for (i=0; i<tamanio; i++)
	{
		if(*(contador+i) != 0)
		{
			log_info(logi, "%p --- %d \t %c",contador+i,*(contador+i), *(contador+i));
			if(mostrarPantalla) printf("%p --- %d \t %c \n",contador+i,*(contador+i), *(contador+i));
		}
		//log_info(logi, "%c",*(contador+i));
//		printf("%p --- %d\t",contador+i,*(contador+i));
//		printf("%c\n",*(contador+i));
	}
}

void imprimirSegmento(t_segmento* segmento)
{
	log_info(logi, "Proceso: %d \t Segmento: %d \n Base: %d" ,segmento->idProceso, segmento->idSegmento, segmento->base);
	//log_info(logi, "Segmento: %d" ,segmento->idSegmento);
	//log_info(logi, "Base: %d" ,segmento->base);
	//log_info(logi, "Tamaño: %d" ,segmento->tamanio);
	//log_info(logi, "Direccion fisica: %p" ,segmento->dirInicio);
//	printf("\nProceso: %d \n",segmento->idProceso);
//	printf("Segmento: %d \n",segmento->idSegmento);
//	printf("Base: %d \n",segmento->base);
//	printf("Tamaño: %d \n",segmento->tamanio);
//	printf("Direccion fisica: %p \n",segmento->dirInicio);
//	printf("-----------------------------------\n");
}

int cambioProcesoActivo(int idProceso)
{
	sem_wait(&s_cambioProcesoActivo);
	int aux = procesoActivo;
	procesoActivo = idProceso;
	return aux;
}
