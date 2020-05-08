/*
 * socketsUnificado.h
 *
 *  Created on: 29 abr. 2020
 *      Author: utnso
 */

#ifndef SOCKETS_H_
#define SOCKETS_H_

#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netdb.h>
#include<sys/socket.h>
#include<string.h>
#include<pthread.h> // Servidor
#include<commons/log.h> //Cliente
#include<commons/collections/list.h> // Cliente
#include<signal.h> // Cliente
#include<unistd.h> // Cliente

//#define IP_SERVIDOR "127.0.0.1"
//#define PUERTO_SERVIDOR "4444"

char * IP_SERVIDOR;
char * PUERTO_SERVIDOR;


typedef enum
{
	SUSCRIPCION=0,
	NEW_POKEMON=1,
	APPEARED_POKEMON=2,
	CATCH_POKEMON=3,
	CAUGHT_POKEMON=4,
	GET_POKEMON=5,
	LOCALIZED_POKEMON=6,
	MENSAJE = 99
}op_code;

typedef struct
{
	int size;
	void* stream; //en el stream debe ir la estructura administrativa: id_Mensaje + Mensaje
} t_buffer;

typedef struct
{
	op_code codigo_operacion;
	t_buffer* buffer;
} t_paquete;

pthread_t thread_socket_global;

int crear_conexion(char* ip, char* puerto);
void enviar_mensaje(char* mensaje, int socket_cliente);
char* recibir_mensaje(int socket_cliente);
void eliminar_paquete(t_paquete* paquete);
void liberar_conexion(int socket_cliente);

void* serializar_paquete(t_paquete* paquete, int* bytes);

void* recibir_buffer(int*, int);
void iniciar_servidor(void);
void esperar_cliente(int);
void* recibir_mensaje_servidor(int socket_cliente, int* size);
int recibir_operacion(int);
void process_request(int cod_op, int cliente_fd);
void serve_client(int *socket);
void devolver_mensaje(void* payload, int size, int socket_cliente);


#endif /* SOCKETS_H_ */