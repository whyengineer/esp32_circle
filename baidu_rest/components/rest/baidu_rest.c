#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "baidu_rest.h"
#include "http.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "hal_i2s.h"
#include "wm8978.h"
#include "http_parser.h"
#include "url_parser.h"
#include "mbedtls/base64.h"
#include "spiram_fifo.h"
#include "record_task.h"

#define TAG "REST:"

char rsa_result[256];

uint32_t http_body_length=0;
char* http_body=NULL;
static int body_callback(http_parser* a, const char *at, size_t length){
    http_body=realloc(http_body,http_body_length+length);
    memcpy(http_body+http_body_length,at,length);
    http_body_length+=length;
    ESP_LOGI(TAG,"http return code:%d",a->status_code);
    // cJSON *root;
    // root= cJSON_Parse(http_body);
    // char* err_msg;
    // err_msg=cJSON_GetObjectItem(root,"err_msg")->valuestring;
    // ESP_LOGI(TAG,"err_msg:%s",err_msg);
    // char* sn;
    // err_msg=cJSON_GetObjectItem(root,"sn")->valuestring;
    // ESP_LOGI(TAG,"sn:%s",err_msg);
    // int err_no;
    // err_no=cJSON_GetObjectItem(root,"err_no")->valueint;
    // ESP_LOGI(TAG,"err_msg:%d",err_no);
    // cJSON_Delete(root);
    //printf("recv data:%s\n", http_body);
    //ESP_LOGI(TAG,"received body:%s",http_body);
    return 0;
}
static int body_done_callback (http_parser* a){
    http_body=realloc(http_body,http_body_length+1);
    http_body[http_body_length]='\0';
    // ESP_LOGI(TAG,"return code:%d",a->status_code);
    // ESP_LOGI(TAG,"request method:%d",a->method);
    // ESP_LOGI(TAG,"data:%s",http_body);
    cJSON *root;
    root= cJSON_Parse(http_body);
    char* err_msg;
    err_msg=cJSON_GetObjectItem(root,"err_msg")->valuestring;
    ESP_LOGI(TAG,"err_msg:%s",err_msg);
    char* sn;
    err_msg=cJSON_GetObjectItem(root,"sn")->valuestring;
    ESP_LOGI(TAG,"sn:%s",err_msg);
    int err_no;
    err_no=cJSON_GetObjectItem(root,"err_no")->valueint;
    ESP_LOGI(TAG,"err_msg:%d",err_no);
    //char* result=cJSON_GetObjectItem(root,"result")->valuestring;
    //ESP_LOGI(TAG,"result:%s",result);
    memcpy(rsa_result,http_body,strlen(http_body));
    cJSON_Delete(root);
    ESP_LOGI(TAG,"received body:%s",http_body);
    xEventGroupSetBits(record_event_group, RECORD_DONE);
    free(http_body);
    return 0;
}

static http_parser_settings settings_null =
{   .on_message_begin = 0
    ,.on_header_field = 0
    ,.on_header_value = 0
    ,.on_url = 0
    ,.on_status = 0
    ,.on_body = body_callback
    ,.on_headers_complete = 0
    ,.on_message_complete = body_done_callback
    ,.on_chunk_header = 0
    ,.on_chunk_complete = 0
};
#define MAX_LENGTH 8*1000*16*10  //base64 8k 16bits 40s 
const char* stream_head="{\"format\":\"wav\",\"cuid\":\"esp32_whyengineer\",\"token\":\"24.44810154581d4b7e8cc3554c90b949f0.2592000.1505980562.282335-10037482\",\"rate\":8000,\"channel\":1,\"speech\":\"";//","len":0,}"
static int baid_http_post(http_parser_settings *callbacks, void *user_data)
{
    url_t *url = url_parse("http://vop.baidu.com/server_api");
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    char port_str[6]; // stack allocated
    snprintf(port_str, 6, "%d", url->port);

    int err = getaddrinfo(url->host, port_str, &hints, &res);
    if(err != ESP_OK || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        return err;
    }

    // print resolved IP
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    // allocate socket
    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if(sock < 0) {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
    }
    ESP_LOGI(TAG, "... allocated socket");


    // connect, retrying a few times
    char retries = 0;
    while(connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        retries++;
        ESP_LOGE(TAG, "... socket connect attempt %d failed, errno=%d", retries, errno);

        if(retries > 5) {
            ESP_LOGE(TAG, "giving up");
            close(sock);
            freeaddrinfo(res);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);
    uint32_t length=MAX_LENGTH;
    // write http request
    char *request;
    if(asprintf(&request, "POST %s HTTP/1.1\r\nHost: %s:%d\r\nTransfer-Encoding: chunked\r\nUser-Agent: ESP32\r\nAccept: */*\r\n\r\n", url->path, url->host, url->port) < 0)
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "requesting %s", request);

    if (write(sock, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        close(sock);
    }

    free(request);
    ESP_LOGI(TAG, "... socket send success");
   	uint32_t w,r;
    char chunk_len[6];
   	//stream head
   	printf("%s",stream_head );
    sprintf(chunk_len,"%x\r\n",strlen(stream_head));
    write(sock, chunk_len, strlen(chunk_len));
   	write(sock, stream_head, strlen(stream_head));
    write(sock, "\r\n",2);
   	ESP_LOGI(TAG, "#head success");
    //loop write
    uint32_t wav_len=0;
    uint32_t olen=0;
    uint8_t *read_buf=malloc(1026); //1026/3=342
    if(read_buf==NULL){
    	ESP_LOGE(TAG,"read_buf malloc failed!");
    	return ESP_FAIL;
    }
    uint8_t *dst_buf=malloc(1369);
    if(dst_buf==NULL){
    	ESP_LOGE(TAG,"read_buf malloc failed!");
    	return ESP_FAIL;
    }
    EventBits_t uxBits;
    while(1){
      uxBits=xEventGroupWaitBits(record_event_group,RECORD_STOP,pdTRUE,pdTRUE,0);
      if((uxBits & RECORD_STOP)!=0){
            //find the record stop event
            break;
      }
    	spiRamFifoRead((char*)read_buf, 1026);
    	wav_len+=1026;
    	if(mbedtls_base64_encode(dst_buf,1369,&olen,read_buf,1026)){
    		ESP_LOGE(TAG,"base64 encode failed!");
    		ESP_LOGE(TAG,"olen:%d",olen);
    	}
      sprintf(chunk_len,"%x\r\n",olen);
      write(sock, chunk_len, strlen(chunk_len));
    	write(sock, dst_buf, olen);
      write(sock, "\r\n",2);
      //printf("%s",dst_buf);
    }
    ESP_LOGI(TAG, "#loop success");
   	free(read_buf);
   	free(dst_buf);
   	//steaem len
    char stream_len[30];
    sprintf(stream_len,"\",\"len\":%d}",wav_len);
   	printf("%s",stream_len);
    sprintf(chunk_len,"%x\r\n",strlen(stream_len));
    write(sock, chunk_len, strlen(chunk_len));
   	write(sock, stream_len, strlen(stream_len));
    write(sock, "\r\n0\r\n\r\n",7);
   	ESP_LOGI(TAG, "#http send ok");

    /* Read HTTP response */
    char recv_buf[64];
    bzero(recv_buf, sizeof(recv_buf));
    ssize_t recved;

    /* intercept on_headers_complete() */

    /* parse response */
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data = user_data;

    esp_err_t nparsed = 0;
    do {
        recved = read(sock, recv_buf, sizeof(recv_buf)-1);

        // using http parser causes stack overflow somtimes - disable for now
        nparsed = http_parser_execute(&parser, callbacks, recv_buf, recved);

        // invoke on_body cb directly
        // nparsed = callbacks->on_body(&parser, recv_buf, recved);
    } while(recved > 0 && nparsed >= 0);

    free(url);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d", recved, errno);
    close(sock);
    ESP_LOGI(TAG, "socket closed");

    return 0;
}

int start_record_audio(){
	WAV_HEADER wav_header;
	memcpy(wav_header.rld,"RIFF",4);
    memcpy(wav_header.wld,"WAVE",4);
    memcpy(wav_header.fld,"fmt ",4);
    wav_header.fLen=0x00000010;
    wav_header.wFormatTag=0x0001;
    wav_header.wChannels=0x0001;
    wav_header.nSamplesPersec=8000;
    wav_header.nAvgBitsPerSample=8000*1*2;
    wav_header.wBlockAlign=1*16/8;
    wav_header.wBitsPerSample=16;
    memcpy(wav_header.dld,"data",4);

    int w;
	FILE* f = fopen("/sdcard/record.wav", "wb");
	w=fwrite(&wav_header,1,sizeof(wav_header),f);
	if(w!=sizeof(wav_header)){
		ESP_LOGE(TAG,"write wav_header failed!");
		return 1;
	}
	char* data=malloc(1024);
	uint8_t vad_cnt=0;
	int16_t* value=(int16_t*)data;
	
	while(1){
		hal_i2s_read(0,data,1024,portMAX_DELAY);
		if(vad_check(value,512)==0)
			vad_cnt++;
		else
			vad_cnt=0;
		if(vad_cnt>10)
			break;
		w=fwrite(data,1,1024,f);
	}
	int n=ftell(f);
	ESP_LOGI(TAG,"flle lenght:%d",n);
	fseek(f, 0, SEEK_SET);
    wav_header.wSampleLength=n-sizeof(wav_header);
    wav_header.rLen=n-8;
    w = fwrite(&wav_header, 1, sizeof(wav_header), f);
    fclose(f);
    return 0;
}
/**

not enough ram,giveup this way.


**/
// static char* baidu_rest_input(char* speech,uint32_t len){
// 	cJSON* root=NULL;
// 	root=cJSON_CreateObject();
// 	if(root==NULL){
// 		ESP_LOGI(TAG,"cjson root create failed\n");
// 		return NULL;
// 	}
// 	cJSON_AddStringToObject(root,"format","pcm");
// 	cJSON_AddStringToObject(root,"cuid","esp32_whyengineer");
// 	cJSON_AddStringToObject(root,"token",access_token);
// 	cJSON_AddNumberToObject(root, "rate", 8000);
// 	cJSON_AddNumberToObject(root, "channel", 1);

// 	cJSON_AddNumberToObject(root, "len", len);
// 	cJSON_AddStringToObject(root,"speech",speech);
	
// 	// if(!strncmp(item,"vbus",4)){
// 	// 	CALIB_DEBUG("%s\n",info);
// 	// 	cJSON_AddStringToObject(root,"content",info);
// 	// }

// 	char* out = cJSON_PrintUnformatted(root);
// 	//send(client,out,strlen(out),MSG_WAITALL);
// 	//printf("handle_return: %s\n", out);
// 	cJSON_Delete(root);
// 	return out;
// }
static int32_t sign(int32_t a){
	if(a>0)
		return 1;
	else
		return 0;
}
uint8_t vad_check(int16_t* data,uint32_t lenght){
	int sum=0;
	int delta_sum=0;
	for(int i=0;i<lenght-1;i++){
       	 if(sign(data[i])^sign(data[i+1]))
       	 	delta_sum++;
       	 //printf("%d\n",value[i] );
       	 if(data[i]<0)
       	 	sum-=data[i];
       	 else
       	 	sum+=data[i];
       	//value=(((int16_t)sample_data[i*2])<<8)&sample_data[i*2+1];
    }
    if(sum>150000&&delta_sum<150){
    	//gpio_set_level(GPIO_OUTPUT_IO_0,1);
    	return 1;
    }
    else{
       	//gpio_set_level(GPIO_OUTPUT_IO_0,0);
        return 0;
    }

}
void baidu_rest_task(void *pvParameters)
{
    
 	// //gpio_set_level(GPIO_OUTPUT_IO_0,1);
  //   ESP_LOGI(TAG, "baidu_rest task start");
  //   //char* json=baidu_rest_input("",0);
  //   //printf("%s\n", json);
  //   char *sample_data=malloc(1024);
  //   int16_t* value=(int16_t*)sample_data;
    
  //   if(sample_data==NULL){
  //  		ESP_LOGE(TAG, "sample_data malloc failed");
  //  		vTaskSuspend(NULL);
  //  	}
  //   //
  //   uint8_t vad_cnt=0;
  //   for(;;){
  //   	hal_i2s_read(0,sample_data,1024,portMAX_DELAY);
  //       if(vad_check(value,512)==1)
  //       	vad_cnt++;
  //       else
  //       	vad_cnt=0;
  //       if(vad_cnt==4){
  //       	start_record_audio();
  //       	break;
  //       }

  //   }
    
    //http_client_post("http://vop.baidu.com/server_api",&settings_null,NULL,json);
    //free(json);	
    //vTaskSuspend(NULL);
    // uint32_t io_num;
   	
   	
   	// hal_i2s_read(0,sample_data,1024,portMAX_DELAY);
   	// for(int i=0;i<1024;i++){
   	// 	printf("%d:%d\n",i,sample_data[i]);
   	// }	
   	baid_http_post(&settings_null,NULL);
    vTaskDelete(NULL);
    // for(;;) {
    //    hal_i2s_read(0,sample_data,1024,portMAX_DELAY);
    //    vad_check(value,512);
    //    hal_i2s_write(0,sample_data,1024,portMAX_DELAY);
    //    //printf("cnt:%d\n",cnt++ );
    // }
}