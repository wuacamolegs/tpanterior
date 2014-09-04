/*
 ============================================================================
 Name        : cpu.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/string.h>
#include <commons/log.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <commons/config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <parser/metadata_program.h>
#include <parser/parser.h>
#include <signal.h>
#include <semaphore.h>

/* Definiciones y variables para la conexión por Sockets */
//#define PUERTOUMV "6668"
//#define IPUMV "127.0.0.1"
//#define PUERTOKERNEL "6680"
//#define IPKERNEL "127.0.0.1"
//#define BACKLOG 3	// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
//#define PACKAGESIZE 1024 	// Define cual va a ser el size maximo del paquete a enviar

/* Estructuras de datos */
typedef struct pcb
{
	int pid;						//Lo creamos nosotros
	int segmentoCodigo;				//Direccion al inicio de segmento de codigo (base)
	int segmentoStack;				//Direccion al inicio de segmento de stack
	int cursorStack;				//Cursor actual del stack (contexto actual)
	int indiceCodigo;				//Direccion al inicio de indice de codigo
	int indiceEtiquetas;			//
	int programCounter;
	int tamanioContextoActual;
	int tamanioIndiceEtiquetas;
	int tamanioIndiceCodigo;
	int peso;
	struct pcb *siguiente;
}t_pcb;

typedef struct diccionario
{
	char variable;
	int offset;
	struct diccionario *siguiente;
} t_diccionario;


/* Funciones */
void conectarConKernel();
void conectarConUMV();
void* UMV_solicitarBytes(int pid, int base, int offset, int tamanio);
void UMV_enviarBytes(int pid, int base, int offset, int tamanio, void* buffer);
char* serializarEnvioBytes(int pid, int base, int offset, int tamanio, void* buffer);
void dejarDeDarServicio();
void recibirSuperMensaje ( int* superMensaje );
void generarSuperMensaje(void);
void generarDiccionarioVariables(void);
void agregarAlDiccionario(char variable, int offset);
void liberarDiccionario(void);

/* Primitivas*/
t_puntero AnSISOP_definirVariable(t_nombre_variable identificador_variable);
t_puntero AnSISOP_obtenerPosicionVariable(t_nombre_variable identificador_variable);
t_valor_variable AnSISOP_dereferenciar(t_puntero direccion_variable);
void AnSISOP_asignar(t_puntero direccion_variable, t_valor_variable valor);
//todo:ObtenerValorCompartida, AsignarValorCompartida,
t_valor_variable AnSISOP_obtenerValorCompartida(t_nombre_compartida variable);
t_valor_variable AnSISOP_asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor);
void AnSISOP_irAlLabel(t_nombre_etiqueta nombre_etiqueta);
void AnSISOP_llamarSinRetorno(t_nombre_etiqueta etiqueta);
void AnSISOP_llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar);
void AnSISOP_finalizar(void);
void AnSISOP_retornar(t_valor_variable retorno);
void AnSISOP_imprimir(t_valor_variable valor_mostrar);
void AnSISOP_imprimirTexto(char* texto);
void AnSISOP_entradaSalida(t_nombre_dispositivo dispositivo, int tiempo);
void AnSISOP_wait(t_nombre_semaforo identificador_semaforo);
void AnSISOP_signal(t_nombre_semaforo identificador_semaforo);

AnSISOP_funciones funciones = {
		.AnSISOP_asignar				= AnSISOP_asignar,
		.AnSISOP_asignarValorCompartida = AnSISOP_asignarValorCompartida,
		.AnSISOP_definirVariable		= AnSISOP_definirVariable,
		.AnSISOP_dereferenciar			= AnSISOP_dereferenciar,
		.AnSISOP_entradaSalida 			= AnSISOP_entradaSalida,
		.AnSISOP_finalizar				= AnSISOP_finalizar,
		.AnSISOP_imprimir				= AnSISOP_imprimir,
		.AnSISOP_imprimirTexto			= AnSISOP_imprimirTexto,
		.AnSISOP_irAlLabel 				= AnSISOP_irAlLabel,
		.AnSISOP_llamarConRetorno 		= AnSISOP_llamarConRetorno,
		.AnSISOP_llamarSinRetorno 		= AnSISOP_llamarSinRetorno,
		.AnSISOP_obtenerPosicionVariable= AnSISOP_obtenerPosicionVariable,
		.AnSISOP_obtenerValorCompartida = AnSISOP_obtenerValorCompartida,
		.AnSISOP_retornar 				= AnSISOP_retornar,

};
AnSISOP_kernel kernel_functions = {
		.AnSISOP_signal 				= AnSISOP_signal,
		.AnSISOP_wait					= AnSISOP_wait,
};

/* Variables Globales */
int kernelSocket;
int socketUMV;
int quantum = 3; //todo:quantum que lee de archivo de configuración
int retardo = 0;
int estaEjecutando = 0;
char estadoCPU;
bool matarCPU = 0;
bool terminarPrograma = 0;
bool bloquearPrograma = 0;
bool errorDeEjecucion = 0;
t_pcb* pcb;
int superMensaje[11];
char* PUERTOUMV;
char* IPUMV;
char* PUERTOKERNEL;
char* IPKERNEL;
int BACKLOG;	// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
int PACKAGESIZE;
t_diccionario* diccionarioVariables;
t_log* logger;

sem_t s_terminarCPU;

//todo:Primitivas, Hot Plug.

int main(int cantArgs, char** args){

//	void* package = malloc(sizeof(t_pcb));
//	void* indiceCodigo;
//	char ping;
//	char puedeRecibir = 10;
	char* instruccionAEjecutar = malloc(1);
	logger = log_create(NULL, "CPU", 0, LOG_LEVEL_TRACE);
	t_config* configuracion = config_create(args[1]);
	BACKLOG = config_get_int_value(configuracion, "BACKLOG");			// Define cuantas conexiones vamos a mantener pendientes al mismo tiempo
	PACKAGESIZE = config_get_int_value(configuracion, "PACKAGESIZE");	// Define cual va a ser el size maximo del paquete a enviar
	PUERTOUMV = config_get_string_value(configuracion, "PUERTOUMV");
	IPUMV = config_get_string_value(configuracion, "IPUMV");
	PUERTOKERNEL = config_get_string_value(configuracion, "PUERTOKERNEL");
	IPKERNEL = config_get_string_value(configuracion, "IPKERNEL");
	t_intructions* instruccionABuscar;
	int quantumUtilizado = 1;
	signal(SIGUSR1,dejarDeDarServicio);
	sem_init(&s_terminarCPU,0,1);
	conectarConUMV();
	conectarConKernel();
	pcb = malloc (sizeof(t_pcb));
	printf("==========\t CPU \t==========\n");
	printf("Conectado con Kernel (Socket: %d), y con UMV (Socket: %d)\n",kernelSocket,socketUMV);

	int i,a;
	while(1)
	{
		terminarPrograma = 0;
		bloquearPrograma = 0;

		log_trace(logger, "Esperando PCB");
		int recibido = recv(kernelSocket,superMensaje,sizeof(t_pcb),0);
		estaEjecutando = 1;
		log_info(logger, "Llego el PCB con pid %d para ejecutar", superMensaje[0]);
		recibirSuperMensaje(superMensaje);

		if(pcb->tamanioContextoActual != 0)
		{
			generarDiccionarioVariables();
		}

		while(quantumUtilizado<=quantum)
		{
			log_trace(logger, "Ejecuto rafaga del programa %d", pcb->pid);
			log_trace(logger, "Calculo instruccion a buscar");
			instruccionABuscar = UMV_solicitarBytes(pcb->pid,pcb->indiceCodigo,pcb->programCounter*8,sizeof(t_intructions));
			instruccionAEjecutar = realloc(instruccionAEjecutar, instruccionABuscar->offset);
			log_trace(logger, "Busco instruccion a ejecutar");
			instruccionAEjecutar = UMV_solicitarBytes(pcb->pid,pcb->segmentoCodigo,instruccionABuscar->start,instruccionABuscar->offset);
			if(instruccionAEjecutar == NULL)
			{
				log_error(logger, "Error en la lectura de memoria. Finalizando la ejecución del programa");
				estadoCPU = 0;
				send(kernelSocket,&estadoCPU,sizeof(char),0);
				generarSuperMensaje();
				send(kernelSocket,superMensaje, sizeof(int)*11,0);
				break;
			}
			instruccionAEjecutar[instruccionABuscar->offset-1] = '\0';
			printf("Ejecutando Instruccion: %s\n",instruccionAEjecutar);
			log_info(logger, "Instruccion: %s", instruccionAEjecutar);
			log_trace(logger, "Llamo al parser");
			analizadorLinea(instruccionAEjecutar,&funciones,&kernel_functions);
			log_trace(logger, "Espero %d segundos de retardo", retardo);
			usleep(retardo*1000);
			pcb->programCounter++;
			quantumUtilizado++;
			if(terminarPrograma)
			{
				log_info(logger, "El programa %d finalizo su ejecucion", pcb->pid);
				//printf("error %d", errorDeEjecucion);
				if(!errorDeEjecucion)
				{
					estadoCPU = 0;
				}
				else
				{
					estadoCPU = 10;
				}
				send(kernelSocket,&estadoCPU,sizeof(char),0);
				generarSuperMensaje();
				send(kernelSocket,superMensaje, sizeof(int)*11,0);
				liberarDiccionario();
				break;
			}
			if(bloquearPrograma)
			{
				log_info(logger, "El programa %d se bloqueo", pcb->pid);
				generarSuperMensaje();
				send(kernelSocket,superMensaje, sizeof(int)*11,0);
				liberarDiccionario();
				printf("PCB Devuelto al Kernel. El programa se bloquea\n");
				break;
			}

			if(matarCPU == 1)
			{
				estadoCPU = -1;
				log_trace(logger, "Aviso al kernel que voy a terminar la ejecucion");
				send(kernelSocket,&estadoCPU,sizeof(char),0); //avisar que se muere
				generarSuperMensaje();
				send(kernelSocket,superMensaje, sizeof(int)*11,0);
				close(kernelSocket);
				close(socketUMV);
				free(pcb);
				printf("PCB Devuelto al Kernel. La CPU se cierra.\n");
				return 0;
			}
		}
		log_trace(logger, "El programa %d finalizo su quantum", pcb->pid);
		quantumUtilizado = 1;
		if(terminarPrograma || bloquearPrograma)
		{
			estaEjecutando = 0;
			continue; //Si el programa ya salió por algo, no mandarlo de vuelta.
		}
		estadoCPU = 1;
		send(kernelSocket,&estadoCPU,sizeof(char),0); //avisar que se termina el quantum
		generarSuperMensaje();
		send(kernelSocket,superMensaje, sizeof(int)*11,0);
		estaEjecutando = 0;
		liberarDiccionario();
		printf("PCB Devuelto al Kernel. Termina el Quantum\n");
		if(matarCPU == 1)
		{
			log_trace(logger, "Finalizo ejecucion");
			close(kernelSocket);
			close(socketUMV);
			return 0;
		}
	}
	return 0;
}

void conectarConKernel()
{
	struct addrinfo hintsKernel;
	struct addrinfo *kernelInfo;

	int a = -1;

	memset(&hintsKernel, 0, sizeof(hintsKernel));
	hintsKernel.ai_family = AF_UNSPEC;		// Permite que la maquina se encargue de verificar si usamos IPv4 o IPv6
	hintsKernel.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP

	getaddrinfo(IPKERNEL, PUERTOKERNEL, &hintsKernel, &kernelInfo);	// Carga en serverInfo los datos de la conexion
	kernelSocket = socket(kernelInfo->ai_family, kernelInfo->ai_socktype, kernelInfo->ai_protocol);
	while (a == -1){
		a = connect(kernelSocket, kernelInfo->ai_addr, kernelInfo->ai_addrlen);
	}
	log_info(logger, "Se establecio conexion con el kernel por el socket %d", kernelSocket);
	recv(kernelSocket, &quantum, sizeof(int), 0);
	log_trace(logger, "Recibo valor de quantum: %d", quantum);
	recv(kernelSocket, &retardo, sizeof(int), 0);
	log_trace(logger, "Recibo valor de retardo: %d", retardo);
	freeaddrinfo(kernelInfo);	// No lo necesitamos mas

}

void conectarConUMV()
{
	struct addrinfo hintsUmv;
	struct addrinfo *umvInfo;
	char id = 1;
	char conf = 0;

	memset(&hintsUmv, 0, sizeof(hintsUmv));
	hintsUmv.ai_family = AF_UNSPEC;		// Permite que la maquina se encargue de verificar si usamos IPv4 o IPv6
	hintsUmv.ai_socktype = SOCK_STREAM;	// Indica que usaremos el protocolo TCP

	getaddrinfo(IPUMV, PUERTOUMV, &hintsUmv, &umvInfo);	// Carga en serverInfo los datos de la conexion
	socketUMV = socket(umvInfo->ai_family, umvInfo->ai_socktype, umvInfo->ai_protocol);

	connect(socketUMV, umvInfo->ai_addr, umvInfo->ai_addrlen);
	freeaddrinfo(umvInfo);	// No lo necesitamos mas

	send(socketUMV, &id, sizeof(char), 0);
	recv(socketUMV, &conf, sizeof(char), 0);

	log_info(logger, "Se establecio conexion con la UMV por el socket %d", socketUMV);
}

void dejarDeDarServicio()
{
	log_info(logger, "Se recibio señal para finalizar la ejecucion");
	matarCPU = 1;
	if(!estaEjecutando) kill(getpid(), SIGKILL);
}

t_pcb* deserializarPcb(void* package)
{
	t_pcb* pcb = malloc(sizeof(t_pcb));
	int offset = 0;
	memcpy(&(pcb->pid),package,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->programCounter),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->tamanioIndiceEtiquetas),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->cursorStack),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->tamanioContextoActual),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->siguiente),package+offset,sizeof(t_pcb*));
	offset += sizeof(t_pcb*);

	memcpy(&(pcb->tamanioIndiceCodigo),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->segmentoStack),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->segmentoCodigo),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->indiceCodigo),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->indiceEtiquetas),package+offset,sizeof(int));
	offset += sizeof(int);

	memcpy(&(pcb->peso),package+offset,sizeof(int));

	pcb->siguiente= NULL;
	//free(package);

	return pcb;
}

void* UMV_solicitarBytes(int pid, int base, int offset, int tamanio)
{
	char operacion = 0;
	char confirmacion;
	char haySegFault;
	void* buffer = malloc(tamanio);
	int mensaje[4], status;
	mensaje[0] = pid;
	mensaje[1] = base;
	mensaje[2] = offset;
	mensaje[3] = tamanio;
	log_trace(logger, "Solicito bytes a la UMV");
	send(socketUMV, &operacion, sizeof(char), 0);
	//printf("Pedido enviado\n");
	status = recv(socketUMV, &confirmacion, sizeof(char), 0);
	//printf("confirmacion recibida %d\n",confirmacion);
	if(confirmacion != 0)
	{
		send(socketUMV, mensaje, 4*sizeof(int), 0);
		recv(socketUMV, &haySegFault, sizeof(char), 0);
		if(!haySegFault)
		{
			status = recv(socketUMV, buffer, tamanio, 0);
			log_trace(logger, "Recibo datos desde la UMV");
			return buffer;
		}
		else
		{
			terminarPrograma = 1;
			errorDeEjecucion = 1;
			log_error(logger, "SEGMENTATION FAULT");
			printf("= Segmentation Fault =\n");
			return NULL;
		}
	}
	else
	{
		terminarPrograma = 1;
		errorDeEjecucion = 1;
		log_error(logger, "No se recibio confirmacion por parte de la UMV");
		return NULL;
	}
}

void UMV_enviarBytes(int pid, int base, int offset, int tamanio, void* buffer)
{
	//int status = 1;
	char operacion = 3;
	char confirmacion;
	char conf;
	char* package;
	log_trace(logger, "Solicito enviar bytes a la UMV");
	send(socketUMV, &operacion, sizeof(char), 0);
	recv(socketUMV, &confirmacion, sizeof(char), 0);
	if(confirmacion == 1)
	{
		package = serializarEnvioBytes(pid, base, offset, tamanio, buffer);
		send(socketUMV, package, 4*sizeof(int) + tamanio, 0);
		recv(socketUMV, &conf, sizeof(char), 0);
		if(conf == -1)
		{
			log_error(logger, "SEGMENTATION FAULT");
			printf("= Segmentation Fault =\n");
			terminarPrograma = 1;
			errorDeEjecucion = 1;
			return;
		}
		log_trace(logger, "La informacion se envio correctamente");
		return;
	}
	terminarPrograma = 1;
	errorDeEjecucion = 1;
	log_error(logger, "No se recibio confirmacion por parte de la UMV");
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

void recibirSuperMensaje ( int* superMensaje )
{
	pcb->pid = superMensaje[0];
	pcb->segmentoCodigo = superMensaje[1];
	pcb->segmentoStack=	superMensaje[2] ;
	pcb->cursorStack=superMensaje[3]  ;
	pcb->indiceCodigo=superMensaje[4] ;
	pcb->indiceEtiquetas=superMensaje[5]  ;
	pcb->programCounter=superMensaje[6] ;
	pcb->tamanioContextoActual=superMensaje[7] ;
	pcb->tamanioIndiceEtiquetas=superMensaje[8] ;
	pcb->tamanioIndiceCodigo=superMensaje[9] ;
	pcb->peso=superMensaje[10] ;

//	int i;
//	for(i=0; i<11; i++){
//		printf("pcb: %d\n", superMensaje[i]);
//	}
	return;
}

void generarSuperMensaje(void)
{
	superMensaje[0] = pcb->pid;
	superMensaje[1] = pcb->segmentoCodigo;
	superMensaje[2] = pcb->segmentoStack;
	superMensaje[3] = pcb->cursorStack;
	superMensaje[4] = pcb->indiceCodigo;
	superMensaje[5] = pcb->indiceEtiquetas;
	superMensaje[6] = pcb->programCounter;
	superMensaje[7] = pcb->tamanioContextoActual;
	superMensaje[8] = pcb->tamanioIndiceEtiquetas;
	superMensaje[9] = pcb->tamanioIndiceCodigo;
	superMensaje[10] = pcb->peso;
//	int i;
//	for(i=0; i<11; i++){
//		printf("pcb: %d\n", superMensaje[i]);
//	}
	return;
}

void generarDiccionarioVariables(void)
{
	log_trace(logger, "Genero diccionario de variables");
	int aux = 0;
	char* buffer = malloc(pcb->tamanioContextoActual * 5);
	t_diccionario* diccionarioAux;
	t_diccionario* nodo;
	//t_diccionario* puntero;
	buffer = UMV_solicitarBytes(pcb->pid,pcb->segmentoStack,pcb->cursorStack,(pcb->tamanioContextoActual * 5));
	while(aux < (pcb->tamanioContextoActual * 5))
	{
		if(diccionarioVariables == NULL)
		{
			diccionarioVariables = malloc(sizeof(t_diccionario));
			diccionarioVariables->variable = buffer[aux];
			log_trace(logger, "Variable: %c", diccionarioVariables->variable);
			diccionarioVariables->offset = pcb->cursorStack + aux + 1;
			diccionarioVariables->siguiente = NULL;
			aux = aux + 5;
			diccionarioAux = diccionarioVariables;
		}
		else
		{
			nodo = malloc(sizeof(t_diccionario));
			nodo->variable = buffer[aux];
			log_trace(logger, "Variable: %c", nodo->variable);
			nodo->offset = pcb->cursorStack + aux + 1;
			nodo->siguiente = NULL;
			aux = aux + 5;
			diccionarioAux->siguiente = nodo;
			diccionarioAux = diccionarioAux->siguiente;
		}
	}
	log_trace(logger, "Diccionario de variables armado");
	return;
}

void agregarAlDiccionario(char variable, int offset)
{
	log_trace(logger, "Agrego variable %c al diccionario", variable);
	t_diccionario* diccionarioAux = diccionarioVariables;
	t_diccionario* nodo = malloc(sizeof(t_diccionario));
	nodo->variable = variable;
	nodo->offset = offset;
	nodo->siguiente = NULL;
	if(diccionarioVariables == NULL)
	{
		diccionarioVariables = nodo;
	}
	else
	{
		while(diccionarioAux->siguiente != NULL)
		{
			diccionarioAux = diccionarioAux->siguiente;
		}
		diccionarioAux->siguiente = nodo;
	}
	return;
}

void liberarDiccionario(void)
{
	log_trace(logger, "Elimino diccionario");
	t_diccionario* diccionarioAux = diccionarioVariables;
	while(diccionarioVariables != NULL)
	{
		diccionarioVariables = diccionarioVariables->siguiente;
		free(diccionarioAux);
		diccionarioAux = diccionarioVariables;
	}
}

/*
 * PRIMITVAS
 */

t_puntero AnSISOP_definirVariable(t_nombre_variable identificador_variable)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Definir Variable\n");
		int offset;
		char variable = (char) identificador_variable;
		UMV_enviarBytes(pcb->pid,pcb->segmentoStack,(pcb->cursorStack + pcb->tamanioContextoActual * 5),5,&variable);
		offset = pcb->cursorStack + pcb->tamanioContextoActual * 5 + 1;
		agregarAlDiccionario(variable, offset);
		pcb->tamanioContextoActual++;
		//todo:Diccionario de variables. ??? ???
		return offset;
	}
	else
	{
		return 0;
	}
}

t_puntero AnSISOP_obtenerPosicionVariable(t_nombre_variable identificador_variable)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Obtener Posicion Variable\n");
		int offset = -1;
		//int aux = 0;
		t_diccionario* diccionarioAux = diccionarioVariables;
		//char* buffer = malloc(pcb->tamanioContextoActual * 5);
		//buffer = UMV_solicitarBytes(pcb->pid,pcb->segmentoStack,pcb->cursorStack,(pcb->tamanioContextoActual * 5));
		while(diccionarioAux != NULL)
		{
			if(diccionarioAux->variable == identificador_variable)
			{
				//free(buffer);
				offset = diccionarioAux->offset;
				return (offset);
			}
			diccionarioAux = diccionarioAux->siguiente;
		}
		//free(buffer);
		return offset;
	}
	else
	{
		return 0;
	}
}

t_valor_variable AnSISOP_dereferenciar(t_puntero direccion_variable)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Dereferenciar Variable\n");
		int* valor;
		valor = UMV_solicitarBytes(pcb->pid,pcb->segmentoStack,direccion_variable,4);
		if(valor == NULL)
		{
			printf("VALOR ES NULL\n");
			return 0;
		}
		//printf("Dereferenciar puntero: %d, valor: %d\n", direccion_variable, *valor);
		return *valor;
	}
	else
	{
		return 0;
	}
}

void AnSISOP_asignar(t_puntero direccion_variable, t_valor_variable valor)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Asignar Variable\n");
		//printf("Valor: %d, Dirección: %d\n", valor, direccion_variable);
		UMV_enviarBytes(pcb->pid,pcb->segmentoStack,direccion_variable,4,&valor);
	}
}

//TODO:ObtenerValorCompartida, asignarVariableCompartida
t_valor_variable AnSISOP_obtenerValorCompartida(t_nombre_compartida variable)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Obtener Valor de Variable Compartida: %s\n", variable);
		char estadoCPU = 2; //Trabajar con variables compartidas
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		char mensaje2 = 0; //Pedir valor de variable
		send(kernelSocket,&mensaje2,sizeof(char),0);
		//todo: Chequear acá si hubo error recibiendo. Marcar terminarPrograma = 1.
		int tamanio = strlen(variable);
		//printf("Tamanio: %d\n", tamanio);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,variable,tamanio,0);
		t_valor_variable valorVariable;
		recv(kernelSocket,&valorVariable,sizeof(int),0);
		if(valorVariable == -1) printf("Error recibiendo el valor de la variable");
		//printf("Valor de la variable: %d\n", valorVariable);
		return valorVariable;
	}
	else
	{
		return 0;
	}
}

t_valor_variable AnSISOP_asignarValorCompartida(t_nombre_compartida variable, t_valor_variable valor)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Asignar Valor de Variable Compartida: %s\n", variable);
		estadoCPU = 2; //Trabajar con variables compartidas
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		char mensaje2 = 1; //Asignar valor de variable
		send(kernelSocket,&mensaje2,sizeof(char),0);
		//todo: Chequear acá si hubo error recibiendo. Marcar terminarPrograma = 1.
		int tamanio = strlen(variable);
		//printf("Tamanio: %d\n", tamanio);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,variable,tamanio,0);
		send(kernelSocket,&valor,sizeof(int),0);
		return valor;
	}
	else
	{
		return 0;
	}
}

void AnSISOP_irAlLabel(t_nombre_etiqueta nombre_etiqueta)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Ir al Label\n");
		void* buffer = malloc(pcb->tamanioIndiceEtiquetas);
		if(string_ends_with(nombre_etiqueta, "\n")) string_trim(&nombre_etiqueta);
	//	char* etiquetas = malloc(pcb->tamanioIndiceEtiquetas);
		//printf("tamanio indice etiquetas: %d\n", pcb->tamanioIndiceEtiquetas);
		t_puntero_instruccion instruccion;
		memcpy(buffer, UMV_solicitarBytes(pcb->pid,pcb->indiceEtiquetas,0,pcb->tamanioIndiceEtiquetas), pcb->tamanioIndiceEtiquetas);
	//	memcpy(etiquetas,buffer,pcb->tamanioIndiceEtiquetas);
		instruccion = metadata_buscar_etiqueta(nombre_etiqueta,buffer,pcb->tamanioIndiceEtiquetas);
		//char* n = etiquetas;
		//printf("etiquetas: \n");
		//printf("%s \n", etiquetas);
	//	printf("largo etiquetas: %d\n", strlen(etiquetas));
		//printf("etiqueta: %s\n", nombre_etiqueta);
		//printf("instruccion: %d\n", instruccion);
		//scanf("%d", &instruccion);

		pcb->programCounter = instruccion - 1;

	//	free(etiquetas);
		return;
	}
}

void AnSISOP_llamarSinRetorno(t_nombre_etiqueta etiqueta)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Llamar Sin Retorno\n");
		//printf("Etiqueta: %s\n", etiqueta);
		void* buffer = malloc(8);
		memcpy(buffer,&(pcb->cursorStack),4);
		memcpy((buffer+4),&(pcb->programCounter),4);
		UMV_enviarBytes(pcb->pid,pcb->segmentoStack,(pcb->cursorStack + (pcb->tamanioContextoActual*5)),8,buffer);
		pcb->cursorStack = pcb->cursorStack + 8 + (pcb->tamanioContextoActual*5);
		AnSISOP_irAlLabel(etiqueta);
		pcb->tamanioContextoActual = 0;
		liberarDiccionario();
		free(buffer);
		return;
	}
}


void AnSISOP_llamarConRetorno(t_nombre_etiqueta etiqueta, t_puntero donde_retornar)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Llamar Con Retorno\n");
		void* buffer = malloc(12);
		memcpy(buffer,&(pcb->cursorStack),4);
		memcpy((buffer+4),&donde_retornar,4);
		memcpy((buffer+8),&(pcb->programCounter),4);
		UMV_enviarBytes(pcb->pid,pcb->segmentoStack,(pcb->cursorStack + (pcb->tamanioContextoActual*5)),12,buffer);
		pcb->cursorStack = pcb->cursorStack + 12 + (pcb->tamanioContextoActual*5);
		AnSISOP_irAlLabel(etiqueta);
		pcb->tamanioContextoActual = 0;
		liberarDiccionario();
		free(buffer);
		return;
	}
}

void AnSISOP_finalizar(void)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Finalizar. ");
		if(pcb->cursorStack == 0)
		{
			//printf("El programa termina\n");
			terminarPrograma = 1;
			return;
		}
		else
		{
			liberarDiccionario();
			//printf("Termina la funcion\n");
			int contexto_anterior, instruccion_a_ejecutar;
			void* buffer = malloc(8);
			buffer = UMV_solicitarBytes(pcb->pid,pcb->segmentoStack,(pcb->cursorStack - 8),8);
			memcpy(&instruccion_a_ejecutar,(buffer+4),4);
			memcpy(&contexto_anterior,buffer,4);
			pcb->tamanioContextoActual = (pcb->cursorStack - 8 - contexto_anterior) / 5;
			//printf("Contexto Actual: %d\n", pcb->tamanioContextoActual);
			pcb->programCounter = instruccion_a_ejecutar;
			pcb->cursorStack = contexto_anterior;
			generarDiccionarioVariables();
			free(buffer);
			return;
		}
	}
}

void AnSISOP_retornar(t_valor_variable retorno)
{
	if(!errorDeEjecucion)
	{
		int contexto_anterior, instruccion_a_ejecutar, direccion_a_retornar;
		void* buffer = malloc(12);
		liberarDiccionario();
		buffer = UMV_solicitarBytes(pcb->pid,pcb->segmentoStack,(pcb->cursorStack - 12),12);
		memcpy(&instruccion_a_ejecutar,(buffer+8),4);
		memcpy(&direccion_a_retornar,(buffer+4),4);
		memcpy(&contexto_anterior,buffer,4);
		UMV_enviarBytes(pcb->pid,pcb->segmentoStack,direccion_a_retornar,4,&retorno);
		pcb->programCounter = instruccion_a_ejecutar;
		pcb->tamanioContextoActual = (pcb->cursorStack - 12 - contexto_anterior) / 5;
		pcb->cursorStack = contexto_anterior;
		generarDiccionarioVariables();
		free(buffer);
		return;
	}
}

void AnSISOP_imprimir(t_valor_variable valor_mostrar)
{
	if(!errorDeEjecucion)
	{
		char confirmacion;
		//printf("Primitiva imprimir: %d\n", valor_mostrar);
		estadoCPU = 5; //Imprimir
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		send(kernelSocket,&pcb->pid,sizeof(int),0);
		send(kernelSocket,&valor_mostrar,sizeof(int),0);
		recv(kernelSocket,&confirmacion,sizeof(char),0);
		return;
	}
}

void AnSISOP_imprimirTexto(char* texto)
{
	if(!errorDeEjecucion)
	{
		char confirmacion;
		int tamanio = strlen(texto)+1;
		texto[tamanio-1] = '\0';
		//printf("Primitiva imprimir texto: %s\n", texto);
		estadoCPU = 6; //Imprimir
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		send(kernelSocket,&pcb->pid,sizeof(int),0);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,texto,tamanio,0);
		recv(kernelSocket,&confirmacion,sizeof(char),0);
		return;
	}
}

void AnSISOP_entradaSalida(t_nombre_dispositivo dispositivo, int tiempo)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Entrada Salida: %s\n", dispositivo);
		estadoCPU = 3; //Trabajar con entrada/Salida
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		int tamanio = strlen(dispositivo);
		//printf("Tamanio del nombre del dispositivo: %d\n", tamanio);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,dispositivo,tamanio,0);
		send(kernelSocket,&tiempo,sizeof(int),0);
		bloquearPrograma = 1;
		return;
	}
}

void AnSISOP_wait(t_nombre_semaforo identificador_semaforo)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Wait Semaforo: %s\n", identificador_semaforo);
		estadoCPU = 4; //Trabajar con entrada/Salida
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		int tamanio = strlen(identificador_semaforo);
		//tamanio--;
		//printf("Tamanio del nombre del semaforo: %d\n", tamanio);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,identificador_semaforo,tamanio,0);
		char operacion = 0, confirmacion = -1; //Wait
		send(kernelSocket,&operacion,sizeof(char),0);
		recv(kernelSocket,&confirmacion,sizeof(char),0);
		if(confirmacion == 0)
		{
			//Se bloquea el programa. Hay que enviar PCB
			//printf("Se bloquea el programa\n");
			bloquearPrograma = 1;
			return;
		}
		else if (confirmacion == 1)
		{
			//Se puede seguir ejecutando.
			//printf("Puedo seguir ejecutando\n");
			return;
		}
		else
		{
			//Error de comunicación. No llegó la confirmación.
			//printf("No se pudo recibir la confirmación\n");
			terminarPrograma = 1;
			return;
		}
	}
}

void AnSISOP_signal(t_nombre_semaforo identificador_semaforo)
{
	if(!errorDeEjecucion)
	{
		//printf("Primitiva Signal Semaforo: %s\n", identificador_semaforo);
		estadoCPU = 4; //Trabajar con entrada/Salida
		send(kernelSocket,&estadoCPU,sizeof(char),0);
		int tamanio = strlen(identificador_semaforo);
		//tamanio--;
		//printf("Tamanio del nombre del semaforo: %d\n", tamanio);
		send(kernelSocket,&tamanio,sizeof(int),0);
		send(kernelSocket,identificador_semaforo,tamanio,0);
		char operacion = 1; //Signal
		send(kernelSocket,&operacion,sizeof(char),0);
		return;
	}
}

