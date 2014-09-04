/*
 ============================================================================
 Name        : pcp.c
 Author      :
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>    // boolean datatype


//Estructuras de datos

typedef struct pcb
{
	int pid;
	int segmentoCodigo;
	int segmentoStack;
	int cursorStack;
	int indiceCodigo;
	int indiceEtiquetas;
	int programCounter;
	int tamanioContextoActual;
	int tamanioIndiceEtiquetas;
}pcb;


typedef struct datosPrograma{
	int finish;
	int usoDelCPU;
	pcb *pcbPrograma;
}t_datosPrograma;

typedef struct datosIO{
	int idIO;
	int tiempoIO;
	int semaforo;
}t_datosIO;

typedef struct nodoReady{
	t_datosPrograma datosProgramas;
	int prioridad;
	int *siguiente;
}t_nodoReady;

typedef struct nodoBlock{
	t_datosPrograma datosProgramas;
	int prioridad;
	int datosIO;
	int *siguiente;
}t_nodoBlock;

typedef struct nodoExit{
	pcb *pcbPrograma;
	int *siguiente;
}t_nodoExit;


//variables globales

t_nodoReady *inicioReady;
t_nodoReady *finReady;
t_nodoBlock *inicioBlock;
t_nodoBlock *finBlock;
t_nodoExit *inicioExit;
t_nodoExit *finExit;
int cantidadCPU;
int cpuDisponibles = 10 ;

//Definicion de funciones
void llegaUnPrograma(pcb pcbProceso);
t_nodoReady crearNodoReady(pcb pcbProceso);
void encolarAReady(t_nodoReady proceso);
void encolarABlock(pcb pcbProceso); //encolar el proceso que me mando CPU
void encolarAExit(t_nodoReady proceso);
void planificacionRoundRobin(t_nodoReady inicioReady);



//funciones

void llegaUnPrograma(pcb pcbProceso){
	t_nodoReady proceso = crearNodoReady(pcbProceso);
	encolarAReady(proceso);
	proceso -> prioridad = 3; //un proceso nuevo tiene la menor prioridad
}

t_nodoReady crearNodoReady(pcb pcbProceso){
	t_nodoReady proceso;
	proceso -> datosProgramas -> finish = 0;
	proceso -> datosProgramas -> usoDelCPU = sizeof(pcbProceso -> indiceCodigo);
	proceso -> datosProgramas -> pcbPrograma = pcbProceso;
	proceso -> siguiente = NULL;
	return proceso;
}


void encolarAReady(t_nodoReady proceso){
	//hay que tener en cuenta si llegan dos o mas procesos al mismo tiempo.
	//comparar prioridades


	//si hay algo en la lista
	if(inicioReady != NULL){
		finReady -> siguiente = proceso;
		finReady = proceso;

	}

	//si la lista esta vacia
	if(inicioReady == NULL){
		inicioReady = proceso;
		finReady = proceso;
	}

}

void encolarAExit(t_nodoReady proceso){
	t_nodoExit procesoAFinalizar;

	//verificar que haya terminado de ejecutar
	if(proceso -> datosProgramas -> finish == 1){
		procesoAFinalizar->	pcbPrograma = proceso -> datosProgramas -> pcbPrograma;

		//si hay algo en la lista
		if(inicioExit != NULL){
			finExit -> siguiente = procesoAFinalizar;
			procesoAFinalizar-> siguiente = finExit;
		}

		//si la lista esta vacia
		if(inicioExit == NULL){
				inicioExit = proceso;
				finExit = proceso;
		}
	}

}


void planificacionRoundRobin(t_nodoReady inicioReady){

    int cantProcesosEnReady = getElementsCount(inicioReady);

    while(cantProcesosEnReady>=0)//Hay procesos en la lista
    {
    	t_nodoReady procesoActual = inicioReady;
    	int datosPrograma = procesoActual-> datosProgramas;
    	int usoCPU = procesoActual-> datosProgramas -> usoDelCPU;
    	pcb pcbPrograma = procesoActual-> datosProgramas -> pcbPrograma;

    	if(cpuDisponibles >=0){
    		//lo asigno a un CPU
    		send(socketCPU,&datosPrograma,sizeof(pcbPrograma),0);
    		cpuDisponibles -- ;
    	}

    	int *aux = inicioReady -> siguiente;
    	inicioReady = aux;
    }

}

//suponemos estados, en realidad habria que ver cual fue la ultima instruccion
//que se ejecuto.
//estado 1 no hubieron errores, termino su quantum
//estado 2 se interrumpio
//estado 3 vuelve de un IO


void reciboSeñaldeCPU(t_nodoReady proceso,int estado){
	//el cpu termino su quantum, no hubieron errores
	if(1){
		int quantum;
		proceso ->datosProgramas ->usoDelCPU -= quantum;

		//si termino de ejecutarse
		if(proceso ->datosProgramas ->usoDelCPU <= 0){
		proceso ->datosProgramas ->finish = 1;
		encolarAExit(proceso);
		}

		//si no termino de ejecutarse
		else{
		proceso->prioridad = 1;
		encolarAReady(proceso);
		}
	}
	//se interrumpio el proceso, error.
	if(2){
		//se suspende el programa
		printf("se interrumpio el proceso");
	}

	//el proceso se mando a un I/O
	if(3){
		t_nodoBlock bloquearProceso;
		bloquearProceso-> datosProgramas = proceso-> datosProgramas;
		bloquearProceso-> datosProgramas -> usoDelCPU = 0;
		bloquearProceso-> prioridad = 2;
		bloquearProceso-> datosIO = estado; //ahi me tendria que mandar un array con los flags, estado,idIO, tiempoen iO, semaforo
		bloquearProceso-> siguiente = NULL;
		encolarABlock(bloquearProceso);
	}

	cpuDisponibles ++ ;
}

