#ifndef TCP_H
#define TCP_H

#include "esp_err.h"

esp_err_t create_tcp_server(int port);
esp_err_t create_tcp_client(const char*ip,int port); 


extern int server_socket;
extern int connect_socket;
#endif