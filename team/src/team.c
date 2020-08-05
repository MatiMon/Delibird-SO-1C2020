/*
 ============================================================================
 Name        : team.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include "team.h"
#include "pokemon_utils.h"

//constantes una vez que se inicialicen
int32_t cantidad_entrenadores = 0;
int32_t socket_escucha_team;
int RETARDO_CICLO_CPU = 0;

int ciclos_totales = 0; //ciclos de intercambios + ciclos de cada entrenador por moverse
int deadlocks_resueltos_totales = 0; // deadlocks resueltos
int cambios_contexto = -1; //el primer proceso que se carga no cuenta como cambio de contexto
int total_deadlock_resueltos;
int proceso_ejecutando = -1;
t_list* total_deadlocks;

bool cambio_cola_ready = false;

//colas de planificacion
t_list* cola_ready;
t_list* entrenadores_DL;

//semaforos
sem_t s_cola_ready_con_items;
sem_t s_procesos_en_exec;
sem_t s_posiciones_a_mover;
sem_t s_control_planificador_rr;
sem_t s_detectar_deadlock;
sem_t s_replanificar;
sem_t s_entrenador_exit;
sem_t s_replanificar_sjfcd;
pthread_mutex_t mutex_cola_ready;
pthread_mutex_t mutex_ciclos_totales;
pthread_mutex_t mutex_deadlocks_totales;
pthread_mutex_t mutex_deadlocks_resueltos_totales;



//colas de mensajes
t_list* pokemones_recibidos; // registro para saber si rechazar appeareds y localizeds
t_list* pokemones_ubicados; // estos son los pokemones capturables. Esta lista varía con el tiempo.
t_list* mensajes_get_esperando_respuesta; // seguramente algun id o algo
t_list* mensajes_catch_esperando_respuesta; // seguramente algun id o algo


void show_estado(){

	printf("PLANIFICACION: \n	ALGORITMO: %s, QUANTUM: %d\n", algoritmo.algoritmo_string, algoritmo.quantum);
	    printf("********************\n");


	    void _mostrar_pokemon(void* elemento){
	    	return mostrar_pokemon((t_pokemon_team*)elemento, objetivo_global);
	    }


	    for(int32_t i = 0; i < entrenadores->elements_count; i++){
	    	//pthread_mutex_lock(&mutex_list_entrenadores);
	    	t_entrenador* entrenador_actual = list_get(entrenadores, i);
	    	//pthread_mutex_unlock(&mutex_list_entrenadores);
	    	printf("|ENTRENADOR: %d\n|----------------\n|POSICION: (%d,%d)\n",
	    			i,
					entrenador_actual->posicion.X,
					entrenador_actual->posicion.Y);
	    	printf("|ESTADO: %d\n", entrenador_actual->estado);
	    	if(entrenador_actual->ocupado) printf("|ESTA OCUPADO\n");

	    	printf("|POKEMONES:\n");
	    	list_iterate(entrenador_actual->pokemones, _mostrar_pokemon);

	    	printf("|OBJETIVOS:\n");
	    	list_iterate(entrenador_actual->objetivo, _mostrar_pokemon);

	    	printf("|CICLOS UTILIZADOS:%d\n", entrenador_actual->ciclos);
			printf("********************\n");

		}

		printf("|OBJETIVO GLOBAL:\n");
		list_iterate(objetivo_global, _mostrar_pokemon);


		printf("|CICLOS TOTALES UTILIZADOS: %d\n", ciclos_totales);
		printf("|CAMBIOS DE CONTEXTO: %d\n", cambios_contexto);
		printf("********************\n");

		return;
}


char* get_metricas_string(){
	char* metricas = string_new();
	string_append_with_format(&metricas, "\n\nMETRICAS FINALES ALGORITMO %s\n---------------------\n",algoritmo.algoritmo_string);

	string_append(&metricas, "CANTIDAD DE CICLOS DE CPU POR ENTRENADOR:\n");

	for(int i =0; i < entrenadores->elements_count; i++){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		t_entrenador* entrenador = list_get(entrenadores, i);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
		string_append_with_format(&metricas, " * ENTRENADOR %d: %d\n",entrenador->id, entrenador->ciclos);
	}

	string_append_with_format(&metricas, "CANTIDAD DE CICLOS DE CPU TOTALES: %d\n", ciclos_totales);
	string_append_with_format(&metricas, "CANTIDAD DE CAMBIOS DE CONTEXTO: %d\n", cambios_contexto);
	string_append_with_format(&metricas, "CANTIDAD DE DEADLOCKS DETECTADOS: %d\n", total_deadlocks->elements_count);
	string_append_with_format(&metricas, "CANTIDAD DE DEADLOCKS RESUELTOS: %d\n", total_deadlock_resueltos);

	return metricas;
}

void hilo_exit(){
	for(int i =0; i < entrenadores->elements_count;i++){
		sem_wait(&s_entrenador_exit);
	}

	printf("Todos los entrenadores cumplieron sus objetivos\n");

	for(int i =0; i<entrenadores->elements_count;i++){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		t_entrenador* e = list_get(entrenadores, i);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
		ciclos_totales += e->ciclos;
	}

	show_estado();
	char* metricas = get_metricas_string();
	log_info(logger, metricas);
	printf("Terminando el programa...\n");

	return;
}


void resolver_deadlock(t_deadlock* deadlock){

	printf("tratando de resolver DL...   \n");

	int* e1 = list_get(deadlock->procesos_involucrados, 0);
	int* e2 = list_get(deadlock->procesos_involucrados, 1);


	t_entrenador* entrenadorDL1 = list_get(entrenadores_DL, *e1);
	t_entrenador* entrenadorDL2 = list_get(entrenadores_DL, *e2);

	//pthread_mutex_lock(&mutex_list_entrenadores);
	t_entrenador* entrenador1= list_get(entrenadores, entrenadorDL1->id);
	t_entrenador* entrenador2= list_get(entrenadores, entrenadorDL2->id);
	//pthread_mutex_unlock(&mutex_list_entrenadores);

	if((entrenador1->estado ==BLOCKED && entrenador1->ocupado) || entrenador1->estado == READY){
		printf("el entrenador %d ya está resolviendo su deadlock\n", entrenador1->id);
		return;
	}

	if((entrenador2->estado ==BLOCKED && entrenador2->ocupado) || entrenador2->estado == READY){
		printf("el entrenador %d ya está resolviendo su deadlock\n", entrenador1->id);
		return;
	}

	// entrenadorDL1 se va a mover a entrenadorDL2

	t_pokemon_team* pokemon_destino = pokemon_que_sirve(entrenadorDL1, entrenadorDL2);
	pokemon_destino->posicion = entrenador2->posicion;

	entrenador1->pokemon_destino=pokemon_destino;

	printf("El entrenador %d va a ir a la posicion del entrenador %d a intercambiar a %s\n",
			entrenador1->id, entrenador2->id, entrenador1->pokemon_destino->nombre);

	entrenador1->estado = READY;


	pthread_mutex_lock(&mutex_cola_ready);
	list_add(cola_ready, entrenador1);
	cambio_cola_ready = true;
	pthread_mutex_unlock(&mutex_cola_ready);


	free(e1);
	free(e2);
	entrenador_destroyer((void*)entrenadorDL1);
	entrenador_destroyer((void*)entrenadorDL2);

	sem_post(&s_cola_ready_con_items);

	return;
}

void resolver_deadlocks(t_list* deadlocks_encontrados){


	t_deadlock* deadlock = list_get(deadlocks_encontrados, 0);
	resolver_deadlock(deadlock);

	return;
}

bool esta_en_deadlock(void* entrenador){
	t_entrenador* e = (t_entrenador*)entrenador;
	return (e->estado == BLOCKED &&
			!e->ocupado &&
			!cumplio_objetivo(e) &&
			!puede_capturar_pokemones(e));
}




t_list* entrenadores_en_deadlock(){
	//pthread_mutex_lock(&mutex_list_entrenadores);
	t_list* en_DL = list_filter(entrenadores, esta_en_deadlock);
	//pthread_mutex_unlock(&mutex_list_entrenadores);

	t_list* entrenadores_en_DL = list_create();
	for(int i = 0; i < en_DL->elements_count; i++){
		t_entrenador* actual = list_get(en_DL, i);
		t_entrenador* nuevo = malloc(sizeof(t_entrenador));
		nuevo->id = actual->id;
		nuevo->pokemones = pokemones_de_mas(actual);//pokemones que no estan en objetivo;
		nuevo->objetivo = objetivos_pendientes(actual);//objetivo que no estan satisfechos;
		list_add(entrenadores_en_DL, nuevo);
	}

	return entrenadores_en_DL;

}


bool estan_unidos(t_entrenador* nodo_actual, t_entrenador* nodo_siguiente){
	//estan unidos si el nodo actual necesita algo que tiene el nodo_siguiente
	//nodo_actual.objetivo tiene los objetivos pendientes de satisfacer
	//nodo_siguiente.pokemones tiene los pokemones que tiene de mas
//	for(int i = 0; i < nodo_actual->objetivo->elements_count; i++){
//		t_pokemon_team* objetivo_actual = list_get(nodo_actual->objetivo, i);
//
//		int capturados_siguiente = get_cantidad_by_nombre_pokemon(objetivo_actual->nombre, nodo_siguiente->pokemones);
//		if (capturados_siguiente > 0) return true;
//
//	}

	if(pokemon_que_sirve(nodo_actual, nodo_siguiente) != NULL) return true;

	return false;

}

bool ordenar_DL(void* proceso1, void* proceso2){
	return *((int*)proceso1) < *((int*)proceso2);
}

bool deadlock_ya_detectado(t_list* deadlock_detectados, t_deadlock* deadlock){

	for(int i = 0; i < deadlock_detectados->elements_count; i++){
		t_deadlock* dl = list_get(deadlock_detectados, i);

		if(dl->procesos_involucrados->elements_count != deadlock->procesos_involucrados->elements_count) continue;

		for(int j=0; j < deadlock->procesos_involucrados->elements_count ; j++){
			int* proceso_involucrado = list_get(deadlock->procesos_involucrados, j);
			int* proceso_dl_involucrado = list_get(dl->procesos_involucrados, j);

			if((*proceso_involucrado) == (*proceso_dl_involucrado)){
				if(j == deadlock->procesos_involucrados->elements_count - 1){
					return true;
				}
				continue;
			} else {
				break;
			}
		}
	}

	return false;

}


void detectar_deadlocks(){
	bool puede_haber_deadlock =false;


	for(int i = 0; i< entrenadores->elements_count; i++){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		t_entrenador* e = list_get(entrenadores,i);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
		if(!esta_en_deadlock(e) && !(e->estado == EXIT)){
			continue; // no hay deadlock
		} else {
			puede_haber_deadlock =true;
		}
	}

	if(!puede_haber_deadlock) return;

	printf("puede haber deadlock, analizando...\n");
	//show_entrenadores();

	entrenadores_DL = entrenadores_en_deadlock();

	if(entrenadores_DL->elements_count <= 1) return;

	int filas = entrenadores_DL->elements_count;
	int columnas = filas;

	/* matriz dependiendo la cantidad de entrenadores en DL: si hay 2, será de 2x2
	 *
	 * ej: grafo de deadlock triple
	 *
	 *      A	 B
	 * A|   0  | 1	|
	 * B|	1  | 0	|
	 *
	 * 		PIK 	SQRT 	ONIX
	 * A |   1
	 * B |
	 * C |
	 *
	 *
	 * 	   A B C
	 *  A| 0 1 0
		B| 1 0 1
		C| 0 1 0
	 *
	 * 0: 2pik
	 *1: 1sqr, 1onix
	 *
	*/




	int matriz[filas][columnas];
	//lleno la matriz
	for(int i = 0; i < filas; i++){
		t_entrenador* e1 = list_get(entrenadores_DL, i);

		for(int j = 0; j < columnas; j++){
			t_entrenador* e2 = list_get(entrenadores_DL, j);
			if(i==j){
				matriz[i][j] = 0;
			} else {
				matriz[i][j] = estan_unidos(e1, e2);
			}
		}
	}

	for(int i = 0; i < filas; i++){
		for(int j = 0; j < columnas; j++){
			printf("%d ", matriz[i][j]);
		}
		 printf("\n");
	}










	t_list* deadlocks_detectados = list_create();

	for(int i = 0; i < filas; i++){
		for(int j = 0; j < columnas; j++){
			if(i==j) continue;
			if(matriz[i][j]){
				if(matriz[j][i]){
					t_deadlock* deadlock = malloc(sizeof(t_deadlock));
					deadlock->procesos_involucrados = list_create();
					int* p1 = malloc(sizeof(int));
					int* p2 = malloc(sizeof(int));
					*p1 = i;
					*p2 = j;

					list_add(deadlock->procesos_involucrados, p1);
					list_add(deadlock->procesos_involucrados, p2);
					list_sort(deadlock->procesos_involucrados, ordenar_DL);
					if(!deadlock_ya_detectado(deadlocks_detectados, deadlock))
						list_add(deadlocks_detectados, deadlock);

				} else { // hay que armar el caminito
					//recorro la fila 'j' en busca de otro 1
					t_deadlock* deadlock = malloc(sizeof(t_deadlock));
					deadlock->procesos_involucrados = list_create();

					int* p1 = malloc(sizeof(int));
					*p1 = i;

					int* p2 = malloc(sizeof(int));
					*p2 = j;

					for(int x = 0; x < columnas; x++){
						if(matriz[j][x]){
							for(int y = 0; y < columnas; y++){
								if(matriz[x][y]){
									if(y==i){
										int* p3 = malloc(sizeof(int));
										*p3 = x;
										list_add(deadlock->procesos_involucrados, p1);
										list_add(deadlock->procesos_involucrados, p2);
										list_add(deadlock->procesos_involucrados, p3);
										list_sort(deadlock->procesos_involucrados, ordenar_DL);
										break;
									}

								}
							}
							break;
						}
					}

					if(!deadlock_ya_detectado(deadlocks_detectados, deadlock))
						list_add(deadlocks_detectados, deadlock);

					break;
				}
			}
		}
	}

	for(int m = 0; m < deadlocks_detectados->elements_count; m++){
		t_deadlock* dl = list_get(deadlocks_detectados, m);


		if(!deadlock_ya_detectado(total_deadlocks, dl)){
			list_add(total_deadlocks, dl);
		}

		char* log_deadlock = string_new();

		string_append(&log_deadlock, "se detectó un deadlock entre el entrenador: ");

		for(int n=0; n < dl->procesos_involucrados->elements_count ; n++){
			int* proceso_involucrado = list_get(dl->procesos_involucrados, n);
			t_entrenador* entrenador_involucrado = list_get(entrenadores_DL, *proceso_involucrado);
			string_append_with_format(&log_deadlock, "%d ", entrenador_involucrado->id);
		}

		string_append(&log_deadlock, "\n");

		log_info(logger, log_deadlock);

		free(log_deadlock);
	}
	//show_entrenadores();


	resolver_deadlocks(deadlocks_detectados);

}



void ejecuta(t_entrenador* entrenador){

//	entrenador->estado = EXEC;
//	entrenador->ocupado= true;
//	printf("soy el entrenador %d y voy a EXEC a capturar a: %s\n", entrenador->id, entrenador->pokemon_destino->nombre);
//	printf("me voy a mover desde %d, %d\n", entrenador->posicion.X, entrenador->posicion.Y);
//	printf("me voy a mover hasta %d, %d\n", entrenador->pokemon_destino->posicion.X, entrenador->pokemon_destino->posicion.Y);

	int32_t posicion_final_X = entrenador->pokemon_destino->posicion.X - entrenador->posicion.X;
	int32_t posicion_final_Y = entrenador->pokemon_destino->posicion.Y - entrenador->posicion.Y;

	entrenador->ciclos++;
	entrenador->cantidad_ejecutada++;
	sleep(RETARDO_CICLO_CPU);
	if(posicion_final_X != 0){
		if(posicion_final_X < 0){
			entrenador->posicion = avanzar(entrenador->posicion, -1, 0);
		} else {
			entrenador->posicion = avanzar(entrenador->posicion, 1, 0);
		}

		printf("**Avanzo 1 paso en X**\n");

		return;

	}

	if(posicion_final_Y != 0){
		if(posicion_final_Y < 0){
			entrenador->posicion = avanzar(entrenador->posicion, 0, -1);
		} else {
			entrenador->posicion = avanzar(entrenador->posicion, 0, 1);
		}
		printf("**Avanzo 1 paso en Y**\n");

		return;

	}


}


void replanificar_entrenador(t_entrenador* entrenador){
	printf("VOY A REPLANIFICAR AL ENTRENADOR %d\n", entrenador->id);
	if(entrenador->pokemon_destino == NULL){
		entrenador->pokemon_destino = get_pokemon_necesario_mas_cercano(pokemones_ubicados, entrenador->posicion);
		entrenador->pokemon_destino->planificable=false;
	}

	if(entrenador->pokemon_destino == NULL){
		printf("No hay pokemones ubicados, me bloqueo\n");

	} else {
		printf("SU NUEVO POKEMON ES: %s\n", entrenador->pokemon_destino->nombre);
		entrenador->estado = READY;
		pthread_mutex_lock(&mutex_cola_ready);
		list_add(cola_ready, entrenador);
		cambio_cola_ready = true;
		pthread_mutex_unlock(&mutex_cola_ready);
		sem_post(&s_cola_ready_con_items);
	}
}

bool generar_y_enviar_catch(t_entrenador* entrenador){

	int32_t socket = conexion_broker();

	if(socket == 0){
		log_error(logger,"Error al conectar al Broker...");
		//logica de reintentar
		return false;
	}

	int32_t operacion = 0;
	int32_t id_mensaje = 0; // esto creo que habria que cambiarlo
	int32_t tamanio_estructura = 0;

	printf("Conectado al Broker para enviar CATCH");
	enviar_handshake(PROCESS_ID, socket);
	if(recv(socket, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
		if(operacion == ACK){ // Confirmacion de que la identificacion (handshake) fue recibida
			recv(socket, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
			recv(socket, &id_mensaje, sizeof(int32_t), MSG_WAITALL);
			printf("El broker me reconocio\n");

			char* pokemon = entrenador->pokemon_destino->nombre;
			char* posX = string_itoa(entrenador->pokemon_destino->posicion.X);
			char* posY = string_itoa(entrenador->pokemon_destino->posicion.Y);

			enviar_catch_pokemon(pokemon, posX, posY, string_itoa(0), socket);

			if(recv(socket, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){// Esperamos confirmacion de recepcion del mensaje
				if(operacion == ACK){
					recv(socket, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
					recv(socket, &id_mensaje, sizeof(int32_t), MSG_WAITALL); //recibo el paquete, aca llega el id_mensaje asignado por Broker

					t_respuesta* respuesta = malloc(sizeof(respuesta));
					respuesta->id_entrenador = entrenador->id;
					respuesta->id_respuesta = id_mensaje;
					list_add(mensajes_catch_esperando_respuesta, respuesta);
				}
			}
		}
	}

	return true;
}

void planificar_fifo(){

	printf("planificando FIFO...\n");


	// en fifo, el proximo entrenador es el que esté primero en la cola de ready
	pthread_mutex_lock(&mutex_cola_ready);
	t_entrenador* entrenador = list_remove(cola_ready, 0);
	pthread_mutex_unlock(&mutex_cola_ready);

	int cantidad_a_moverse = get_distancia_entre_puntos(entrenador->posicion, entrenador->pokemon_destino->posicion);
	printf("voy a ejecutar %d veces!\n", cantidad_a_moverse);
	entrenador->estado=EXEC;
	entrenador->ocupado=true;
	sem_post(entrenador->semaforo);

	return;
}

void planificar_rr(){

	printf("planificando RR...\n");

	// en RR, el proximo entrenador es el que esté primero en la cola de ready
	pthread_mutex_lock(&mutex_cola_ready);
	t_entrenador* entrenador = list_remove(cola_ready, 0);
	pthread_mutex_unlock(&mutex_cola_ready);

	printf("voy a planificar al entrenador %d\n", entrenador->id);

	int cantidad_a_moverse = get_distancia_entre_puntos(entrenador->posicion, entrenador->pokemon_destino->posicion);
	printf("voy a ejecutar %d veces!\n", cantidad_a_moverse);
	sem_post(entrenador->semaforo); // por cada paso que tiene que dar

	return;
}

int get_index_entrenador_estimacion_mas_corta(){

	// FORMULA DE ESTIMACION: ESTn+1 = alpha * TEn + (1-alpha) * ESTn

	//TE: t ejec rafaga actual
	//EST: estimado de la rafaga actual

	double estimacion_mas_corta = -1;
	t_entrenador* entrenador_mas_rapido=NULL;
	int indice = 0;

	// por archivo de cfg se obtiene el EST_siguiente inicial ya CALCULADO.
	// => si el est_anterior es 0, vamos a decir que es la primera vez que va a ejecutar y no hay que hacer el calculo.

	printf("en cola ready: %d\n", cola_ready->elements_count);

	for(int i = 0; i < cola_ready->elements_count; i++){

		pthread_mutex_lock(&mutex_cola_ready);
		t_entrenador* entrenador_actual = list_get(cola_ready, i);
		pthread_mutex_unlock(&mutex_cola_ready);



		double EST_siguiente = 0;

		printf("estimacion entrenador: %d\n", entrenador_actual->id);

		if(entrenador_actual->estimacion_anterior == 0){
			EST_siguiente = atoi(config_get_string_value(config, "ESTIMACION_INICIAL"));
		} else {

			int TE = get_distancia_entre_puntos(entrenador_actual->posicion, entrenador_actual->pokemon_destino->posicion);
			double EST = entrenador_actual->estimacion_anterior;

			printf("| Rafaga real: %d\n", TE);
			printf("| Estimacion anterior: %f\n", EST);


			EST_siguiente =  algoritmo.alpha * TE + (1-algoritmo.alpha) * EST;
			entrenador_actual->estimacion = EST_siguiente;
		}

		printf("| estimacion: %f\n", EST_siguiente);
		printf("_______________\n");

		if(estimacion_mas_corta == -1){
			estimacion_mas_corta = EST_siguiente;
			entrenador_mas_rapido = entrenador_actual;
		} else if(EST_siguiente < estimacion_mas_corta) {
			estimacion_mas_corta = EST_siguiente;
			entrenador_mas_rapido= entrenador_actual;
			indice = i;
		}
	}

	printf("el próximo va a ser el entrenador %d\n", entrenador_mas_rapido->id);

	//pthread_mutex_lock(&mutex_list_entrenadores);
	//t_entrenador* entrenador_actual = list_get(entrenadores, entrenador_mas_rapido->id);
	//pthread_mutex_unlock(&mutex_list_entrenadores);

	//entrenador_actual->estimacion_anterior = estimacion_mas_corta;



	return indice;
}

void planificar_sjfsd(){

	int i = get_index_entrenador_estimacion_mas_corta();
	pthread_mutex_lock(&mutex_cola_ready);
	t_entrenador* entrenador = list_remove(cola_ready, i);
	pthread_mutex_unlock(&mutex_cola_ready);


	sem_post(entrenador->semaforo);
	return;
}

void planificar_sjfcd(){
	planificar_sjfsd();
	return;
}

void planificar()
{

	switch(algoritmo.algoritmo_code){
	case FIFO:
		planificar_fifo();
		break;
	case RR:
		planificar_rr();
		break;
	case SJFSD:
		planificar_sjfsd();
		break;
	case SJFCD:
		planificar_sjfcd();
		break;
	default:
		return;
	}

}


void hilo_replanificar(){

	while(1){
		printf("soy el hilo que replanifica! esperando...\n");
		sem_wait(&s_replanificar);
		printf("llegó algo pa replanificar\n");
		t_entrenador* replanificar = malloc(sizeof(t_entrenador));
		for(int i = 0; i < entrenadores->elements_count; i++){
			//pthread_mutex_lock(&mutex_list_entrenadores);
			t_entrenador* entrenador = list_get(entrenadores, i);
			//pthread_mutex_unlock(&mutex_list_entrenadores);
			if(puede_capturar_pokemones(entrenador) && entrenador->estado == BLOCKED && !entrenador->ocupado && entrenador->pokemon_destino == NULL){
				entrenador_destroyer(replanificar);
				replanificar = entrenador;
			}
		}
		replanificar_entrenador(replanificar);
	}

}

void hilo_replanificar_sjfcd(){

	while(1){
		printf("soy el hilo que replanifica sjfcd! esperando que llegue algo a ready...\n");
		sem_wait(&s_replanificar_sjfcd);

		printf("llegó algo pa replanificar y el algoritmo es SJF-CD\n");
		cambio_cola_ready = false;

		//t_entrenador* replanificar = malloc(sizeof(t_entrenador));

		//t_entrenador* entrenador_en_exec =

		printf("el proceso ejecutando es :%d\n", proceso_ejecutando);
		t_entrenador* entrenador_ejecutando = list_get(entrenadores, proceso_ejecutando);

		int i = get_index_entrenador_estimacion_mas_corta();

		pthread_mutex_lock(&mutex_cola_ready);
		t_entrenador* entrenador = list_get(cola_ready, i);
		pthread_mutex_unlock(&mutex_cola_ready);

		printf("entrenador ejecutando: %d y su estimacion es de: %d - %d = %d\n",
				proceso_ejecutando, entrenador_ejecutando->estimacion,
				entrenador_ejecutando->cantidad_ejecutada,
				entrenador_ejecutando->estimacion - entrenador_ejecutando->cantidad_ejecutada);
		printf("entrenador en cola con menor rafaga: %d y su estimacion es de: %d\n",
				entrenador->id, entrenador->estimacion);


		if(entrenador_ejecutando->estimacion - entrenador_ejecutando->cantidad_ejecutada <= entrenador->estimacion){
			printf("sigue ejecutando el entrenador actual\n");
			entrenador_ejecutando->estado=EXEC;
			sem_post(entrenador_ejecutando->semaforo);
		} else {
			entrenador_ejecutando->estado = READY;
			entrenador->estado = EXEC;
			sem_post(entrenador_ejecutando->semaforo);
			sem_post(entrenador->semaforo);
		}
	}
}

void hilo_detectar_deadlock(){
	while(1){
		printf("soy el hilo que detecta deadlocks! esperando...\n");
		for(int i = 0; i < entrenadores->elements_count; i++){
			sem_wait(&s_detectar_deadlock);
		}

		printf("puede que haya deadlock..\n");
		show_estado();
		detectar_deadlocks();
	}
}

void capturar(t_entrenador* entrenador){
	if(!generar_y_enviar_catch(entrenador)){//esto quiere decir que no se pudo conectar al broker

		list_add(entrenador->pokemones, entrenador->pokemon_destino);
		entrenador->pokemon_destino = NULL;
		entrenador->cantidad_ejecutada = 0;
		entrenador->estimacion_anterior = entrenador->estimacion;
		entrenador->estimacion = 0;
		printf("POKEMON CAPTURADO!\n");
		entrenador->ocupado = false;

		/*Esta logica tendría que estar dentro del while ? SI*/
		if(!cumplio_objetivo(entrenador)){
			printf("el entrenador %d aún no cumplio sus objetivos, pasandolo a BLOCKED\n", entrenador->id);

			if(puede_capturar_pokemones(entrenador)){
				printf("el entrenador puede capturar mas pokemones, llamando al replanificador...\n");
				sem_post(&s_replanificar);//replanificar_entrenador(entrenador);
			} else {
				printf("el entrenador no puede capturar mas pokemones, queda bloqueado hasta intercambiar\n");
				sem_post(&s_detectar_deadlock);
			}
		}
	} else {
		printf("ESPERANDO A VER SI LO CAPTURÉ!\n");
		//acá creo que iría otro sem_wait(entrenador->semaforo) hasta que lo desbloquee el recibidor de mensajes caught
		// para ver si lo capturó o no y en base a eso repreguntar.
	}
}

t_entrenador* get_entrenador_by_posicion(t_posicion posicion, int id){

	for(int i = 0; i < entrenadores->elements_count; i++){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		t_entrenador* entrenador_actual = list_get(entrenadores, i);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
		if(entrenador_actual->id == id) continue;

		if(entrenador_actual->posicion.X == posicion.X && entrenador_actual->posicion.Y == posicion.Y){
			return entrenador_actual;
		}

	}

	return NULL;
}


void intercambio(t_entrenador* entrenador){
	printf("Soy el entrenador %d y voy a intercambiar con el que esté en esta posicion\n", entrenador->id);
	//puedo tener mas de uno dependiendo cuantos participan en el deadlock.
	show_estado();
	t_entrenador* entrenador_en_posicion = get_entrenador_by_posicion(entrenador->posicion, entrenador->id);
	t_pokemon_team* pokemon_queme_sirve = entrenador->pokemon_destino; // es el entrenador->pokemon_destino
	t_pokemon_team* pokemon_quele_sirve = pokemon_que_sirve_intercambio(entrenador_en_posicion, entrenador);

	if(pokemon_quele_sirve != NULL){
		printf("el pokemon que le sirve al otro es %s\n", pokemon_quele_sirve->nombre);
	} else {

		printf("Tiene algo que me sirve pero yo no tengo nada que le sirva\n");
		printf("Le doy un pokemon random que me sobre\n");

		t_list* pokemons_de_mas = pokemones_de_mas(entrenador);

		if(pokemons_de_mas->elements_count == 0){
			printf("no le puedo dar nada porque no me sobra nada\n");
		} else {
			pokemon_quele_sirve = list_get(pokemons_de_mas, 0);
			printf("le voy a dar el pokemon %s\n", pokemon_quele_sirve->nombre);

		}

	}

	entrenador_en_posicion->pokemon_destino = pokemon_quele_sirve;

	if(entrenador_en_posicion != NULL){
		printf("llegué al entrenador que tiene el pokemon que me interesa\n");
		entrenador_en_posicion->ocupado=true;

		if(pokemon_queme_sirve != NULL && pokemon_quele_sirve != NULL){

			printf("ambos entrenadores tienen algo que le interesa al otro\n");

			printf("intercambiando primer pokemon:\n");

			for(int i =0; i < 5; i++){
				printf("%d ciclo de cpu\n", i + 1);
				//	pthread_mutex_lock(&mutex_ciclos_totales);
				ciclos_totales++;
				//	pthread_mutex_unlock(&mutex_ciclos_totales);
				sleep(RETARDO_CICLO_CPU);
			}

			list_add(entrenador->pokemones, pokemon_queme_sirve);
			remove_pokemon_by_nombre(pokemon_queme_sirve->nombre, entrenador_en_posicion->pokemones);//remuevo de la lista de pokemones el que le di al entrenador
			entrenador->pokemon_destino=NULL;
			list_add(entrenador_en_posicion->pokemones, entrenador_en_posicion->pokemon_destino);
			remove_pokemon_by_nombre(pokemon_quele_sirve->nombre, entrenador->pokemones);
			entrenador_en_posicion->pokemon_destino=NULL;


			printf("INTERCAMBIO FINALIZADO!\n");


			if(!esta_en_deadlock((void*)entrenador) || !esta_en_deadlock((void*)entrenador_en_posicion) ){
				total_deadlock_resueltos++;
			}


			entrenador->estado = BLOCKED;
			entrenador->ocupado = false;
			entrenador_en_posicion->estado = BLOCKED;
			entrenador_en_posicion->ocupado = false;
			sem_post(entrenador->semaforo);
			sem_post(entrenador_en_posicion->semaforo);
		}


	} else {
		printf("**hubo un error porque no hay otro entrenador en esta posicion**\n");
	}

	detectar_deadlocks();
}


void capturar_pokemon(t_entrenador* entrenador){

	printf("LLEGUÉ A DESTINO!! X: %d, Y: %d, \n", entrenador->posicion.X, entrenador->posicion.Y);
	entrenador->estado=BLOCKED;
	entrenador->ocupado=true;
	sem_post(&s_procesos_en_exec); // salgo de exec

	if(puede_capturar_pokemones(entrenador)){
		capturar(entrenador);
	} else {
		intercambio(entrenador);//intercambio
	}
}

void ejecutar_fifo(t_entrenador* entrenador){

	while(entrenador->posicion.X != entrenador->pokemon_destino->posicion.X ||
			entrenador->posicion.Y != entrenador->pokemon_destino->posicion.Y){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		ejecuta(entrenador);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
	}

	printf("posicion actual: %d, %d\n", entrenador->posicion.X, entrenador->posicion.Y);

	capturar_pokemon(entrenador);

}

void ejecutar_rr(t_entrenador* entrenador){

	int cantidad_a_moverse = get_distancia_entre_puntos(entrenador->posicion, entrenador->pokemon_destino->posicion);
	int movimientos = cantidad_a_moverse < algoritmo.quantum ? cantidad_a_moverse : algoritmo.quantum;

	for(int i = 0; i < movimientos; i++){
		//pthread_mutex_lock(&mutex_list_entrenadores);
		ejecuta(entrenador);
		//pthread_mutex_unlock(&mutex_list_entrenadores);
	}

	printf("posicion actual: %d, %d\n", entrenador->posicion.X, entrenador->posicion.Y);

	cantidad_a_moverse = get_distancia_entre_puntos(entrenador->posicion, entrenador->pokemon_destino->posicion);
	if(cantidad_a_moverse > 0 ){
		printf("se terminó el quantum y todavía falta que se mueva, lo mando a la cola de READY\n");
		entrenador->estado = READY;

		pthread_mutex_lock(&mutex_cola_ready);
		list_add(cola_ready, entrenador);
		cambio_cola_ready = true;
		pthread_mutex_unlock(&mutex_cola_ready);

		sem_post(&s_cola_ready_con_items);
		sem_post(&s_procesos_en_exec);
	} else {
		capturar_pokemon(entrenador);
	}
}

void ejecutar_sjfsd(t_entrenador* entrenador){
	return ejecutar_fifo(entrenador);
}

void ejecutar_sjfcd(t_entrenador* entrenador){

	while(entrenador->posicion.X != entrenador->pokemon_destino->posicion.X ||
			entrenador->posicion.Y != entrenador->pokemon_destino->posicion.Y){

		if(cambio_cola_ready && cola_ready->elements_count > 0){
			printf("llegó algo a la cola de ready mientras ejecutaba");
			sem_post(&s_replanificar_sjfcd);
			sem_wait(entrenador->semaforo); // mme bloqueo hasta que me saque el planificador
			printf("me desbloqueó el planificador!!! \n");
			printf("soy el entrenador %d y mi estado es: %d", entrenador->id, entrenador->estado);
			if(entrenador->estado == READY){
				return;
			} else if (entrenador->estado == EXEC){
				ejecuta(entrenador);

			}

		} else {
			ejecuta(entrenador);
			printf("posicion actual: %d, %d\n", entrenador->posicion.X, entrenador->posicion.Y);
		}
	}


	capturar_pokemon(entrenador);


	return;
}


void ejecutar_algoritmo(t_entrenador* entrenador)
{

	switch(algoritmo.algoritmo_code){
	case FIFO:
		ejecutar_fifo(entrenador);
		break;
	case RR:
		ejecutar_rr(entrenador);
		break;
	case SJFSD:
		ejecutar_sjfsd(entrenador);
		break;
	case SJFCD:
		ejecutar_sjfcd(entrenador);
		break;
	default:
		return;
	}

}



void hilo_enviar_get(int i){
	int32_t socket = conexion_broker();

	printf(logger, "hilo creado para enviar get...\n");

	if(socket == 0){
		log_error(logger,"Error al conectar al Broker para enviar GET...");
		return;
	}

	log_debug(logger,"Conectado al Broker para enviar GET");

	int32_t operacion = 0;
	int32_t id_mensaje = 0;
	int32_t tamanio_estructura = 0;
	t_pokemon_team* pokemon = list_get(objetivo_global, i);

	enviar_handshake(PROCESS_ID, socket);

	if(recv(socket, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
		if(operacion == ACK){
			recv(socket, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
			recv(socket, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

			enviar_get_pokemon(pokemon->nombre, "0", socket);
			if(recv(socket, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
				if(operacion == ACK){
					recv(socket, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
					recv(socket, &id_mensaje, sizeof(int32_t), MSG_WAITALL);
				}
			}
		}
	}

	liberar_conexion(socket);
}

void generar_y_enviar_get(){
	for(int i = 0; i < objetivo_global->elements_count; i++){

		/* HILO PLANIFICADOR */
		pthread_t p_enviar_get;
		pthread_create(&p_enviar_get, NULL, (void*)hilo_enviar_get, (void*)i);
		pthread_detach(p_enviar_get);

	}

	return;
}

void show_cola_ready(){
	for(int i = 0; i < cola_ready->elements_count; i++){
		pthread_mutex_lock(&mutex_cola_ready);
		t_entrenador* entrenador_actual = list_get(cola_ready, i);
		pthread_mutex_unlock(&mutex_cola_ready);

		printf("posicion en la lista: %d\n", i);
		printf("posicion actual: X:%d, Y:%d\n", entrenador_actual->posicion.X, entrenador_actual->posicion.Y);
		printf("posicion destino: X:%d, Y:%d\n", entrenador_actual->pokemon_destino->posicion.X, entrenador_actual->pokemon_destino->posicion.Y);
	}
}

void hilo_planificador(){

	while(1){ // while no haya otro en EXEC (semaforo?)
		//printf("esperando que llegue algo a la cola de ready....\n");
		sem_wait(&s_cola_ready_con_items);//inicializado en 0

		printf("llegó algo a la cola de READY\n");
		sem_wait(&s_procesos_en_exec); //inicializado en 1, o sea que solo puede haber uno a la vez;

		//printf("nadie en EXEC, voy a planificar tranquilo..\n");
		planificar();
	}
}




void ubicar_pokemones_localized(t_Localized* pokemones_a_ubicar){
	for(int i = 0; i < pokemones_a_ubicar->listaPosiciones->elements_count; i++){

		t_posicion* posicion = list_get(pokemones_a_ubicar->listaPosiciones, i);
		t_pokemon_team* pokemon_ubicado = get_pokemon_team(pokemones_a_ubicar->pokemon.nombre, *posicion);

		list_add(pokemones_ubicados, pokemon_ubicado);
	}
}

void recibidor_mensajes_localized(void* args){
	t_args_mensajes* arg = (t_args_mensajes*)args;
	t_Localized* mensaje = (t_Localized*)arg->mensaje;
	t_respuesta* respuesta = arg->respuesta;

	printf("Se recibió un LOCALIZED %s (id: %d)\n", mensaje->pokemon.nombre, respuesta->id_respuesta);

	if(localized_valido(mensaje, respuesta->id_respuesta, mensajes_get_esperando_respuesta, pokemones_recibidos, objetivo_global)){
		printf("A WILD %s WAS LOCALIZED!!!!\n", mensaje->pokemon.nombre);
		list_add(pokemones_recibidos, mensaje->pokemon.nombre);


		//tengo que planificar tantas posiciones como pokemon necesite.
		//Ej, si me llegan 3 posiciones y necesito 2, solo planifico 2 pero las otras las guardo en memoria
		//Si necesito 2 posiciones y llega 1, planifico 1.

		int cantidad_pokemones_global = get_cantidad_by_nombre_pokemon(mensaje->pokemon.nombre, objetivo_global);

		// me quedo con la menor de las cantidades
		int cantidad_a_planificar = 0;
		if(mensaje->listaPosiciones->elements_count <= cantidad_pokemones_global ){
			cantidad_a_planificar = mensaje->listaPosiciones->elements_count;
		} else {
			cantidad_a_planificar = cantidad_pokemones_global;
		}

		for(int i = 0; i < cantidad_a_planificar; i++){
			t_posicion* posicion = list_get(mensaje->listaPosiciones, i);

			t_entrenador* entrenador_mas_cercano = get_entrenador_planificable_mas_cercano(entrenadores, *posicion);

			if(entrenador_mas_cercano != NULL){

				//Si hay entrenadores planificables, planifico esa posicion que siempre va a ser la 0
				list_remove(mensaje->listaPosiciones, 0);

				t_pokemon_team* pokemon_destino = get_pokemon_team(mensaje->pokemon.nombre, *posicion);

				entrenador_mas_cercano->estado = READY;
				entrenador_mas_cercano->pokemon_destino = pokemon_destino;

				pthread_mutex_lock(&mutex_cola_ready);
				list_add(cola_ready, entrenador_mas_cercano);
				cambio_cola_ready = true;
				pthread_mutex_unlock(&mutex_cola_ready);

				sem_post(&s_cola_ready_con_items);
				sem_post(entrenador_mas_cercano->semaforo);
			} else {
				ubicar_pokemones_localized(mensaje);
			}
		}
	}

}

void recibidor_mensajes_appeared(void* args){
	t_args_mensajes* arg = (t_args_mensajes*)args;
	t_Appeared* mensaje = (t_Appeared*)arg->mensaje;


	//printf("se recibió un mensaje APPEARED: %s\n", mensaje->pokemon.nombre);

	if(appeared_valido(mensaje, entrenadores, objetivo_global)){
		printf("A WILD %s APPEARED!!!!\n", mensaje->pokemon.nombre);

		//pthread_mutex_lock(&mutex_pokemones_ubicados);
		list_add(pokemones_recibidos, mensaje->pokemon.nombre);
		//pthread_mutex_unlock(&mutex_pokemones_ubicados);

		t_entrenador* entrenador_mas_cercano = get_entrenador_planificable_mas_cercano(entrenadores, mensaje->posicion);

		if(entrenador_mas_cercano != NULL){
			entrenador_mas_cercano->estado = READY;

			t_pokemon_team* pokemon_destino = get_pokemon_team(mensaje->pokemon.nombre, mensaje->posicion);
			pokemon_destino->planificable = false;
			entrenador_mas_cercano->pokemon_destino = pokemon_destino;
			entrenador_mas_cercano->ocupado = true;

			pthread_mutex_lock(&mutex_cola_ready);
			list_add(cola_ready, entrenador_mas_cercano);
			cambio_cola_ready = true;
			pthread_mutex_unlock(&mutex_cola_ready);


			printf("el entrenador %d fue agregado a la cola READY\n", entrenador_mas_cercano->id);
			sem_post(&s_cola_ready_con_items);
			//sem_post(entrenador_mas_cercano->semaforo);
		} else {
			t_pokemon_team* pokemon_ubicado = get_pokemon_team(mensaje->pokemon.nombre, mensaje->posicion);
			pokemon_ubicado->planificable = true;
			//pthread_mutex_lock(&mutex_pokemones_ubicados);
			list_add(pokemones_ubicados, pokemon_ubicado);
			//pthread_mutex_unlock(&mutex_pokemones_ubicados);
		}
	}

	free(mensaje);

	pthread_exit(NULL);

}

void recibidor_mensajes_caught(void* args){
	t_args_mensajes* arg = (t_args_mensajes*)args;
	t_Caught* mensaje = (t_Caught*)arg->mensaje;
	t_respuesta* respuesta = arg->respuesta;

	//pthread_mutex_lock(&mutex_list_entrenadores);
	t_entrenador* entrenador = list_get(entrenadores, respuesta->id_entrenador);
	//pthread_mutex_unlock(&mutex_list_entrenadores);

	printf("TRYING TO CATCH A %s \nTRAINER %d USED A ULTRA BALL\n",entrenador->pokemon_destino->nombre, entrenador->id);
	printf("3...\n2... \n1... \n");
	sleep(3);

	if(mensaje->fueAtrapado){
		printf("GOTCHA! %s WAS CAUGHT!\n", entrenador->pokemon_destino->nombre);
		list_add(entrenador->pokemones, entrenador->pokemon_destino);

		if(cumplio_objetivo(entrenador)){
			entrenador->estado = EXIT;
		} else {
			entrenador->estado = READY;
			entrenador->ocupado = false;
		}
	} else {
		printf("OH, NO! THE POKEMON %s BROKE FREE\n", entrenador->pokemon_destino->nombre);
		entrenador->estado = READY;
		entrenador->ocupado = false;
		//cuando falla, tengo que ir a capturar otro pokemon de esa especie.
		//mismo que antes, comparo entre todos los entrenadores disponibles y todas las posiciones de ese pokemon
		//o voy por la primer posicion que encuentre?
	}

	entrenador->estado = BLOCKED;

	return;
}

void hilo_recibidor_mensajes_gameboy(){
	printf("Esperando que el Game Boy se conecte...\n");
	while(1){
		int32_t socket_cliente = (int32_t)recibir_cliente(socket_escucha_team);
		if(socket_cliente != -1){

			int32_t codigo_operacion = 0;
			int32_t tamanio_estructura = 0;
			int32_t id_mensaje = 0;

			if(recv(socket_cliente, &codigo_operacion, sizeof(int32_t), MSG_WAITALL) == -1)
					codigo_operacion = -1;
			recv(socket_cliente, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
			recv(socket_cliente, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

			switch(codigo_operacion){
				case APPEARED_POKEMON:
					printf("Llego un mensaje APPEARED desde el GAME BOY\n");
					t_Appeared* mensaje_appeared = deserializar_paquete_appeared(&socket_cliente);

//					printf("Llego un mensaje Appeared Pokemon con los siguientes datos: %d  %s  %d  %d\n",
//							mensaje_appeared->pokemon.size_Nombre,
//							mensaje_appeared->pokemon.nombre,
//							mensaje_appeared->posicion.X,
//							mensaje_appeared->posicion.Y);

					t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
					args->mensaje = mensaje_appeared;
					args->respuesta = NULL;

					pthread_t p_generador_mensajes_appeared;
					pthread_create(&p_generador_mensajes_appeared, NULL, (void*)recibidor_mensajes_appeared, (void*)args);
					pthread_detach(p_generador_mensajes_appeared);

					break;

				case LOCALIZED_POKEMON:
					printf("Recibí un mensaje LOCALIZED desde el GAME BOY\n");
					t_Localized* mensaje_localized = deserializar_paquete_localized(&socket_cliente);

					printf("Llego un mensaje Localized Pokemon con los siguientes datos: %s\n",
							mensaje_localized->pokemon.nombre);

//					for(int i = 0; i < mensaje_localized->listaPosiciones->elements_count; i++){
//						t_posicion* posicion = list_get(mensaje_localized->listaPosiciones, i);
//						printf("Pos X: %d\nPos Y: %d\n", posicion->X, posicion->Y);
//					}

					t_respuesta* respuesta_get = get_respuesta(id_mensaje, mensajes_get_esperando_respuesta);

					if(respuesta_get != NULL){
						t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
						args->mensaje = mensaje_localized;
						args->respuesta = respuesta_get;



						 pthread_t p_generador_mensajes_localized;
						 pthread_create(&p_generador_mensajes_localized, NULL, (void*)recibidor_mensajes_localized, (void*)args);
					}

					break;

				case CAUGHT_POKEMON:
					printf("Recibí un mensaje CAUGHT desde el GAME BOY\n");
					t_Caught* mensaje_caught = deserializar_paquete_caught(&socket_cliente);

					printf("Llego un mensaje Caught Pokemon con los siguientes datos: %d\n",
												mensaje_caught->fueAtrapado);

					t_respuesta* respuesta_catch = get_respuesta(id_mensaje, mensajes_catch_esperando_respuesta);

					//debería ser un hilo
					if(respuesta_catch != NULL){

						t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
						args->mensaje = mensaje_localized;
						args->respuesta = respuesta_catch;

						pthread_t p_generador_mensajes_caught;
						pthread_create(&p_generador_mensajes_caught, NULL, (void*)recibidor_mensajes_caught, (void*)args);

					}

					break;
				default:
					printf("no me interesa el mensaje");
					return;
			}
		}
	}
}

char* get_config_path(char* entrenador){
	char* cfg_path = string_new();
	string_append(&cfg_path, "/home/utnso/workspace/tp-2020-1c-5rona/team/config/");
	string_append(&cfg_path, entrenador);
	string_append(&cfg_path, ".config");

	return cfg_path;
}

void hilo_suscriptor(op_code* code){
	int32_t operacion=0;
	int32_t tamanio_estructura = 0;
	int32_t id_mensaje=0;
	int32_t socket_broker = crear_conexion(IP_BROKER,PUERTO_BROKER);
	bool fin = false;

	while(1){
		if(socket_broker != 0){
			enviar_handshake(PROCESS_ID, socket_broker);
			if(recv(socket_broker, &operacion, sizeof(int32_t), MSG_WAITALL) > 0){
				if(operacion == ACK){
					recv(socket_broker, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
					recv(socket_broker, &id_mensaje, sizeof(int32_t), MSG_WAITALL);


					enviar_suscripcion(*code, socket_broker);
					if(recv(socket_broker, &operacion, sizeof(int32_t), MSG_WAITALL) >0){
						if(operacion == ACK){
							recv(socket_broker, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
							recv(socket_broker, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

							while(fin == false){
								if(recv(socket_broker, &operacion, sizeof(int32_t), MSG_WAITALL) >0){
									recv(socket_broker, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
									recv(socket_broker, &id_mensaje, sizeof(int32_t), MSG_WAITALL);
									t_Appeared* app;
									t_Localized* loc;
									t_Caught* caught;

									pthread_t p_generador_mensajes;

									switch(operacion){
										case APPEARED_POKEMON:
											app = deserializar_paquete_appeared(&socket_broker);
											enviar_ACK(0, socket_broker);
											log_info(logger, "ID de Mensaje recibido: %d", id_mensaje);
											pthread_create(&p_generador_mensajes, NULL, (void*)recibidor_mensajes_appeared, (void*)app);
											pthread_detach(p_generador_mensajes);
											break;
										case LOCALIZED_POKEMON:
											loc = deserializar_paquete_localized(&socket_broker);
											enviar_ACK(0, socket_broker);
											log_info(logger, "ID de Mensaje recibido: %d", id_mensaje);
											pthread_create(&p_generador_mensajes, NULL, (void*)recibidor_mensajes_localized, (void*)loc);
											pthread_detach(p_generador_mensajes);
											break;
										case CAUGHT_POKEMON:
											caught = deserializar_paquete_caught(&socket_broker);
											enviar_ACK(0, socket_broker);
											log_info(logger, "ID de Mensaje recibido: %d", id_mensaje);
											pthread_create(&p_generador_mensajes, NULL, (void*)recibidor_mensajes_caught, (void*)caught);
											pthread_detach(p_generador_mensajes);
											break;
										default:
											printf("No me interesa el mensaje");
											return;
									}
									} else {
										log_info(logger, "Se cayo la conexion");
										liberar_conexion(socket_broker);
										socket_broker = 0;
										fin = true;
										}
							}

						} else {
							log_error(logger, "Fallo la suscripcion, respondieron algo que no era un ACK");
							liberar_conexion(socket_broker);
							socket_broker = 0;
						}
					} else {
						log_error(logger, "Se cayo la conexion con Broker");
						liberar_conexion(socket_broker);
						socket_broker = 0;
					}
				} else {
					log_error(logger, "Fallo el ACK del handshake con Broker");
					liberar_conexion(socket_broker);
					socket_broker = 0;
				}

			}

		} else {
			log_info(logger, "Reintentando conexion cola...");
			socket_broker = reconectar(socket_broker);
		}
	}
}
/*
void hilo_suscribirse_appeared(void* l_entrenadores){

	int32_t socket_suscripcion = conexion_broker();
	if(socket_suscripcion == 0){
		log_error(logger,"Error al conectar al Broker para suscribirse a Appeared...");
		//logica para reintentar
		return;
	}

	log_info(logger,"Conectado al Broker para suscribirse a APPEARED...\n");

	int32_t operacion = 0;
	int32_t id_mensaje = 0;
	int32_t tamanio_estructura = 0;

	if(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
		printf("codigo de op respuesta: %d\n", operacion);
		if(operacion == ACK){
			printf("el broker me dio el OK\n");
			recv(socket_suscripcion, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
			recv(socket_suscripcion, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

			//voy a ir recibiendo mensajes APPEARED
			printf("Esperando que llegue algún APPEARED de la cola de suscripcion...\n");
			while(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
				recv(socket_suscripcion, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
				recv(socket_suscripcion, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

				enviar_handshake(PROCESS_ID, socket_suscripcion);

				if(operacion == APPEARED_POKEMON){
					printf("Llego un mensaje APPEARED desde el GAME BOY\n");
					t_Appeared* mensaje_appeared = deserializar_paquete_appeared(&socket_suscripcion);

					printf("Llego un mensaje Appeared Pokemon con los siguientes datos: %d  %s  %d  %d\n",
							mensaje_appeared->pokemon.size_Nombre,
							mensaje_appeared->pokemon.nombre,
							mensaje_appeared->posicion.X,
							mensaje_appeared->posicion.Y);

					t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
					args->mensaje = mensaje_appeared;
					args->respuesta = NULL;

					pthread_t p_generador_mensajes_appeared;
					pthread_create(&p_generador_mensajes_appeared, NULL, (void*)recibidor_mensajes_appeared, (void*)args);
				}
			}
		}
	} else {
		log_error(logger, "el hilo murió\n");
		return;
	}

	return;
}

void hilo_suscribirse_caught(void* l_entrenadores){

	int32_t socket_suscripcion = conexion_broker();

	if(socket_suscripcion == 0){
		log_error(logger,"Error al conectar al Broker para suscribirse a CAUGHT...");
		//logica para reintentar (signal a un hilo?)
		return;
	}

	log_info(logger,"Conectado al Broker para suscribirse a CAUGHT...\n");

	int32_t operacion = 0;
	int32_t id_mensaje = 0;
	int32_t tamanio_estructura = 0;

	if(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
		printf("codigo de op respuesta: %d\n", operacion);
		if(operacion == ACK){
			printf("el broker me dio el OK\n");
			recv(socket_suscripcion, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
			recv(socket_suscripcion, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

			//voy a ir recibiendo mensajes CAUGHT
			printf("Esperando que llegue algún CAUGHT de la cola de suscripcion...\n");
			while(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
				recv(socket_suscripcion, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
				recv(socket_suscripcion, &id_mensaje, sizeof(int32_t), MSG_WAITALL);

				enviar_handshake(PROCESS_ID, socket_suscripcion);

				if(operacion == CAUGHT_POKEMON){
					printf("Recibí un mensaje CAUGHT\n");
					t_Caught* mensaje_caught = deserializar_paquete_caught(&socket_suscripcion);

					printf("Llego un mensaje Caught Pokemon con los siguientes datos: %d\n",
												mensaje_caught->fueAtrapado);

					t_respuesta* respuesta_catch = get_respuesta(id_mensaje, mensajes_catch_esperando_respuesta);

					//debería ser un hilo
					if(respuesta_catch != NULL){

					t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
					args->mensaje = mensaje_caught;
					args->respuesta = respuesta_catch;

					pthread_t p_generador_mensajes_caught;
					pthread_create(&p_generador_mensajes_caught, NULL, (void*)recibidor_mensajes_caught, (void*)args);


						//recibidor_mensajes_caught(l_entrenadores, mensaje_caught); // cambiar nombre a la funcion
					}
				}
			}
		}
	} else {
		log_error(logger, "el hilo murió\n");
		return;
	}

	return;
}

void hilo_suscribirse_localized(void* l_entrenadores){

	int32_t socket_suscripcion = conexion_broker();

	if(socket_suscripcion == 0){
		log_error(logger,"Error al conectar al Broker para suscribirse a Appeared...");
		//logica para reintentar
		return;
	}

	log_info(logger,"Conectado al Broker para suscribirse a LOCALIZED...\n");


	int32_t operacion = 0;
	int32_t id_mensaje = 0;
	int32_t tamanio_estructura = 0;

	if(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
		printf("codigo de op respuesta: %d\n", operacion);
			if(operacion == ACK){
				printf("el broker me dio el OK\n");
				recv(socket_suscripcion, &tamanio_estructura, sizeof(int32_t), MSG_WAITALL);
				recv(socket_suscripcion, &id_mensaje, sizeof(int32_t), MSG_WAITALL);
				//voy a ir recibiendo mensajes LOCALIZED
				printf("Esperando que llegue algún LOCALIZED de la cola de suscripcion...");
				while(recv(socket_suscripcion, &operacion, sizeof(int32_t), MSG_WAITALL) != -1){
					printf("Recibí un mensaje LOCALIZED\n");
					t_Localized* mensaje_localized = deserializar_paquete_localized(&socket_suscripcion);

					printf("Llego un mensaje LOCALIZED Pokemon con los siguientes datos: %s\n",
							mensaje_localized->pokemon.nombre);

					for(int i = 0; i < mensaje_localized->listaPosiciones->elements_count; i++){
						t_posicion* posicion = list_get(mensaje_localized->listaPosiciones, i);
						printf("Pos X: %d\nPos Y: %d\n", posicion->X, posicion->Y);
					}

					t_respuesta* respuesta_get = get_respuesta(id_mensaje, mensajes_get_esperando_respuesta);

					t_args_mensajes* args = malloc(sizeof(t_args_mensajes));
					args->mensaje = mensaje_localized;
					args->respuesta = respuesta_get;

					pthread_t p_generador_mensajes_localized;
					pthread_create(&p_generador_mensajes_localized, NULL, (void*)recibidor_mensajes_localized, (void*)args);

				}
		}
	}else {
		log_error(logger, "el hilo murió\n");
		return;
	}

	return;
}
*/
int inicializar_team(char* entrenador){

	cola_ready = list_create();
	objetivo_global = list_create();
	pokemones_recibidos = list_create();
	pokemones_ubicados = list_create();
	mensajes_get_esperando_respuesta = list_create();
	mensajes_catch_esperando_respuesta = list_create();
	entrenadores_DL = list_create();
	entrenadores = list_create();
	total_deadlocks = list_create();
	sem_init(&s_cola_ready_con_items, 0, 0);
	sem_init(&s_posiciones_a_mover, 0, 0);
	sem_init(&s_procesos_en_exec, 0, 1);
	sem_init(&s_control_planificador_rr, 0, 0);
	sem_init(&s_detectar_deadlock, 0, 0);
	sem_init(&s_replanificar, 0, 0);
	sem_init(&s_entrenador_exit, 0, 0);
	sem_init(&s_replanificar_sjfcd, 0,0);
	pthread_mutex_init(&mutex_ciclos_totales, NULL);
	pthread_mutex_init(&mutex_deadlocks_totales, NULL);
	pthread_mutex_init(&mutex_deadlocks_resueltos_totales, NULL);
	pthread_mutex_init(&mutex_cola_ready, NULL);
	pthread_mutex_init(&mutex_cola_ready, NULL);
	pthread_mutex_init(&mutex_pokemones_ubicados, NULL);


	srand(time(NULL));

	config = config_create(get_config_path(entrenador));
	printf("el entrenador que se va a cargar es el de la config: %s\n", entrenador);
	char* log_path = config_get_string_value(config, "LOG_FILE");
	logger = log_create(log_path, "Team", 1, LOG_LEVEL_DEBUG);
	IP_BROKER = config_get_string_value(config, "IP_BROKER");
	PUERTO_BROKER = config_get_string_value(config, "PUERTO_BROKER");
	PROCESS_ID = atoi(config_get_string_value(config, "PROCESS_ID"));
	RETARDO_CICLO_CPU = atoi(config_get_string_value(config, "RETARDO_CICLO_CPU"));
	algoritmo = get_algoritmo(config);
	TIEMPO_RECONEXION = atoi(config_get_string_value(config,"TIEMPO_RECONEXION"));

	//sem_init(&s_control_planificador_rr, 0, algoritmo.quantum);

	return 1;
}



void entrenador(void* index){
	//pthread_mutex_lock(&mutex_list_entrenadores);
	t_entrenador* entrenador = list_get(entrenadores, (int)index);
	//pthread_mutex_unlock(&mutex_list_entrenadores);
	//NEW
	printf("este hilo maneja al entrenador %d\n", entrenador->id);
	while(!cumplio_objetivo(entrenador)){

		sem_wait(entrenador->semaforo); //READY, todavia no tengo ningun pokemon asignado
		printf("soy el entrenador %d y estoy ejecutando\n", entrenador->id);
		if(proceso_ejecutando != entrenador->id){
			cambios_contexto++;
			proceso_ejecutando = entrenador->id;
		}
		printf("----------------------------\n");
		if(entrenador->pokemon_destino != NULL){ // agora sim

		log_info(logger, "El entrenador %d se mueve a atrapar a %s en posición %d-%d.\n",
				entrenador->id, entrenador->pokemon_destino->nombre,
				entrenador->pokemon_destino->posicion.X,
				entrenador->pokemon_destino->posicion.Y);
			ejecutar_algoritmo(entrenador);
		}
		printf("----------------------------\n");
	}

	log_info(logger, "El entrenador %d cumplió todos sus objetivos.\n", entrenador->id);
	entrenador->estado=EXIT;
	//show_entrenadores();
	sem_post(&s_detectar_deadlock);
	sem_post(&s_entrenador_exit);
	//show_entrenadores();

}


void liberar_memoria(){
	liberar_elementos_lista_pokemon(objetivo_global);
	liberar_elementos_lista_entrenador(entrenadores);
	liberar_elementos_lista_pokemon(pokemones_recibidos);
	liberar_elementos_lista_pokemon(pokemones_ubicados);
	//liberar_elementos_lista_deadlock(total_deadlocks);
	free(total_deadlocks);;
	liberar_elementos_lista_entrenador(entrenadores_DL);
	free(mensajes_get_esperando_respuesta);
	free(mensajes_catch_esperando_respuesta);
	free(cola_ready);
	sem_destroy(&s_cola_ready_con_items);
	sem_destroy(&s_procesos_en_exec);
	sem_destroy(&s_posiciones_a_mover);
	sem_destroy(&s_control_planificador_rr);
	sem_destroy(&s_detectar_deadlock);
	sem_destroy(&s_replanificar);
	sem_destroy(&s_entrenador_exit);
	pthread_mutex_destroy(&mutex_cola_ready);
	pthread_mutex_destroy(&mutex_ciclos_totales);
	pthread_mutex_destroy(&mutex_deadlocks_totales);
	pthread_mutex_destroy(&mutex_deadlocks_resueltos_totales);
	config_destroy(config);
	log_destroy(logger);

}


int32_t main(int32_t argc, char** argv){
	if(!argv[1]){
		printf("Fata definir el team a cargar\n");
		return EXIT_FAILURE;
	}

	inicializar_team(argv[1]);
    cantidad_entrenadores = array_length(config_get_array_value(config, "POSICIONES_ENTRENADORES"));
	printf("Inicializacion finalizada.\nCantidad dentrenadores: %d\nAlgoritmo:%s\n",
			cantidad_entrenadores, algoritmo.algoritmo_string);

    entrenadores = get_entrenadores(config, cantidad_entrenadores);

    objetivo_global = get_objetivo_global(entrenadores);



    generar_y_enviar_get();

    pthread_t p_suscribirse_appeared;
    op_code op_appeared = SUSCRIPCION_APPEARED;
   	pthread_create(&p_suscribirse_appeared, NULL, (void*)&hilo_suscriptor, &op_appeared);
   	pthread_detach(p_suscribirse_appeared);

	pthread_t p_suscribirse_caught;
	op_code op_caught = SUSCRIPCION_CAUGHT;
	pthread_create(&p_suscribirse_caught, NULL, (void*)&hilo_suscriptor, &op_caught);
	pthread_detach(p_suscribirse_caught);

	pthread_t p_suscribirse_localized;
	op_code op_localized = SUSCRIPCION_LOCALIZED;
	pthread_create(&p_suscribirse_localized, NULL, (void*)&hilo_suscriptor, &op_localized);
	pthread_detach(p_suscribirse_localized);


    /* HILO PLANIFICADOR */
    pthread_t p_planificador;
	pthread_create(&p_planificador, NULL, (void*)hilo_planificador, NULL);
	pthread_detach(p_planificador);


    /* HILO DETECTOR DE DEADLOCKS */
    pthread_t p_detectar_deadlock;
	pthread_create(&p_detectar_deadlock, NULL, (void*)hilo_detectar_deadlock, NULL);
	pthread_detach(p_detectar_deadlock);

	 /* HILO DETECTOR DE DEADLOCKS */
	pthread_t p_replanificar;
	pthread_create(&p_replanificar, NULL, (void*)hilo_replanificar, NULL);
	pthread_detach(p_replanificar);

	 /* HILO DETECTOR DE DEADLOCKS */
	pthread_t p_replanificar_sjfcd;
	pthread_create(&p_replanificar_sjfcd, NULL, (void*)hilo_replanificar_sjfcd, NULL);
	pthread_detach(p_replanificar_sjfcd);

	/* UN HILO POR PROCESO */
    for(int x = 0; x < cantidad_entrenadores; x++){
    	pthread_t p_entrenador;
		pthread_create(&p_entrenador, NULL, (void*)entrenador, (void*)x);
		pthread_detach(p_entrenador);
    }

    pthread_t p_escuchador;
    /* HILO ESCUCHADOR DE GAMEBOY */
    char* ip = config_get_string_value(config, "IP");
	char* puerto = config_get_string_value(config, "PUERTO");

	socket_escucha_team = crear_socket_escucha(ip, puerto);

	printf("Creado socket de escucha \n");

	if(socket_escucha_team == -1){
		log_error(logger, "Fallo al crear socket de escucha = -1\n");
	} else {
	    pthread_create(&p_escuchador, NULL, (void*)hilo_recibidor_mensajes_gameboy, (void*)entrenadores);
	}

	pthread_detach(p_escuchador);

	 /* HILO DE EXITS */
	pthread_t p_exit;
	pthread_create(&p_exit, NULL, (void*)hilo_exit, NULL);
	pthread_join(p_exit, NULL);


	liberar_conexion(socket_escucha_team);
	liberar_memoria();

    printf("End\n");

    return EXIT_SUCCESS;
}




