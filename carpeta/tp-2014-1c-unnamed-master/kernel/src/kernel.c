/*
 ============================================================================
 Name        : kernel.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <parser/metadata_program.h>
#include <commons/string.h>
#include <commons/config.h>
#include <commons/log.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>

/* Estructuras de datos */
typedef struct pcb
{
	int pid;						//Lo creamos nosotros
	int segmentoCodigo;				//Direccion al inicio de segmento de codigo (base)
	int segmentoStack;				//Direccion al inicio de segmento de stack
	int cursorStack;				//Cursor actual del stack (contexto actual) Es el offset con respecto al stack.
	int indiceCodigo;				//Direccion al inicio de indice de codigo
	int indiceEtiquetas;			//
	int programCounter;
	int tamanioContextoActual;
	int tamanioIndiceEtiquetas;
	int tamanioIndiceCodigo;
	int peso;
	struct pcb *siguiente;
}t_pcb;

typedef struct new
{
	int pid;						//Lo creamos nosotros
	char* codigo;
	struct new *siguiente;
}t_new;

/*Estructura de variables compartidas. Array con Nombre y valor*/
typedef struct  variableCompartida
{
	char* nombreVariable;
	int valorVariable;
}t_variableCompartida;

/*Estructuras IO. Un listado con PCB/Tiempo y un array con cada dispositivo y su lista*/
typedef struct listaIO
{
	t_pcb* pcb;
	int tiempo;
	struct listaIO *siguiente;
}t_listaIO;

typedef struct  IO
{
	char* nombreIO;
	int retardo;
	t_listaIO* pcbEnLista;
}t_IO;

/*Estructura Semáforos. Un array con cada Semáforo, su cantidad de recursos disponibles (valor) y listado de PCB*/
typedef struct semaforo
{
	char* nombreSemaforo;
	int valor;
	t_pcb* pcb;
}t_semaforo;

typedef struct imprimir
{
	int pid;
	char mensaje;
	int valor;
	char* texto;
	struct imprimir *siguiente;
}t_imprimir;

typedef struct cpu
{
	int socketID;
	struct cpu *siguiente;
	t_pcb *pcb;
}t_cpu;

typedef struct semArray
{
	sem_t semaforo;
}t_semArray;

enum
{
	new,
	ready,
	exec,
	finish
};


/* Funciones */
void* f_hiloPCP();
void* f_hiloHabilitarCpu(void);
void* f_hiloPLP();
int crearPcb(t_new programa, t_pcb* pcbAux);
void UMV_enviarBytes(int pid, int base, int offset, int tamanio, void* buffer);
char* serializarEnvioBytes(int pid, int base, int offset, int tamanio, void* buffer);
void Programa_imprimirTexto(int pid, char* texto);
void encolarEnNew(t_new* programa);
void conexionUMV(void);
int UMV_crearSegmentos(int mensaje[2]);
void UMV_destruirSegmentos(int pid);
void* f_hiloMostrarNew();
t_pcb* recibirSuperMensaje ( int superMensaje[11] );
void cargarConfig(void);
void cargarVariablesCompartidas(void);
void cargarDispositivosIO(void);
void cargarSemaforos(void);
void destruirPCB(int pid);
void* f_hiloColaReady();
void* f_hiloIO(void* pos);
t_new desencolarNew(void);
void encolarEnReady(t_pcb* pcb);
t_pcb* desencolarReady(void);
int encolarExec(t_pcb* pcb);
void desencolarExec(t_pcb* pcb);
void encolarExit(t_pcb* pcb);
void agregarCpu(int socketID);
t_pcb* sacarCpuDeEjecucion(int socketID);
void* f_hiloColaExit(void);


/* Variables Globales */
t_new* l_new = NULL;
t_pcb* l_ready = NULL;
t_pcb* l_exec = NULL;
t_pcb* l_exit = NULL;
t_medatada_program* metadata;
int tamanioStack;
int socketUMV;
//int socketCPU; //VER
fd_set fd_PCP;
fd_set fdWPCP;
fd_set fdRPCP;
fd_set readPCP;
fd_set writePCP;
fd_set fdRPLP;
fd_set fdWPLP;
fd_set readPLP;
fd_set writePLP;
int maximoCpu = 0;
int maximoPrograma = 0;
char* PUERTOPROGRAMA;
int BACKLOG;			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
int	PACKAGESIZE;	// Define cual va a ser el size maximo del paquete a enviar
char* PUERTOUMV;
char* IPUMV;
char* PUERTOCPU;
char* IPCPU;
t_variableCompartida* arrayVariablesCompartidas;
int cantidadVariablesCompartidas = 0;
t_IO* arrayDispositivosIO;
int cantidadDispositivosIO = 0;
t_semaforo* arraySemaforos;
int cantidadSemaforos = 0;
t_pcb* listadoSemaforos;
int quantum = 1;
int retardo = 0;
t_imprimir* l_imprimir;
t_cpu* l_cpu;
int gradoMultiprogramacion;
t_log* logger;


/* Semáforos */
sem_t s_Multiprogramacion; //Semáforo del grado de Multiprogramación. Deja pasar a Ready los PCB Disponibles.
sem_t s_ColaReady;
sem_t s_ColaExit;
sem_t s_ColaNew;
sem_t s_ComUmv;
sem_t* s_IO; //Semáforo para habilitar revisar la lista de IO y atender pedidos. Inicializada en 0.
sem_t s_Semaforos; //Semáforo para habilitar revisar la lista de IO y atender pedidos. Inicializada en 0.
sem_t s_ProgramaImprimir;
sem_t s_ProgramasEnReady;
sem_t s_ProgramasEnNew;
sem_t s_ProgramasEnExit;
sem_t s_CpuDisponible;
sem_t s_ColaCpu;
sem_t s_ColaExec;
sem_t s_ColaIO;
sem_t s_ConexionCpu;

/*Archivo de Configuración*/
t_config* configuracion;

int main(int cantArgs, char** args) {
	pthread_t hiloPCP, hiloPLP, hiloMostrarNew, hiloColaReady;
	//int rhPCP, rhPLP, rhMostrarNew, rhColaReady, rhColaIO;
	logger = log_create(args[2], "Kernel", 0, LOG_LEVEL_TRACE);
	configuracion = config_create(args[1]);
	cargarConfig();
	sem_init(&s_Multiprogramacion,0,gradoMultiprogramacion);
	sem_init(&s_ColaReady,0,1);
	sem_init(&s_ColaExit,0,1);
	sem_init(&s_ColaNew,0,1);
	sem_init(&s_ColaExec,0,1);
	sem_init(&s_ComUmv,0,1);
	sem_init(&s_ColaIO,0,1);
	sem_init(&s_Semaforos,0,1);
	sem_init(&s_ProgramaImprimir,0,1);
	sem_init(&s_ProgramasEnReady,0,0);
	sem_init(&s_CpuDisponible,0,0);
	sem_init(&s_ProgramasEnNew,0,0);
	sem_init(&s_ColaCpu,0,1);
	sem_init(&s_ProgramasEnExit,0,0);
	sem_init(&s_ConexionCpu,0,1);
	cargarVariablesCompartidas();
	cargarDispositivosIO();
	cargarSemaforos();

	int i;
	printf("==========\t Kernel \t==========\n");
	printf("Variables Compartidas:\t");
	for(i=0;i<cantidadVariablesCompartidas;i++) printf("%s  ",arrayVariablesCompartidas[i].nombreVariable);
	printf("\nSemáforos\t");
	for(i=0;i<cantidadSemaforos;i++) printf("%s (%d) ", arraySemaforos[i].nombreSemaforo, arraySemaforos[i].valor);
	printf("\nDispositivos Entrada/Salida (HIO): \t");
	for(i=0;i<cantidadDispositivosIO;i++) printf("%s  ",arrayDispositivosIO[i].nombreIO);
	printf("\n------------------------------------------------------------\n");









	conexionUMV();
	pthread_create(&hiloPCP, NULL, f_hiloPCP, NULL);
	pthread_create(&hiloPLP, NULL, f_hiloPLP, NULL);
	pthread_create(&hiloMostrarNew, NULL, f_hiloMostrarNew, NULL);
	pthread_create(&hiloColaReady, NULL, f_hiloColaReady, NULL);
	//rhColaIO = pthread_create(&hiloIO, NULL, f_hiloIO, NULL);
	pthread_join(hiloPCP, NULL);
	pthread_join(hiloPLP, NULL);
	pthread_join(hiloMostrarNew, NULL);
	pthread_join(hiloColaReady, NULL);
	//pthread_join(hiloIO, NULL);
	//printf("%d",rhPCP);
	//printf("%d",rhPLP);

	return 0;
}


void* f_hiloColaReady()
{
	t_new programa;
	int conf;
	char terminarPrograma = 2;
	while(1)
	{
		sem_wait(&s_ProgramasEnNew);
		sem_wait(&s_Multiprogramacion);
		log_info(logger, "Se puede pasar un programa de NEW a READY");
		programa = desencolarNew();
		t_pcb* nuevoPCB = malloc(sizeof(t_pcb));
		conf = crearPcb(programa, nuevoPCB);
		if(conf == 1)
		{
			encolarEnReady(nuevoPCB);
		}
		else
		{
			log_info(logger, "No se creo el pcb del programa %d", programa.pid);
			sem_post(&s_Multiprogramacion);
			send(programa.pid, &terminarPrograma, sizeof(char), 0);
			log_trace(logger, "Se le informo al programa %d que debe finalizar", programa.pid);
		}
	}
	return NULL;
}


void* f_hiloPCP()
{
	log_trace(logger, "Levanto hilo PCP");
	pthread_t hiloHabilitarCpu;
	struct addrinfo hints;
	struct addrinfo *serverInfo;
	int socketPCP, socketAux;
	int i, j, maximoAnterior;
	int superMensaje[11];
	int status = 1;
	int statusSelect;
	char mensaje;
	int pid, valorAImprimir, tamanioTexto;
	char* texto;
	char operacion;
	//char ping = 1;
	t_pcb* pcb;
	//t_pcb* puntero;
	//signal(SIGPIPE, SIG_IGN);
	//void* package = malloc(sizeof(t_pcb));
	//t_pcb* pcb;
	//t_pcb* puntero;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// No importa si uso IPv4 o IPv6
	hints.ai_flags = AI_PASSIVE;		// Asigna el address del localhost: 127.0.0.1
	hints.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP
	getaddrinfo(NULL, PUERTOCPU, &hints, &serverInfo); // Notar que le pasamos NULL como IP, ya que le indicamos que use localhost en AI_PASSIVE
	struct sockaddr_in conexioncpu;			// Esta estructura contendra los datos de la conexion del cliente. IP, puerto, etc.
	socklen_t addrlen = sizeof(conexioncpu);

	socketPCP = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
	maximoCpu = socketPCP;
	bind(socketPCP,serverInfo->ai_addr, serverInfo->ai_addrlen);
	listen(socketPCP, BACKLOG);

	FD_ZERO(&fdRPCP);
	FD_ZERO(&fdWPCP);
	FD_ZERO(&readPCP);
	FD_ZERO(&writePCP);
	FD_ZERO(&fd_PCP);

	FD_SET (socketPCP, &fdRPCP);

	pthread_create(&hiloHabilitarCpu, NULL, f_hiloHabilitarCpu, NULL);

	while(1)
	{
		FD_ZERO(&readPCP);
		//FD_ZERO(&writePCP);
		readPCP = fdRPCP;
		//writePCP = fdWPCP;
		statusSelect = select(maximoCpu + 1, &readPCP, NULL, NULL, NULL);
		//printf("SELECT: %d\n", statusSelect);
		for(i=3; i<=maximoCpu; i++)
		{
			if(FD_ISSET(i, &readPCP))
			{
				if(i == socketPCP)
				{
					sem_wait(&s_ConexionCpu);
					socketAux = accept(socketPCP, (struct sockaddr *) &conexioncpu, &addrlen);
					log_info(logger, "Se conecto un cpu por el socket %d", socketAux);
					printf("CPU Conectado en el socket %d.\n", socketAux);
					FD_SET(socketAux, &fdRPCP);
					agregarCpu(socketAux);
					//FD_SET(socketAux, &fdRPCP);
					if (socketAux > maximoCpu) maximoCpu = socketAux;
					log_trace(logger, "Le envio al cpu %d el quantum y retardo", socketAux);
					send(socketAux, &quantum, sizeof(int), 0);
					send(socketAux, &retardo, sizeof(int), 0);
					sem_post(&s_ConexionCpu);
				}
				else
				{
					//Cuando no es un CPU, recibe el PCB o se muere el programa;
					status = recv(i,&mensaje,sizeof(char),0);
					if(status != 0)
					{
						if(mensaje == 0) //todo:podria ser ENUM
						{
							//Se muere el programa
							log_info(logger, "Llego una solicitud del cpu %d para finalizar un programa", i);
							recv(i,&superMensaje,sizeof(superMensaje),0);
							log_trace(logger, "Recibiendo PCB con pid %d del cpu %d", superMensaje[0],i);
							pcb = recibirSuperMensaje(superMensaje);
							desencolarExec(pcb);
							pcb->siguiente = NULL;
							encolarExit(pcb);
						}
						else if(mensaje == 10) //todo:podria ser ENUM
						{
							//Se muere el programa
							log_info(logger, "Llego una solicitud del cpu %d para finalizar un programa", i);
							recv(i,&superMensaje,sizeof(superMensaje),0);
							log_trace(logger, "Recibiendo PCB con pid %d del cpu %d", superMensaje[0],i);
							pcb = recibirSuperMensaje(superMensaje);
							desencolarExec(pcb);
							pcb->siguiente = NULL;
							Programa_imprimirTexto(pcb->pid, "SEGMENTATION FAULT");
							encolarExit(pcb);
						}
						else if (mensaje == 1)
						{
							//Se termina el quantum
							log_info(logger, "El cpu %d informa que se le termino el quantum a un proceso", i);
							recv(i,&superMensaje,sizeof(superMensaje),0);
							log_trace(logger, "Recibiendo PCB con pid %d del cpu %d", superMensaje[0],i);
							pcb = recibirSuperMensaje(superMensaje);
							desencolarExec(pcb);
							pcb->siguiente = NULL;
							encolarEnReady(pcb);
						}
						else if(mensaje == 2) //todo:podria ser ENUM
						{
							//Manejo de variables compartidas.
							log_info(logger, "El cpu %d solicita acceso a variables compartidas", i);
							char mensaje2;
							recv(i,&mensaje2,sizeof(char),0);
							int tamanio = 0, valorVariable = -1;
							recv(i,&tamanio,sizeof(int),0);
							char* variable = malloc(tamanio);
							variable[tamanio] = '\0';
							printf("Tamanio del nombre de la variable: %d\n",tamanio);
							recv(i,variable,tamanio,0);
							printf("Nombre de la variable: %s\n",variable);
							if(mensaje2 == 0)
							{
								//TODO: Obtener valor variable compartida.
								//printf("Obtener valor variable compartida %s\n", variable);
								int j;
								for(j = 0; j < cantidadVariablesCompartidas; j++)
								{
									//printf("Busco las variables: Buscada: %s, actual: %s\n", variable, arrayVariablesCompartidas[j].nombreVariable);
									if(string_equals_ignore_case(arrayVariablesCompartidas[j].nombreVariable, variable))
									{
										valorVariable = arrayVariablesCompartidas[j].valorVariable;
										log_trace(logger,"Variable Global pedida: %s Valor %d",variable,valorVariable);
										break;
									}
								}
								send(i,&valorVariable,sizeof(int),0);
								free(variable);
							}
							else if (mensaje2 == 1)
							{
								//TODO: Asignar variable compartida.
								recv(i,&valorVariable,sizeof(int),0);
								//printf("Asignar valor variable compartida: %s\n", variable);
								int j;
								for(j = 0; j < cantidadVariablesCompartidas; j++)
								{
									//printf("Busco las variables: Buscada: %s, actual: %s\n", variable, arrayVariablesCompartidas[j].nombreVariable);
									if(string_equals_ignore_case(variable, arrayVariablesCompartidas[j].nombreVariable))
									{
										arrayVariablesCompartidas[j].valorVariable = valorVariable;
										log_trace(logger,"Variable Global asignada: %s Valor %d",variable,valorVariable);
										break;
									}
								}
								free(variable);
							}
							else
							{
								//Error.
								log_error(logger,"Error accediendo a variables compartidas.");
							}
						}
						else if (mensaje == 3)
						{
							//Se bloquea el PCB
							log_info(logger, "Llego una solicitud del cpu %d para enviar un programa a entrada/salida", i);
							int tamanio = 0, tiempo = -1;
							recv(i,&tamanio,sizeof(int),0);
							char* dispositivo = malloc(tamanio);
							//printf("Tamanio del nombre del dispositivo: %d\n",tamanio);
							recv(i,dispositivo,tamanio,0);
							dispositivo[tamanio] = '\0';
							recv(i,&tiempo,sizeof(int),0);
							//printf("Nombre del dispositivo: %s\n",dispositivo);
							//Recepción del PCB
							recv(i,&superMensaje,sizeof(superMensaje),0);
							//printf("PCB a EntradaSalida: %d\n", superMensaje[0]);
							pcb = recibirSuperMensaje(superMensaje);
							desencolarExec(pcb);
							pcb->siguiente = NULL;
							//Buscar el dispositivo donde encolar
							int j;
							for(j = 0; j < cantidadDispositivosIO; j++)
							{
								//printf("Busco el dispositivo. Buscado: %s, actual: %s\n", dispositivo, arrayDispositivosIO[j].nombreIO);
								if(string_equals_ignore_case(dispositivo, arrayDispositivosIO[j].nombreIO))
								{
									//Generar el elemento de la lista para encolar
									t_listaIO* aux = malloc(sizeof(t_listaIO));
									aux->pcb = pcb;
									aux->tiempo = tiempo;
									aux->siguiente = NULL;
									//Encolarlo último
									if(arrayDispositivosIO[j].pcbEnLista == NULL)
									{
										arrayDispositivosIO[j].pcbEnLista = aux;
									}
									else
									{
										t_listaIO* listaAux = arrayDispositivosIO[j].pcbEnLista;
										while(listaAux->siguiente != NULL) listaAux = arrayDispositivosIO[j].pcbEnLista->siguiente;
										listaAux->siguiente = aux;
									}
									log_info(logger,"PCB Encolado en el dispositivo %s",arrayDispositivosIO[j].nombreIO);
									sem_post(&s_IO[j]);
									break;
								}
							}
							free(dispositivo);
						}
						else if (mensaje == 4)
						{

							log_info(logger, "Llego una solicitud del cpu %d para manejo de semaforo",i);
							int tamanio = 0;
							recv(i,&tamanio,sizeof(int),0);
							char* semaforo = malloc(tamanio);
							//printf("Tamanio del nombre del semaforo: %d\n",tamanio);
							recv(i,semaforo,tamanio,0);
							semaforo[tamanio] = '\0';
							//printf("Nombre del semaforo: %s\n",semaforo);
							char mensaje2;
							recv(i,&mensaje2,sizeof(char),0);
							//printf("Operación: %d\n",mensaje2);
							//Busco afuera cuál es el semaforo,
							int j, semaforoEncontrado = -1;
							for(j = 0; j < cantidadSemaforos; j++)
							{
								//printf("Busco el semáforo. Buscado: %s, actual: %s\n", semaforo, arraySemaforos[j].nombreSemaforo);
								if(string_equals_ignore_case(semaforo, arraySemaforos[j].nombreSemaforo))
								{
									semaforoEncontrado = j;
								}
							}
							//printf("Encolo en el semaforo %s", arraySemaforos[semaforoEncontrado].nombreSemaforo);
							if(semaforoEncontrado == -1)
							{
								mensaje2 = -1; //Se bloquea el programa. Hay que pedir PCB.
								send(i,&mensaje2,sizeof(char),0);
								//printf("No se encontró el semáforo solicitado. Se destruye el PCB");
								recv(i,&superMensaje,sizeof(superMensaje),0);
								//printf("PCB a EntradaSalida: %d\n", superMensaje[0]);
								pcb = recibirSuperMensaje(superMensaje);
								desencolarExec(pcb);
								pcb->siguiente = NULL;
								encolarExit(pcb);
								break;
							}
							if(mensaje2 == 0)
							{
								//WAIT
								//printf("Wait\n");
								arraySemaforos[semaforoEncontrado].valor--;
								if(arraySemaforos[semaforoEncontrado].valor < 0)
								{
									//Se bloquea el programa.
									//printf("Se bloquea el PCB.\n");
									mensaje2 = 0; //Se bloquea el programa. Hay que pedir PCB.
									send(i,&mensaje2,sizeof(char),0);
									//Recepción del PCB
									recv(i,&superMensaje,sizeof(superMensaje),0);
									//printf("PCB a bloquear: %d\n", superMensaje[0]);
									pcb = recibirSuperMensaje(superMensaje);
									desencolarExec(pcb);
									pcb->siguiente = NULL;
									//Encolar en la cola del semáforo.
									if(arraySemaforos[semaforoEncontrado].pcb == NULL)
									{
										arraySemaforos[semaforoEncontrado].pcb = pcb;
										//printf("Encole el primer PCB en el semaforo\n");
										break;
									}
									else
									{
										t_pcb* listaAux = arraySemaforos[semaforoEncontrado].pcb;
										//printf("PID BLOQUEADO: %d\n", listaAux->pid);
										//printf("PID SIGUIENTE: %p\n", listaAux->siguiente);
										while(listaAux->siguiente != NULL)
										{
											//printf("PID BLOQUEADO: %d\n", listaAux->pid);
											//printf("PID SIGUIENTE: %p\n", listaAux->siguiente);
											listaAux = listaAux->siguiente;
										}
										listaAux->siguiente = pcb;
										pcb->siguiente = NULL;
										break;
									}
								}
								else
								{
									//Puede seguir ejecutando
									//printf("Puede seguir ejecutando\n");
									char mensaje3 = 1; //Enviar confirmación: Puede seguir ejecutando.
									send(i,&mensaje3,sizeof(char),0);
									break;
								}
							}
							else if (mensaje2 == 1)
							{
								//SIGNAL
								//printf("Signal\n");
								arraySemaforos[semaforoEncontrado].valor++;
								if(arraySemaforos[semaforoEncontrado].pcb != NULL)
								{
									t_pcb* aux = arraySemaforos[semaforoEncontrado].pcb;
									log_info(logger,"Se desbloquea el PCB %d del semáforo %s", aux->pid, arraySemaforos[semaforoEncontrado].nombreSemaforo);
									arraySemaforos[semaforoEncontrado].pcb = arraySemaforos[semaforoEncontrado].pcb->siguiente;
									aux->siguiente = NULL;
									encolarEnReady(aux);
									break;
								}
							}
							else
							{
								//Error.
								//printf("Error en la recepción de la operación.\n");
								break;
							}
							free(semaforo);
						}
						else if(mensaje == 5)	//Imprimir
						{
							log_info(logger, "Llego una solicitud del cpu %d para imprimir un valor por pantalla",i);
							operacion = 0;
							recv(i, &pid, sizeof(int), 0);
							recv(i, &valorAImprimir, sizeof(int), 0);
							log_trace(logger, "Enviando %d al programa %d", valorAImprimir, pid);
							send(pid, &operacion, sizeof(char), 0);
							send(pid, &valorAImprimir, sizeof(int), 0);
							operacion = 1;
							send(i, &operacion, sizeof(char), 0);
						}
						else if (mensaje == 6)
						{
							log_info(logger, "Llego una solicitud del cpu %d para imprimir texto por pantalla",i);
							recv(i, &pid, sizeof(int), 0);
							recv(i, &tamanioTexto, sizeof(int), 0);
							texto = malloc(tamanioTexto);	//TODO: Revisar malloc
							recv(i, texto, tamanioTexto, 0);
							Programa_imprimirTexto(pid, texto);
							operacion = 1;
							send(i, &operacion, sizeof(char), 0);
							free(texto);
						}
						else if (mensaje == -1)	//Se cerro el cpu
						{
							//Saco el socket del select
							log_info(logger, "El cpu %d informa que cerrara su conexion", i);
							FD_CLR(i, &fdRPCP);
							//Recibo pcb
							recv(i,&superMensaje,sizeof(superMensaje),0);
							pcb = recibirSuperMensaje(superMensaje);
							log_trace(logger, "Recibiendo PCB con pid %d del cpu %d", superMensaje[0], i);
							//desencolarExec(pcb);
							pcb = sacarCpuDeEjecucion(i);
							pcb->siguiente = NULL;
							encolarEnReady(pcb);
							//Hago la desconexion
							close(i);
							log_trace(logger, "Finalizada conexion con cpu %d", i);
							if (i == maximoCpu)
							{
								for(j=3; j<maximoCpu; j++)
								{
									if(FD_ISSET(j, &fdWPCP) || FD_ISSET(j, &fdRPCP))
									{
										printf("baje maximo\n");
										maximoAnterior = j;
									}
								}
								maximoCpu = maximoAnterior;
							}
						}
					}
					else
					{
						//Saco el socket del select
						log_info(logger, "Se desconecto el cpu %d", i);
						FD_CLR(i, &fdRPCP);
						pcb = sacarCpuDeEjecucion(i);
						if(pcb != NULL)
						{
							pcb->siguiente = NULL;
							Programa_imprimirTexto(pcb->pid, "Se desconecto el cpu abruptamente y no se pudo finalizar la ejecucion\n");
							log_error(logger, "El cpu %d estaba ejecutando un programa cuando se desconecto. Enviando PCB %d a EXIT", i, pcb->pid);
							encolarExit(pcb);
						}
						//Hago la desconexion
						close(i);
						log_trace(logger, "Finalizada conexion con cpu %d", i);
						if (i == maximoCpu)
						{
							for(j=3; j<maximoCpu; j++)
							{
								if(FD_ISSET(j, &fdWPCP) || FD_ISSET(j, &fdRPCP))
								{
									printf("baje maximo cpu\n");
									maximoAnterior = j;
								}
							}
							maximoCpu = maximoAnterior;
						}
					}
				}
			}
		}
	}
}

void agregarCpu(int socketID)
{
	sem_wait(&s_ColaCpu);
	t_cpu* aux = l_cpu;
	t_cpu* nodo = malloc(sizeof(t_cpu));
	nodo->socketID = socketID;
	nodo->siguiente = NULL;
	nodo->pcb = NULL;
	if(l_cpu == NULL)
	{
		l_cpu = nodo;
		sem_post(&s_CpuDisponible);
		sem_post(&s_ColaCpu);
		log_trace(logger, "Se agrego el cpu %d a la lista de cpus", socketID);
	}
	else
	{
		while(aux->siguiente != NULL)
		{
			aux = aux->siguiente;
		}
		aux->siguiente = nodo;
		nodo->siguiente = NULL;
		sem_post(&s_CpuDisponible);
		sem_post(&s_ColaCpu);
		log_trace(logger, "Se agrego el cpu %d a la lista de cpus", socketID);
	}
}


t_pcb* sacarCpuDeEjecucion(int socketID)
{
	sem_wait(&s_ColaCpu);
	t_cpu* aux = l_cpu;
	t_cpu* auxAnt;
	t_pcb* pcbRetorno;
	if(l_cpu->socketID == socketID)
	{
		l_cpu = l_cpu->siguiente;
		pcbRetorno = aux->pcb;
		free(aux);
		if(pcbRetorno == NULL)
		{
			sem_wait(&s_CpuDisponible);
		}
		sem_post(&s_ColaCpu);
		log_trace(logger, "Se saco el cpu %d de la lista de cpus", socketID);
		return pcbRetorno;
	}
	else
	{
		auxAnt = aux;
		aux = aux->siguiente;
		while(aux != NULL)
		{
			if(aux->socketID == socketID)
			{
				auxAnt->siguiente = aux->siguiente;
				pcbRetorno = aux->pcb;
				free(aux);
				if(pcbRetorno == NULL)
				{
					sem_wait(&s_CpuDisponible);
				}
				sem_post(&s_ColaCpu);
				log_trace(logger, "Se saco el cpu %d de la lista de cpus", socketID);
				return pcbRetorno;
			}
			auxAnt = aux;
			aux = aux->siguiente;
		}
		sem_post(&s_ColaCpu);
		log_error(logger, "No se encontro el cpu en la lista de cpus");
		return NULL;
	}
}

void* f_hiloHabilitarCpu(void)
{
	int superMensaje[11];
	//int status;
	int socketID;
	while(1)
	{
		sem_wait(&s_ProgramasEnReady);
		log_info(logger, "Hay PCBs encolados en READY");
		sem_wait(&s_CpuDisponible);
		sem_wait(&s_ConexionCpu);
		log_info(logger, "Hay cpus listos para ejecutar");
		t_pcb* pcbAux;
		pcbAux = desencolarReady();
		superMensaje[0] = pcbAux->pid;
		superMensaje[1] = pcbAux->segmentoCodigo;
		superMensaje[2] = pcbAux->segmentoStack;
		superMensaje[3] = pcbAux->cursorStack;
		superMensaje[4] = pcbAux->indiceCodigo;
		superMensaje[5] = pcbAux->indiceEtiquetas;
		superMensaje[6] = pcbAux->programCounter;
		superMensaje[7] = pcbAux->tamanioContextoActual;
		superMensaje[8] = pcbAux->tamanioIndiceEtiquetas;
		superMensaje[9] = pcbAux->tamanioIndiceCodigo;
		superMensaje[10] = pcbAux->peso;

		socketID = encolarExec(pcbAux);
		log_trace(logger, "Envio al cpu %d el PCB con pid %d", socketID, superMensaje[0]);
		send(socketID, superMensaje, 11*sizeof(int), 0);
		sem_post(&s_ConexionCpu);
	}
	return NULL;
}

void* f_hiloPLP()
{
	log_trace(logger, "Levanto hilo PLP");
	pthread_t hiloColaExit;
	struct addrinfo hints;
	struct addrinfo *serverInfo;
	int socketServidor;
	int socketAux;
	int i, j, status, maximo = 0;
	int maximoAnterior;
	char package[PACKAGESIZE];
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// No importa si uso IPv4 o IPv6
	hints.ai_flags = AI_PASSIVE;		// Asigna el address del localhost: 127.0.0.1
	hints.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP
	getaddrinfo(NULL, PUERTOPROGRAMA, &hints, &serverInfo); // Notar que le pasamos NULL como IP, ya que le indicamos que use localhost en AI_PASSIVE
	struct sockaddr_in programa;			// Esta estructura contendra los datos de la conexion del cliente. IP, puerto, etc.
	socklen_t addrlen = sizeof(programa);

	socketServidor = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
	maximo = socketServidor;
	bind(socketServidor,serverInfo->ai_addr, serverInfo->ai_addrlen);
	listen(socketServidor, BACKLOG);

	pthread_create(&hiloColaExit, NULL, f_hiloColaExit, NULL);


	FD_ZERO(&readPLP);
	FD_ZERO(&writePLP);
	FD_ZERO(&fdRPLP);
	FD_ZERO(&fdWPLP);

	FD_SET (socketServidor, &fdRPLP);

	while(1)
	{
		readPLP = fdRPLP;
		writePLP = fdWPLP;
		select(maximo + 1, &readPLP, NULL, NULL, NULL);
		for(i=3; i<=maximo; i++)
		{
			if(FD_ISSET(i, &readPLP))
			{
				if(i == socketServidor)
				{
					socketAux = accept(socketServidor, (struct sockaddr *) &programa, &addrlen);
					log_info(logger, "Se conecto un programa por el socket %d", socketAux);
					printf("Nuevo programa conectado en el socket %d.\n", socketAux);
					FD_SET(socketAux, &fdRPLP);
					if (socketAux > maximo) maximo = socketAux;
				}
				else
				{
					status = recv(i, (void*)package, PACKAGESIZE, 0);
					if (status != 0)
					{
						log_info(logger, "Se recibio el codigo del programa %d", i);
						FD_CLR(i, &fdRPLP);
						package[status+1] = '\0';
						t_new* nodoNew = malloc(sizeof(t_new));
						nodoNew->pid = i;
						nodoNew->codigo = package;
						nodoNew->siguiente = NULL;
						encolarEnNew(nodoNew);

					}
					else
					{
						log_error(logger, "El programa %d se cerro inesperadamente. Terminando conexion", i);
						FD_CLR(i, &fdRPLP);
						close(i);
					}
					if (i == maximo)
					{
						for(j=3; j<maximo; j++)
						{
							if(FD_ISSET(j, &fdRPLP) || FD_ISSET(j, &fdWPLP))
							{
								maximoAnterior = j;
							}
						}
						maximo = maximoAnterior;
					}
				}
			}
		}
	}
	return NULL;
}

void* f_hiloColaExit(void)
{
	t_pcb* listaExit;
	int mensajePrograma;
	while(1)
	{
		sem_wait(&s_ProgramasEnExit);
		log_info(logger, "Hay programas encolados en EXIT");
		sem_wait(&s_ColaExit);
		listaExit = l_exit;
		mensajePrograma = 2;
		log_trace(logger, "Informo al programa %d que debe terminar su ejecucion", listaExit->pid);
		send(listaExit->pid, &mensajePrograma, sizeof(char), 0);
		UMV_destruirSegmentos(listaExit->pid);
		close(listaExit->pid);
		destruirPCB(listaExit->pid);
		sem_post(&s_Multiprogramacion);
		sem_post(&s_ColaExit);
	}
	return NULL;
}

void* f_hiloIO(void* pos)
{
	int i = (int)pos;
	log_trace(logger, "Inicio del Hilo del IO: %s", arrayDispositivosIO[i].nombreIO);
	while(1)
	{
		sem_wait(&s_IO[i]);
		if(arrayDispositivosIO[i].pcbEnLista != NULL)
		{
			t_listaIO* aux = arrayDispositivosIO[i].pcbEnLista;
			while(aux != NULL)
			{
				log_trace(logger,"Dispositivo IO: %s, PID: %d, Espera de: %d ms\n", arrayDispositivosIO[i].nombreIO, aux->pcb->pid, aux->tiempo);
				usleep(aux->tiempo*arrayDispositivosIO[i].retardo*1000);
				encolarEnReady(aux->pcb);
				arrayDispositivosIO[i].pcbEnLista = aux->siguiente;
				free(aux);
				aux = arrayDispositivosIO[i].pcbEnLista;
			}
			arrayDispositivosIO[i].pcbEnLista = NULL;
		}
	}
	return 0;
}



int crearPcb(t_new programa, t_pcb* pcbAux)
{
	t_medatada_program* metadataAux = metadata_desde_literal(programa.codigo);
	int mensaje[2];
	int respuesta;
	pcbAux->pid = programa.pid;
	pcbAux->programCounter = metadataAux->instruccion_inicio;
	pcbAux->tamanioIndiceEtiquetas = metadataAux->etiquetas_size;
	pcbAux->cursorStack = 0;
	pcbAux->tamanioContextoActual = 0;
	pcbAux->siguiente = NULL;
	pcbAux->tamanioIndiceCodigo = (metadataAux->instrucciones_size*sizeof(t_intructions));
	mensaje[0] = pcbAux->pid;
	mensaje[1] = tamanioStack;
	log_trace(logger, "Solicito a la UMV crear segmento de Stack del programa %d", pcbAux->pid);
	respuesta = UMV_crearSegmentos(mensaje);
	if(respuesta == -1)
	{
		//avisar al programa :D
		Programa_imprimirTexto(programa.pid, "MEMORY OVERLOAD\n");
		free (pcbAux);
		return -1;
	}
	pcbAux->segmentoStack = respuesta;
	mensaje[1] = strlen(programa.codigo);
	log_trace(logger, "Solicito a la UMV crear segmento de Codigo del programa %d", pcbAux->pid);
	respuesta = UMV_crearSegmentos(mensaje);
	if(respuesta == -1)
	{
		//avisar al programa :D
		Programa_imprimirTexto(programa.pid, "MEMORY OVERLOAD\n");
		UMV_destruirSegmentos(pcbAux->pid);
		free (pcbAux);
		return -1;
	}
	pcbAux->segmentoCodigo = respuesta;
	mensaje[1] = (metadataAux->instrucciones_size*sizeof(t_intructions));
	log_trace(logger, "Solicito a la UMV crear segmento de Indice de Codigo del programa %d", pcbAux->pid);
	respuesta = UMV_crearSegmentos(mensaje);
	if(respuesta == -1)
	{
		//avisar al programa :D
		Programa_imprimirTexto(programa.pid, "MEMORY OVERLOAD\n");
		UMV_destruirSegmentos(pcbAux->pid);
		free (pcbAux);
		return -1;
	}
	pcbAux->indiceCodigo = respuesta;
	mensaje[1] = metadataAux->etiquetas_size;
	if(mensaje[1] != 0)
	{
		log_trace(logger, "Solicito a la UMV crear segmento de Indice de Etiquetas del programa %d", pcbAux->pid);
		respuesta = UMV_crearSegmentos(mensaje);
		if(respuesta == -1)
		{
			//avisar al programa :D
			Programa_imprimirTexto(programa.pid, "MEMORY OVERLOAD\n");
			UMV_destruirSegmentos(pcbAux->pid);
			free (pcbAux);
			return -1;
		}
		pcbAux->indiceEtiquetas = respuesta;
	}
	else
	{
		pcbAux->indiceEtiquetas = -1;
	}
	pcbAux->peso = 0;
	log_trace(logger, "El pcb se creo correctamente");
	log_trace(logger, "Envio a la UMV el Codigo del programa %d", pcbAux->pid);
	UMV_enviarBytes(pcbAux->pid, pcbAux->segmentoCodigo,0,strlen(programa.codigo),programa.codigo);
	if(metadataAux->etiquetas_size != 0)
	{
		log_trace(logger, "Envio a la UMV el Indice de Etiquetas del programa %d", pcbAux->pid);
		UMV_enviarBytes(pcbAux->pid, pcbAux->indiceEtiquetas,0,metadataAux->etiquetas_size,metadataAux->etiquetas);
	}
	log_trace(logger, "Envio a la UMV el Indice de Codigo del programa %d", pcbAux->pid);
	UMV_enviarBytes(pcbAux->pid, pcbAux->indiceCodigo,0,pcbAux->tamanioIndiceCodigo,metadataAux->instrucciones_serializado);
	return 1;
}

int UMV_crearSegmentos(int mensaje[2])
{
	sem_wait(&s_ComUmv);
	char operacion = 1;
	char confirmacion;
	int respuesta;
	send(socketUMV, &operacion, sizeof(char), 0);
	log_trace(logger, "Solicito a la UMV crear segmentos");
	recv(socketUMV, &confirmacion, sizeof(char), 0);
	if(confirmacion == 1)
	{
		send(socketUMV, mensaje, 2*sizeof(int), 0);
		log_trace(logger, "Envio segmento tamanio %d", mensaje[1]);
		recv(socketUMV, &respuesta, sizeof(int), 0);
		if(respuesta != -1)
		{
			log_info(logger, "Segmento creado correctamente");
		}
		else
		{
			log_error(logger, "MEMORY OVERLOAD");
		}
		sem_post(&s_ComUmv);
		return respuesta;
	}
	sem_post(&s_ComUmv);
	log_error(logger, "No se recibio confirmacion por parte de la UMV");
	return -1;
}

void UMV_destruirSegmentos(int pid)
{
	sem_wait(&s_ComUmv);
	char operacion = 2;
	char confirmacion;
	send (socketUMV, &operacion, sizeof(char), 0);
	log_trace(logger, "Solicito a la UMV destruir segmentos del programa %d", pid);
	recv(socketUMV, &confirmacion, sizeof(char), 0);
	if (confirmacion ==  1)
	{
		send(socketUMV, &pid, sizeof(int), 0);
		log_info(logger, "Segmentos del programa %d destruidos", pid);
		sem_post(&s_ComUmv);
		return;
	}
	log_error(logger, "No se recibio confirmacion por parte de la UMV");
	sem_post(&s_ComUmv);
	return;
}



void UMV_enviarBytes(int pid, int base, int offset, int tamanio, void* buffer)
{
	sem_wait(&s_ComUmv);
	int status;
	char operacion = 3;
	char confirmacion;
	char* package;
	send(socketUMV, &operacion, sizeof(char), 0);
	log_trace(logger, "Solicito a la UMV enviar bytes");
	recv(socketUMV, &confirmacion, sizeof(char), 0);
	if(confirmacion == 1)
	{
		package = serializarEnvioBytes(pid, base, offset, tamanio, buffer);
		log_trace(logger, "Envio datos del programa %d, base %d para almacenar", pid, base);
		send(socketUMV, package, 4*sizeof(int) + tamanio, 0);
	}
	status = recv(socketUMV,&confirmacion, sizeof(char),0);
	if(status==0)
	{
		sem_post(&s_ComUmv);
		return;
	}
	sem_post(&s_ComUmv);
	return;
}

char* serializarEnvioBytes(int pid, int base, int offset, int tamanio, void* buffer)
{
	int despl = 0;
	char* package = malloc(4*sizeof(int)+tamanio);

	memcpy(package + despl, &pid, sizeof(int));
	despl += sizeof(int);

	memcpy(package + despl, &base, sizeof(int));
	despl += sizeof(int);

	memcpy(package + despl, &offset, sizeof(int));
	despl += sizeof(int);

	memcpy(package + despl, &tamanio, sizeof(int));
	despl += sizeof(int);

	memcpy(package + despl, (char*)buffer, tamanio);

	return package;
}

void Programa_imprimirTexto(int pid, char* texto)
{
	int tamanio = strlen(texto)+1;
	texto[tamanio-1] = '\0';
	char operacion = 1;
	send(pid, &operacion, sizeof(char), 0);
	send(pid, &tamanio, sizeof(int), 0);
	send(pid, texto, tamanio, 0);
	printf("TEXTO: %s\n", texto);
}

void encolarEnNew(t_new* programa)
{
	sem_wait(&s_ColaNew);
	t_new* aux = l_new;
	//printf("Encolando %d\n",programa->pid);
	log_info(logger,"Llegó un programa y se encola en New: %d\n",programa->pid);
	if(aux == NULL)
	{
		l_new = programa;
		l_new->siguiente = NULL;
		sem_post(&s_ColaNew);
		sem_post(&s_ProgramasEnNew);
		return;
	}
	else
	{
		while(aux->siguiente != NULL)
		{
			aux = aux->siguiente;
		}
		aux->siguiente = programa;
		sem_post(&s_ColaNew);
		sem_post(&s_ProgramasEnNew);
		return;
	}
}


void conexionUMV(void)
{
	struct addrinfo hintsumv;
	struct addrinfo *umvInfo;
	char id = 0;
	char conf = 0;

	memset(&hintsumv, 0, sizeof(hintsumv));
	hintsumv.ai_family = AF_UNSPEC;		// Permite que la maquina se encargue de verificar si usamos IPv4 o IPv6
	hintsumv.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP

	getaddrinfo(IPUMV, PUERTOUMV, &hintsumv, &umvInfo);	// Carga en umvInfo los datos de la conexion

	socketUMV = socket(umvInfo->ai_family, umvInfo->ai_socktype, umvInfo->ai_protocol);

	connect(socketUMV, umvInfo->ai_addr, umvInfo->ai_addrlen);
	freeaddrinfo(umvInfo);	// No lo necesitamos mas
	send(socketUMV, &id, sizeof(char), 0);
	recv(socketUMV, &conf, sizeof(char), 0);

	log_info(logger, "Se establecio conexion con la UMV");

	return;
}

void* f_hiloMostrarNew()
{
	char* ingreso = malloc(100);
	while(1)
	{
		gets(ingreso);
		if(string_equals_ignore_case(ingreso,"mostrar-new"))
		{
			sem_wait(&s_ColaNew);
			printf("Procesos encolados en New\n");
			printf("PID:\n");
			printf("-----------------------\n");
			t_new* aux = l_new;
			while(aux != NULL)
			{
				printf("%d\t\t",aux->pid);
				//printf("%d\n",aux->peso);
				aux = aux->siguiente;
			}
			sem_post(&s_ColaNew);
			continue;
		}
		else if(string_equals_ignore_case(ingreso,"mostrar-ready"))
		{
			sem_wait(&s_ColaReady);
			printf("Procesos encolados en Ready\n");
			printf("PID:\n");
			printf("-----------------------\n");
			t_pcb* aux = l_ready;
			while(aux != NULL)
			{
				printf("%d\t\t",aux->pid);
				aux = aux->siguiente;
			}
			sem_post(&s_ColaReady);
			continue;
		}
		else if(string_equals_ignore_case(ingreso,"mostrar-exec"))
		{
			sem_wait(&s_ColaCpu);
			printf("Procesos encolados en Ejecucion\n");
			printf("PID:\n");
			printf("-----------------------\n");
			t_cpu* aux = l_cpu;
			while(aux != NULL)
			{
				if(aux->pcb != NULL) printf("%d\t\t",aux->pcb->pid);
				aux = aux->siguiente;
			}
			sem_post(&s_ColaCpu);
			continue;
		}
		else if(string_equals_ignore_case(ingreso,"mostrar-semaforos"))
		{
			printf("Listado de semáforos, valor y PCB's bloqueados si los hay\n");
			printf("Semáforo\t Valor\t PID Bloqueados\n");
			printf("-----------------------\n");
			int i;
			for(i = 0; i < cantidadSemaforos; i++)
			{
				printf("%s\t\t %d\t",arraySemaforos[i].nombreSemaforo,arraySemaforos[i].valor);
				t_pcb* aux = arraySemaforos[i].pcb;
				if(aux == NULL) printf("No tiene PCB's bloqueados\n");
				else
				{
					while(aux != NULL)
					{
						printf("%d,\t",aux->pid);
						aux = aux->siguiente;
					}
					printf("\n");
				}
			}
			continue;
		}
		else if(string_equals_ignore_case(ingreso,"mostrar-io"))
		{
			printf("Listado de dispositivos de Entrada y Salida, y su Retardo\n");
			printf("Dispositivo\t Retardo\n");
			printf("-----------------------\n");
			int i;
			for(i = 0; i < cantidadDispositivosIO; i++)
			{
				printf("%s\t\t %d\t\n",arrayDispositivosIO[i].nombreIO,arrayDispositivosIO[i].retardo);
			}
			continue;
		}
		if(string_equals_ignore_case(ingreso,"mostrar-exit"))
		{
			sem_wait(&s_ColaExit);
			printf("Procesos encolados en Exit\n");
			printf("PID:\n");
			printf("-----------------------\n");
			t_pcb* aux = l_exit;
			while(aux != NULL)
			{
				printf("%d\t\t",aux->pid);
				aux = aux->siguiente;
			}
			sem_post(&s_ColaExit);
			continue;
		}
		else
		{
			printf("Error de comando.");
			continue;
		}
	}
	free(ingreso);
	return 0;
}

t_pcb* recibirSuperMensaje ( int superMensaje[11] )
{
	sem_wait(&s_ColaCpu);
	//printf("Entre a recibir supermensaje\n");
	int i;
	t_cpu* aux = l_cpu;
	//printf("tabla cpu: %p\n", aux);
	while(aux != NULL)
	{
		//printf("ENTRO AL WHILE\n");
		if(aux->pcb != NULL)
		{
			if(aux->pcb->pid == superMensaje[0]) break;
		}
		//printf("cpu siguiente: %p\n", aux->siguiente);
		aux= aux->siguiente;
	}
	if(aux == NULL) log_error(logger,"Se recibio un PCB que no existe\n");
	//printf("CPU: %p\n", aux);
	//printf("PID: %d\n",aux->pcb->pid);
	aux->pcb->pid = superMensaje[0];
	aux->pcb->segmentoCodigo = superMensaje[1];
	aux->pcb->segmentoStack=	superMensaje[2] ;
	aux->pcb->cursorStack=superMensaje[3]  ;
	aux->pcb->indiceCodigo=superMensaje[4] ;
	aux->pcb->indiceEtiquetas=superMensaje[5]  ;
	aux->pcb->programCounter=superMensaje[6] ;
	aux->pcb->tamanioContextoActual=superMensaje[7] ;
	aux->pcb->tamanioIndiceEtiquetas=superMensaje[8] ;
	aux->pcb->tamanioIndiceCodigo=superMensaje[9] ;
	aux->pcb->peso=superMensaje[10] ;

//	for(i=0; i<11; i++){
//		printf("pcb: %d\n", superMensaje[i]);
//	}
	sem_post(&s_ColaCpu);
	return aux->pcb;
}

void cargarConfig(void)
{
	log_trace(logger, "Levanto configuracion");
	PUERTOPROGRAMA = config_get_string_value(configuracion, "PUERTOPROGRAMA");
	BACKLOG = config_get_int_value(configuracion, "BACKLOG");			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
	PACKAGESIZE = config_get_int_value(configuracion, "PACKAGESIZE");	// Define cual va a ser el size maximo del paquete a enviar
	PUERTOUMV = config_get_string_value(configuracion, "PUERTOUMV");
	IPUMV = config_get_string_value(configuracion, "IPUMV");
	PUERTOCPU = config_get_string_value(configuracion, "PUERTOCPU");
	IPCPU = config_get_string_value(configuracion, "IPCPU");
	tamanioStack = config_get_int_value(configuracion, "TAMANIOSTACK");
	quantum = config_get_int_value(configuracion, "QUANTUM");
	retardo = config_get_int_value(configuracion, "RETARDO");
	gradoMultiprogramacion = config_get_int_value(configuracion, "MULTIPROGRAMACION");
}

void cargarVariablesCompartidas(void)
{
	log_trace(logger, "Cargo variables compartidas");
	char** vars = malloc(1000);
	vars = config_get_array_value(configuracion, "VARIABLES_COMPARTIDAS");
	while(vars[cantidadVariablesCompartidas] != NULL) cantidadVariablesCompartidas++;
	int i;
	arrayVariablesCompartidas = malloc(cantidadVariablesCompartidas*sizeof(t_variableCompartida));
	for(i = 0; i < cantidadVariablesCompartidas; i++) arrayVariablesCompartidas[i].nombreVariable = vars[i];
	log_trace(logger, "Variables compartidas cargadas");
	free(vars);
}

void cargarDispositivosIO(void) //Todo: Falta agregar Retardo para cada Dispositivo. (Un hilo por cada semáforo?)
{
	log_trace(logger, "Cargo dispositivos de entrada/salida");
	pthread_t hiloIO;
	char** disp = malloc(1000);
	char** retardo = malloc(1000);
	disp = config_get_array_value(configuracion, "IO");
	retardo = config_get_array_value(configuracion, "RETARDO_IO");
	while(disp[cantidadDispositivosIO] != NULL)	cantidadDispositivosIO++;
	arrayDispositivosIO = malloc(cantidadDispositivosIO*sizeof(t_IO));
	s_IO = malloc(sizeof(sem_t)*cantidadDispositivosIO);
	int i;
	for(i = 0; i < cantidadDispositivosIO; i++)
	{
		sem_init(&s_IO[i],0,0);
		arrayDispositivosIO[i].nombreIO = disp[i];
		arrayDispositivosIO[i].retardo = atoi(retardo[i]);
		arrayDispositivosIO[i].pcbEnLista = NULL;
		pthread_create(&hiloIO, NULL, f_hiloIO, (void*)i);
	}
	log_trace(logger, "Dispositivos de entrada/salida cargados");
	free(disp);
	free(retardo);
}

void cargarSemaforos(void)
{
	log_trace(logger, "Cargo semaforos");
	char** sem = malloc(1000);
	char** val = malloc(1000);
	sem = config_get_array_value(configuracion, "SEMAFOROS");
	val = config_get_array_value(configuracion, "VALOR_SEMAFOROS");
	while(sem[cantidadSemaforos] != NULL) cantidadSemaforos++;
	arraySemaforos = malloc(cantidadSemaforos*sizeof(t_semaforo));
	int i;
	for(i = 0; i < cantidadSemaforos; i++)
	{
		arraySemaforos[i].nombreSemaforo = sem[i];
		arraySemaforos[i].valor = atoi(val[i]);
		arraySemaforos[i].pcb = NULL;
	}
	log_trace(logger, "Semáforos cargados");
	free(sem);
	free(val);
}


void destruirPCB(int pid)
{
	t_pcb* listaExit = l_exit;
	t_pcb* listaAux = NULL;
	t_pcb* aux = l_exit;
	while(aux != NULL)
	{
		//printf("PID en Exit: %d\n",aux->pid);
		aux = aux->siguiente;
	}
	if(listaExit->pid == pid)
	{
		listaAux = l_exit;
		l_exit = l_exit->siguiente;
		free(listaAux);
		log_info(logger,"PCB Destruido");
		//printf("Destrui unico PCB\n");
		return;
	}
	listaAux = listaExit;
	listaExit = listaExit->siguiente;
	while(listaExit != NULL)
	{
		if(listaExit->pid == pid)
		{
			log_info(logger,"PCB Destruido");
			listaAux->siguiente = listaExit->siguiente;
			free(listaExit);
			return;
		}
		listaAux = listaExit;
		listaExit = listaExit->siguiente;
	}
}

t_new desencolarNew(void)
{
	sem_wait(&s_ColaNew);
	int peso, pesoMax;
	t_new* aux = l_new;
	t_new* auxAnt;
	t_new* maxAnt;
	t_new* maximo = aux;
	t_new max;
	t_medatada_program* metadataAux = metadata_desde_literal(maximo->codigo);
	pesoMax = (5* metadataAux->cantidad_de_etiquetas) + (3* metadataAux->cantidad_de_funciones) + (metadataAux->instrucciones_size);
	if (aux->siguiente == NULL)
	{
		l_new = NULL;
		max = *maximo;
		free(maximo);
		sem_post(&s_ColaNew);
		log_trace(logger, "Desencolo el programa %d de new", max.pid);
		return max;
	}
	else
	{
		auxAnt = aux;
		aux = aux->siguiente;
		while(aux != NULL)
		{
			metadataAux = metadata_desde_literal(aux->codigo);
			peso = (5* metadataAux->cantidad_de_etiquetas) + (3* metadataAux->cantidad_de_funciones) + (metadataAux->instrucciones_size);
			if(peso > pesoMax)
			{
				pesoMax = peso;
				maximo = aux;
				maxAnt = auxAnt;
			}
			auxAnt = aux;
			aux = aux->siguiente;
		}
		if(maximo == l_new)
		{
			l_new = l_new->siguiente;
			max = (*maximo);
			free(maximo);
			sem_post(&s_ColaNew);
			log_trace(logger, "Desencolo el programa %d de new", max.pid);
			return max;
		}
		maxAnt->siguiente = maximo->siguiente;
		max = *maximo;
		free(maximo);
		sem_post(&s_ColaNew);
		log_trace(logger, "Desencolo el programa %d de new", max.pid);
		return max;
	}
}


void encolarEnReady(t_pcb* pcb)
{
	sem_wait(&s_ColaReady);
	t_pcb* aux = l_ready;
	if(l_ready == NULL)
	{
		l_ready = pcb;
		pcb->siguiente = NULL;
		sem_post(&s_ProgramasEnReady);
		sem_post(&s_ColaReady);
		log_trace(logger, "Se encolo en ready el pcb con PID %d", pcb->pid);
		return;
	}
	else
	{
		while(aux->siguiente != NULL)
		{
			//printf("SIGUIENTE: %p", aux->siguiente);
			aux = aux->siguiente;
		}
		aux->siguiente = pcb;
		pcb->siguiente = NULL;
		sem_post(&s_ProgramasEnReady);
		sem_post(&s_ColaReady);
		log_trace(logger, "Se encolo en ready el pcb con PID %d", pcb->pid);
		return;
	}
}

t_pcb* desencolarReady(void)
{
	sem_wait(&s_ColaReady);
	t_pcb* aux = l_ready;
	l_ready = aux->siguiente;
	log_trace(logger,"Desencolando de Ready: %d", aux->pid);
	sem_post(&s_ColaReady);
	return aux;
}

int encolarExec(t_pcb* pcb)
{
	sem_wait(&s_ColaCpu);
	t_cpu* aux = l_cpu;
	log_trace(logger,"Encolando en Exec: %d", pcb->pid);
	while(aux != NULL)
	{
		if(aux->pcb == NULL)
		{
			aux->pcb = pcb;
			sem_post(&s_ColaCpu);
			return aux->socketID;
		}
		aux = aux->siguiente;
	}
	log_error(logger,"No hay CPU disponibles para encolar el PCB: %d", pcb->pid);
	sem_post(&s_ColaCpu);
	return -1;
}

void desencolarExec(t_pcb* pcb)
{
	sem_wait(&s_ColaCpu);
	t_cpu* aux = l_cpu;
	log_trace(logger,"Desencolando PCB de Exec: %d",pcb->pid);
	while(aux != NULL)
	{
		if(aux->pcb != NULL)
		{
			if(aux->pcb->pid == pcb->pid)
			{
				aux->pcb = NULL;
				sem_post(&s_ColaCpu);
				sem_post(&s_CpuDisponible);
				//printf("Se desencolo %d de exec\n", pcb->pid);
				return;
			}
		}
		aux = aux->siguiente;
	}
	log_error(logger,"Error desencolando de Exec el PCB: %d",pcb->pid);
	//printf("Error desencolando de Exec el PCB: %d\n", pcb->pid);
	sem_post(&s_ColaCpu);
	return;
}

void encolarExit(t_pcb* pcb)
{
	sem_wait(&s_ColaExit);
	t_pcb* aux = l_exit;
	log_trace(logger,"Encolando PCB en Exit: %d",pcb->pid);
	if(l_exit == NULL)
	{
		l_exit = pcb;
		pcb->siguiente = NULL;
		sem_post(&s_ColaExit);
		sem_post(&s_ProgramasEnExit);
		return;
	}
	else
	{
		while(aux->siguiente != NULL) aux = aux->siguiente;
		aux->siguiente = pcb;
		pcb->siguiente = NULL;
		sem_post(&s_ColaExit);
		sem_post(&s_ProgramasEnExit);
		return;
	}
}
