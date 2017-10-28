#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"


#include "tcp.h"

#define TAG "tcp:"

static struct sockaddr_in server_addr;
static struct sockaddr_in client_addr;
int server_socket;
int connect_socket;
static unsigned int socklen = sizeof(client_addr);
/* FreeRTOS event group to signal when we are connected to wifi */


static int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    if(getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1) {
	ESP_LOGE(TAG, "getsockopt failed");
	return -1;
    }
    return result;
}

static int show_socket_error_reason(int socket)
{
    int err = get_socket_error_code(socket);
    ESP_LOGW(TAG, "socket error %d %s", err, strerror(err));
    return err;
}





//use this esp32 as a tcp server. return ESP_OK:success ESP_FAIL:error
esp_err_t create_tcp_server(int port)
{
    ESP_LOGI(TAG, "server socket....port=%d\n", port);
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
    	show_socket_error_reason(server_socket);
	return ESP_FAIL;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    	show_socket_error_reason(server_socket);
	close(server_socket);
	return ESP_FAIL;
    }
    if (listen(server_socket, 5) < 0) {
    	show_socket_error_reason(server_socket);
	close(server_socket);
	return ESP_FAIL;
    }
    connect_socket = accept(server_socket, (struct sockaddr*)&client_addr, &socklen);
    if (connect_socket<0) {
    	show_socket_error_reason(connect_socket);
	close(server_socket);
	return ESP_FAIL;
    }
    /*connection established，now can send/recv*/
    ESP_LOGI(TAG, "tcp connection established!");
    return ESP_OK;
}
//use this esp32 as a tcp client. return ESP_OK:success ESP_FAIL:error
esp_err_t create_tcp_client(const char*ip,int port)
{
    ESP_LOGI(TAG, "client socket....serverip:port=%s:%d\n", 
    		ip, port);
    connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect_socket < 0) {
    	show_socket_error_reason(connect_socket);
	return ESP_FAIL;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    ESP_LOGI(TAG, "connecting to server...");
    if (connect(connect_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    	show_socket_error_reason(connect_socket);
	return ESP_FAIL;
    }
    ESP_LOGI(TAG, "connect to server success!");
    return ESP_OK;
}
