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

#include "http.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "hal_i2s.h"
#include "wm8978.h"
#include "http_parser.h"
#include "url_parser.h"
#include "spiram_fifo.h"
#include "mp3_decode.h"

#define TAG "player:"

static int header_value_callback(http_parser* a, const char *at, size_t length){
	for(int i=0;i<length;i++)
		putchar(at[i]);
	return 0;
}
static int body_callback(http_parser* a, const char *at, size_t length){
    // static uint32_t cnt=0;
    // printf("cnt:%d\n",cnt++);
    spiRamFifoWrite(at,length);
    return 0;
}
static int body_done_callback (http_parser* a){
  	ESP_LOGI(TAG,"body done");
    return 0;
}
static int begin_callback (http_parser* a){
  	ESP_LOGI(TAG,"message begin");
    return 0;
}
static int header_complete_callback(http_parser* a){
	ESP_LOGI(TAG,"header completed");
    return 0;
}

static http_parser_settings settings =
{   .on_message_begin = begin_callback
    ,.on_header_field = 0
    ,.on_header_value = header_value_callback
    ,.on_url = 0
    ,.on_status = 0
    ,.on_body = body_callback
    ,.on_headers_complete = header_complete_callback
    ,.on_message_complete = body_done_callback
    ,.on_chunk_header = 0
    ,.on_chunk_complete = 0
};


void player_task(void* pvParameters){
	//init codec and i2s
	hal_i2c_init(0,5,17);
    hal_i2s_init(0,48000,16,2); //8k 16bit 1 channel
    WM8978_Init(); 
    WM8978_ADDA_Cfg(1,1); 
    WM8978_Input_Cfg(1,0,0);     
    WM8978_Output_Cfg(1,0); 
    WM8978_MIC_Gain(35);
    WM8978_AUX_Gain(0);
    WM8978_LINEIN_Gain(0);
    WM8978_SPKvol_Set(0);
    WM8978_HPvol_Set(20,20);
    WM8978_EQ_3D_Dir(1);
    WM8978_EQ1_Set(0,24);
    WM8978_EQ2_Set(0,24);
    WM8978_EQ3_Set(0,24);
    WM8978_EQ4_Set(0,24);
    WM8978_EQ5_Set(0,0);
    spiRamFifoInit();
    xTaskCreate(mp3_decode_task, "mp3_decode_task", 8192, NULL, 5, NULL);
    //start a http requet
    http_client_get("http://icecast.omroep.nl/3fm-sb-mp3", &settings,NULL);




    ESP_LOGE(TAG,"get completed!");
    vTaskDelete(NULL);
}