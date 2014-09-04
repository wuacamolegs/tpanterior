/*
 ============================================================================
 Name        : plp.c
 Author      : tu macho
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <parser/metadata_program.h>


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
t_medatada_program* metadata;


/*Empezando a usar la funcion metadate_desde_literal
 * Descripcion de las estructuras del metadata(machete):
 * 	typedef struct {
		t_puntero_instruccion	start;
		t_size		offset;
	} t_intructions;

	typedef struct {
		t_puntero_instruccion	instruccion_inicio;	//El numero de la primera instruccion (Begin)
		t_size			instrucciones_size;				// Cantidad de instrucciones
		t_intructions*	instrucciones_serializado; 		// Instrucciones del programa

		t_size			etiquetas_size;					// Tamaño del mapa serializado de etiquetas
		char*			etiquetas;							// La serializacion de las etiquetas

		int				cantidad_de_funciones;
		int				cantidad_de_etiquetas;
	} t_medatada_program;
*/

int main(void) {
	puts("!");
	//textoEjemplo es el texto que tendria q resibir del proceso programa
	const char* textoEjemplo="function prueba variables a,b a = 2 b = 16 print b print a a = a + b end begin variables a, b a = 20 print a call prueba print a end";
	metadata=metadatada_desde_literal(textoEjemplo);
	int a= metadata->cantidad_de_funciones;
	printf("%d",a);
	return a;
}
