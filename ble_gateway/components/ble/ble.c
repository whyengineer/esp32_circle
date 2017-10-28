// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.



/****************************************************************************
*
* This file is for gatt client. It can scan ble device, connect one device,
*
****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "controller.h"

#include "bt.h"
#include "bt_trace.h"
#include "bt_types.h"
#include "btm_api.h"
#include "bta_api.h"
#include "bta_gatt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "freertos/event_groups.h"
#include "ble.h"
#include "cJSON.h"

#define GATTC_TAG "BLE"

EventGroupHandle_t ble_event_group;
///Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);


static struct BleServiceTypeDef* now_service_p;
static esp_gatt_srvc_id_t now_service;

static esp_gatt_id_t notify_descr_id = {
    .uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = GATT_UUID_CHAR_CLIENT_CONFIG,},
    },
    .inst_id = 0,
};
static esp_gatt_id_t get_char_descr_id = {
    .uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = GATT_UUID_CHAR_DESCRIPTION,},
    },
    .inst_id = 0,
};
static esp_gatt_id_t test_char_id = {
    .uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = 0x2a47,},
    },
    .inst_id = 0,
};

#define BT_BD_ADDR_STR         "%02x:%02x:%02x:%02x:%02x:%02x"
#define BT_BD_ADDR_HEX(addr)   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

static bool connect = false;

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30
};

esp_bd_addr_t dest_addr={0x02,0x01,0x05,0x08,0x09,0x52};

char * ble_way_result_json=NULL;

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};
static uint16_t conn_id;
static struct BleServiceTypeDef* ble_service_head=NULL;
static void ble_free_service_list(){
    struct BleServiceTypeDef* p1=ble_service_head;
    struct BleServiceTypeDef* p2=NULL;
    struct BleCharTypeDef* pc1=ble_service_head;
    struct BleCharTypeDef* pc2=NULL;
    while(p1!=NULL){
        p2=p1;
        p1=p1->next;
        pc1=p2->character;
        while(pc1!=NULL){
            pc2=pc1;
            pc1=pc1->next;
            free(pc2);
        }
        free(p2);
    }
    ble_service_head=NULL;
}
char * ble_char_result_json;
static void printf_services_result(){
    ESP_LOGI(TAG,"service done");
    struct BleServiceTypeDef* p=ble_service_head;
    uint8_t i=0;
    uint8_t j=0;
    cJSON *root = cJSON_CreateArray();
    if(root==NULL)
        ESP_LOGE(TAG,"malloc failed");
    cJSON *item=NULL;
    cJSON *character=NULL;
    cJSON *char_item=NULL;
    cJSON *c_prev;
    cJSON *prev=NULL;
    struct BleCharTypeDef* p_c;
    char uuid[33];
    while(p!=NULL){
        item=cJSON_CreateObject();
        if(item==NULL)
            ESP_LOGE(TAG,"malloc failed");
        cJSON_AddBoolToObject(item,"is_primary",p->is_primary);
        if(p->id.uuid.len==ESP_UUID_LEN_128){
            for(int j=0;j<ESP_UUID_LEN_128;j++){
                sprintf(uuid+j*2,"%02x",p->id.uuid.uuid.uuid128[j]);
            }
        }else if(p->id.uuid.len==ESP_UUID_LEN_32){
            sprintf(uuid,"%x",p->id.uuid.uuid.uuid32);
        }
        else if(p->id.uuid.len==ESP_UUID_LEN_16){
            sprintf(uuid,"%x",p->id.uuid.uuid.uuid16);
        }else{
            uuid[0]=0;
        }
        cJSON_AddStringToObject(item,"uuid",uuid);
        cJSON_AddNumberToObject(item,"inst_id",p->id.inst_id);
        if(p->character!=NULL){
            //have characteristic
            j=0;
            p_c=p->character;
            character=cJSON_CreateArray();
            do{
                char_item=cJSON_CreateObject();
                cJSON_AddNumberToObject(char_item,"prop",p_c->prop);
                if(p_c->id.uuid.len==ESP_UUID_LEN_128){
                    for(int j=0;j<ESP_UUID_LEN_128;j++){
                        sprintf(uuid+j*2,"%02x",p_c->id.uuid.uuid.uuid128[j]);
                    }
                }else if(p_c->id.uuid.len==ESP_UUID_LEN_32){
                    sprintf(uuid,"%x",p_c->id.uuid.uuid.uuid32);
                }
                else if(p_c->id.uuid.len==ESP_UUID_LEN_16){
                    sprintf(uuid,"%x",p_c->id.uuid.uuid.uuid16);
                }else{
                    uuid[0]=0;
                }
                cJSON_AddStringToObject(char_item,"uuid",uuid);
                if(j==0){
                    character->child=char_item;
                }else{
                    c_prev->next=char_item;
                    char_item->prev=c_prev;
                }
                p_c=p_c->next;
                c_prev=char_item;
                j++;
            }while(p_c!=NULL);
            cJSON_AddItemToObject(item, "character",character);
        }
        if(i==0){
            root->child=item;
        }else{
            prev->next=item;
            item->prev=prev;
        }
        p=p->next;
        prev=item;
        i++;
    }
    ble_char_result_json = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG,"json:%s",ble_char_result_json);
    //free(out);
    cJSON_Delete(root);
    //not need free the lsit, which wiil be used
    ble_free_service_list();
    ESP_LOGI(TAG,"char num:%d",i);
    //test_connect();
}
int add_services_result(esp_gatt_srvc_id_t* srvc_id){
    struct BleServiceTypeDef* p1;
    struct BleServiceTypeDef* p2;
    p1=ble_service_head;
    p2=NULL;
    //ESP_LOGE(TAG,"add!");
    while(p1!=NULL){
        p2=p1;
        p1=p1->next;
    }
    if(ble_service_head==NULL){
        ble_service_head=malloc(sizeof(struct BleServiceTypeDef));
        p2=ble_service_head;
    }
    else{
        p2->next=malloc(sizeof(struct BleServiceTypeDef));
        p2=p2->next;
    }
    p2->is_primary=srvc_id->is_primary;
    p2->id.inst_id=srvc_id->id.inst_id;
    p2->id.uuid.len=srvc_id->id.uuid.len;
    p2->character=NULL;
    if (srvc_id->id.uuid.len == ESP_UUID_LEN_16) {
        ESP_LOGI(GATTC_TAG, "UUID16: %x", srvc_id->id.uuid.uuid.uuid16);
        p2->id.uuid.uuid.uuid16=srvc_id->id.uuid.uuid.uuid16;
    } else if (srvc_id->id.uuid.len == ESP_UUID_LEN_32) {
        ESP_LOGI(GATTC_TAG, "UUID32: %x", srvc_id->id.uuid.uuid.uuid32);
        p2->id.uuid.uuid.uuid32=srvc_id->id.uuid.uuid.uuid32;
    } else if (srvc_id->id.uuid.len == ESP_UUID_LEN_128) {
        ESP_LOGI(GATTC_TAG, "UUID128:");
        esp_log_buffer_hex(GATTC_TAG, srvc_id->id.uuid.uuid.uuid128, ESP_UUID_LEN_128);
        memcpy(p2->id.uuid.uuid.uuid128,srvc_id->id.uuid.uuid.uuid128,ESP_UUID_LEN_128);
    } else {
        ESP_LOGE(GATTC_TAG, "UNKNOWN LEN %d", srvc_id->id.uuid.len);
    }
    //memcpy(p2,&(scan_result->scan_rst),sizeof(struct BleDeviceTypeDef)-6);
    p2->next=NULL;
    return 0;
}
int ble_add_char(struct BleServiceTypeDef* service,esp_ble_gattc_cb_param_t *p_data){
    struct BleCharTypeDef* p1;
    struct BleCharTypeDef* p2;
    p1=service->character;
    p2=NULL;
    while(p1!=NULL){
        p2=p1;
        p1=p1->next;
    }
    if(service->character==NULL){
        service->character=malloc(sizeof(struct BleCharTypeDef));
        p2=service->character;
    }
    else{
        p2->next=malloc(sizeof(struct BleCharTypeDef));
        p2=p2->next;
    }
    memcpy(&(p2->id),&(p_data->get_char.char_id),sizeof(esp_gatt_id_t));
    p2->prop=p_data->get_char.char_prop;
    //memcpy(p2,&(scan_result->scan_rst),sizeof(struct BleDeviceTypeDef)-6);
    p2->next=NULL;
    return 0;
}
static void ble_find_all_descr(){

}
static void ble_find_all_char(){
    struct BleServiceTypeDef* p=ble_service_head;
    while(p!=NULL){
        if(p->id.uuid.len==ESP_UUID_LEN_16){
            ESP_LOGI(TAG,"find service_id:%d",p->id.uuid.uuid.uuid16);
            now_service_p=p;
            now_service.is_primary=p->is_primary;
            now_service.id.inst_id=p->id.inst_id;
            now_service.id.uuid.len=ESP_UUID_LEN_16;
            now_service.id.uuid.uuid.uuid16=p->id.uuid.uuid.uuid16;
            esp_ble_gattc_get_characteristic(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, conn_id,&now_service, NULL);
            xEventGroupWaitBits(ble_event_group,BLE_SERVICE_DONE_EVENT,pdTRUE,pdTRUE,portMAX_DELAY);
        }
        p=p->next;
    }
    //not need free the lsit, which wiil be used
    //ble_free_service_list();
    //printf_services_result();
    ESP_LOGI(TAG,"find all charracteristic");
    //test_connect();
}
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        }
        break;
    case ESP_GATTC_OPEN_EVT:
        conn_id = p_data->open.conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->open.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_OPEN_EVT conn_id %d, if %d, status %d, mtu %d", conn_id, gattc_if, p_data->open.status, p_data->open.mtu);

        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));

        esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        esp_gatt_srvc_id_t *srvc_id = &p_data->search_res.srvc_id;
        add_services_result(srvc_id);
        conn_id = p_data->search_res.conn_id;
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        conn_id = p_data->search_cmpl.conn_id;
        ESP_LOGI(GATTC_TAG, "SEARCH_CMPL: conn_id = %x, status %d", conn_id, p_data->search_cmpl.status);
        //esp_ble_gattc_get_characteristic(gattc_if, conn_id,&ble_service_head->id, NULL);
        //printf_service_result();
        xEventGroupSetBits(ble_event_group, BLE_SERVICES_DONE_EVENT);
        break;
    case ESP_GATTC_GET_CHAR_EVT:
        if (p_data->get_char.status != ESP_GATT_OK) {
            ESP_LOGE(TAG,"Error code:%d",p_data->get_char.status);
            xEventGroupSetBits(ble_event_group, BLE_SERVICE_DONE_EVENT);
            break;
        }
        
        /*
        ESP_LOGI(GATTC_TAG, "GET CHAR: conn_id = %x, status %d", p_data->get_char.conn_id, p_data->get_char.status);
        ESP_LOGI(GATTC_TAG, "GET CHAR: srvc_id = %04x, char_id = %04x, prop = %04x", p_data->get_char.srvc_id.id.uuid.uuid.uuid16, p_data->get_char.char_id.uuid.uuid.uuid16,\
             p_data->get_char.char_prop);
            */
        ble_add_char(now_service_p,p_data);
        // if (p_data->get_char.char_id.uuid.len==ESP_UUID_LEN_16&&(p_data->get_char.char_prop&ESP_GATT_CHAR_PROP_BIT_READ)) {
        //      // esp_ble_gattc_read_char_descr (gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
        //      //                            conn_id,
        //      //                            &now_service,
        //      //                            &p_data->get_char.char_id,
        //      //                            &get_char_descr_id,
        //      //                            ESP_GATT_AUTH_REQ_NONE);
        //      esp_ble_gattc_read_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
        //                                 conn_id,
        //                                 &now_service,
        //                                 &p_data->get_char.char_id,
        //                                 ESP_GATT_AUTH_REQ_NONE);
        // //     ESP_LOGI(GATTC_TAG, "read des!!!");
        // //     esp_ble_gattc_read_char_descr (gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
        // //                                 conn_id,
        // //                                 &now_service,
        // //                                 &p_data->get_char.char_id,
        // //                                 &char_descr_id,
        // //                                 ESP_GATT_AUTH_REQ_NONE);
        // //     //esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, &alert_service_id, &p_data->get_char.char_id);
        // // }
        // }
        esp_ble_gattc_get_characteristic(gattc_if, conn_id,&now_service , &p_data->get_char.char_id);
        break;
    case ESP_GATTC_READ_DESCR_EVT:{
        ESP_LOGI(GATTC_TAG, "char_id: len %x", p_data->read.char_id.uuid.uuid.uuid16);
        ESP_LOGI(GATTC_TAG, "res: len %d, value %08x", p_data->read.value_len, *(uint32_t *)p_data->read.value);
        char* content=malloc((p_data->read.value_len)+1);
        memcpy(content,p_data->read.value,p_data->read.value_len);
        content[p_data->read.value_len]=0;
        cJSON* root=cJSON_CreateObject();
        cJSON_AddNumberToObject(root,"err",0);
        cJSON_AddStringToObject(root,"content",content);
        ble_way_result_json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        xEventGroupSetBits(ble_event_group, BLE_WEB_WAY_DONE_EVENT);
        break;
    }
    case ESP_GATTC_READ_CHAR_EVT:{
        ESP_LOGI(GATTC_TAG, "char_id: len %x", p_data->read.char_id.uuid.uuid.uuid16);
        ESP_LOGI(GATTC_TAG, "res: len %d, value %08x", p_data->read.value_len, *(uint32_t *)p_data->read.value);
        char* content=malloc((p_data->read.value_len)+1);
        memcpy(content,p_data->read.value,p_data->read.value_len);
        content[p_data->read.value_len]=0;
        cJSON* root=cJSON_CreateObject();
        cJSON_AddNumberToObject(root,"err",0);
        cJSON_AddStringToObject(root,"content",content);
        ble_way_result_json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        xEventGroupSetBits(ble_event_group, BLE_WEB_WAY_DONE_EVENT);
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:{
        cJSON* root=cJSON_CreateObject();
        cJSON_AddNumberToObject(root,"err",0);
        cJSON_AddStringToObject(root,"content","write successfully");
        ble_way_result_json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        xEventGroupSetBits(ble_event_group, BLE_WEB_WAY_DONE_EVENT);
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        uint16_t notify_en = 1;
        ESP_LOGI(GATTC_TAG, "REG FOR NOTIFY: status %d", p_data->reg_for_notify.status);
        ESP_LOGI(GATTC_TAG, "REG FOR_NOTIFY: srvc_id = %04x, char_id = %04x", p_data->reg_for_notify.srvc_id.id.uuid.uuid.uuid16, p_data->reg_for_notify.char_id.uuid.uuid.uuid16);

        // esp_ble_gattc_write_char_descr(
        //         gattc_if,
        //         conn_id,
        //         &alert_service_id,
        //         &p_data->reg_for_notify.char_id,
        //         &notify_descr_id,
        //         sizeof(notify_en),
        //         (uint8_t *)&notify_en,
        //         ESP_GATT_WRITE_TYPE_RSP,
        //         ESP_GATT_AUTH_REQ_NONE);
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "NOTIFY: len %d, value %08x", p_data->notify.value_len, *(uint32_t *)p_data->notify.value);
        break;
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(GATTC_TAG, "WRITE: status %d", p_data->write.status);
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:%08x%04x",(bda[0] << 24) + (bda[1] << 16) + (bda[2] << 8) + bda[3],
                 (bda[4] << 8) + bda[5]);
        break;
    }
    default:
        break;
    }
}

struct BleDeviceTypeDef* ble_device_head=NULL;
static uint8_t device_id=0;
char * ble_scan_result_json;
int add_scan_result(esp_ble_gap_cb_param_t* scan_result){
    int i=0;
    struct BleDeviceTypeDef* p1;
    struct BleDeviceTypeDef* p2;
    p1=ble_device_head;
    p2=NULL;
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    //ESP_LOGE(TAG,"add!");
    while(p1!=NULL){
        for(i=0;i<6;i++){
            if(p1->bda[i]!=scan_result->scan_rst.bda[i])
                break;
        }
        //same mac addr
        if(i==6){
            //ESP_LOGI(TAG,"SAME MAC ADDR");
            return 1;
        }
        p2=p1;
        p1=p1->next;
    }
    if(ble_device_head==NULL){
        ble_device_head=malloc(sizeof(struct BleDeviceTypeDef));
        p2=ble_device_head;
    }
    else{
        p2->next=malloc(sizeof(struct BleDeviceTypeDef));
        p2=p2->next;
    }
    adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                        ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
    if(adv_name_len){
        p2->adv_data=malloc(adv_name_len+1);
        memcpy(p2->adv_data,adv_name,adv_name_len);
        p2->adv_data[adv_name_len]=0;
    }else
        p2->adv_data=NULL;
    //memcpy(p2,&(scan_result->scan_rst),sizeof(struct BleDeviceTypeDef)-6);
    memcpy(p2->bda,scan_result->scan_rst.bda,6);
    p2->rssi=scan_result->scan_rst.rssi;
    p2->next=NULL;
    p2->id=device_id;
    device_id++;
    return 0;
}
void ble_connect_device(char* mac_addr){
    esp_bd_addr_t add;
    memcpy(add,mac_addr,6);
    esp_log_buffer_hex(GATTC_TAG,mac_addr,sizeof(esp_bd_addr_t));
    ESP_LOGI(GATTC_TAG, "Connect to the remote device.");
    // esp_ble_gap_stop_scanning();
    // esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,add,true);
    esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,add,true);
}
void ble_free_list(){
    struct BleDeviceTypeDef* p1=ble_device_head;
    struct BleDeviceTypeDef* p2=NULL;
    while(p1!=NULL){
        p2=p1;
        p1=p1->next;
        free(p2->adv_data);
        free(p2);
    }
    ble_device_head=NULL;
    device_id=0;
}
void ble_start_scan(){
    uint32_t duration = 3;
    esp_ble_gap_start_scanning(duration);
}


static void printf_scan_result(){
    ESP_LOGI(TAG,"scan done");
    struct BleDeviceTypeDef* p=ble_device_head;
    uint8_t i=0;
    char bda_str[20];
    cJSON *root = cJSON_CreateArray();
    cJSON *item=NULL;
    cJSON *prev=NULL;
    while(p!=NULL){
        item=cJSON_CreateObject();
        sprintf(bda_str,BT_BD_ADDR_STR,p->bda[0],p->bda[1],p->bda[2],p->bda[3],\
            p->bda[4],p->bda[5]);
        esp_log_buffer_hex(GATTC_TAG, p->bda, 6);
        if(p->adv_data==NULL)
            cJSON_AddStringToObject(item,"name","Unnamed");
        else{
            // if(strcmp(p->adv_data,"Alert Notifictaion")==0){
            //     if (connect == false) {
            //         connect = true;
            //         ESP_LOGI(TAG,"Find The Device");
            //         //esp_ble_gap_stop_scanning();
            //         esp_err_t open_ret=esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,p->bda,true);
            //         if (open_ret){
            //             ESP_LOGE(GATTC_TAG, "open error, error code = %x", open_ret);
            //         }
            //     }
            // }
            cJSON_AddStringToObject(item,"name",p->adv_data);
        }
        ESP_LOGI(TAG,"rssi:%d",p->rssi);
        cJSON_AddNumberToObject(item,"id",p->id);
        cJSON_AddNumberToObject(item,"rssi",p->rssi);
        cJSON_AddStringToObject(item,"mac",bda_str);
        p=p->next;

        if(i==0){
            root->child=item;
        }else{
            prev->next=item;
            item->prev=prev;
        }
        prev=item;
        i++;
    }
    ble_scan_result_json = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG,"json:%s",ble_scan_result_json);
    //free(out);
    cJSON_Delete(root);
    ble_free_list();
    ESP_LOGI(TAG,"device num:%d",i);
    ESP_LOGI(TAG,"start connect");
    //test_connect();
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        //the unit of the duration is second
        //ESP_LOGE(TAG,"START SCAN");
        //uint32_t duration = 5;
        //esp_ble_gap_start_scanning(duration);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "Scan start failed");
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            
            add_scan_result(scan_result);
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            //
            xEventGroupSetBits(ble_event_group, BLE_SCAN_DONE_EVENT);
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Scan stop failed");
        }
        else {
            ESP_LOGI(GATTC_TAG, "Stop scan successfully");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Adv stop failed");
        }
        else {
            ESP_LOGI(GATTC_TAG, "Stop adv successfully");
        }
        break;

    default:
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    ESP_LOGI(GATTC_TAG, "EVT %d, gattc if %d", event, gattc_if);

    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id, 
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

void ble_client_appRegister(void)
{
    esp_err_t status;

    ESP_LOGI(GATTC_TAG, "register callback");
    //register the scan callback function to the gap module
    if ((status = esp_ble_gap_register_callback(esp_gap_cb)) != ESP_OK) {
        ESP_LOGE(GATTC_TAG, "gap register error, error code = %x", status);
        return;
    }

    //register the callback function to the gattc module
    if ((status = esp_ble_gattc_register_callback(esp_gattc_cb)) != ESP_OK) {
        ESP_LOGE(GATTC_TAG, "gattc register error, error code = %x", status);
        return;
    }
    esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    //esp_ble_gattc_app_register(PROFILE_B_APP_ID);
}
void gattc_client_test(void)
{
    ble_event_group = xEventGroupCreate();
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    esp_bluedroid_init();
    esp_bluedroid_enable();
    ble_client_appRegister();
}
void ble_send_way(uint16_t s_id,uint16_t c_id,uint8_t way){
    //1:read
    //2:write
    //3:read description
    now_service.id.uuid.uuid.uuid16=s_id;
    test_char_id.uuid.uuid.uuid16=c_id;
    if(way==1){
        esp_ble_gattc_read_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                         conn_id,
                                         &now_service,
                                         &test_char_id,
                                         ESP_GATT_AUTH_REQ_NONE);
    }else if(way==2){
        uint8_t a[2];
        a[0]='w';
        a[1]='e';
        esp_ble_gattc_write_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                    conn_id,
                                    &now_service,
                                    &test_char_id,
                                    2,
                                    a,
                                    ESP_GATT_WRITE_TYPE_RSP,
                                    ESP_GATT_AUTH_REQ_NONE);
    }else if(way==3){
        esp_ble_gattc_read_char_descr (gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                                        conn_id,
                                        &now_service,
                                        &test_char_id,
                                        &get_char_descr_id,
                                        ESP_GATT_AUTH_REQ_NONE);
    }
    

}
void ble_task(void *param){
    gattc_client_test();
    //vTaskDelete(NULL);
    EventBits_t uxBits;
    //ble_start_scan();
    while(1){
        uxBits=xEventGroupWaitBits(ble_event_group,\
            BLE_SCAN_DONE_EVENT|BLE_SERVICES_DONE_EVENT,pdTRUE,pdFALSE,portMAX_DELAY);
        if(( uxBits & BLE_SCAN_DONE_EVENT ) != 0 ){
            //scan done event;
            //if(connect==false)
            printf_scan_result();
            xEventGroupSetBits(ble_event_group, BLE_WEB_SCAN_DONE_EVENT);
        }else if(( uxBits & BLE_SERVICES_DONE_EVENT ) != 0 ){
            //printf_services_result();
            ESP_LOGI(TAG,"start find all charracteristic");
            ble_find_all_char();
            printf_services_result();
            xEventGroupSetBits(ble_event_group, BLE_WEB_CHAR_DONE_EVENT);
            
            //ble_get_service(ble_service_head);
            //ble_find_all_descr();
            // 
            // esp_ble_gattc_read_char_descr (gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
            //                             conn_id,
            //                             &now_service,
            //                             &test_char_id,
            //                             &get_char_descr_id,
            //                             ESP_GATT_AUTH_REQ_NONE);
            // esp_err_t err=esp_ble_gattc_read_char_descr (gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
            //                             conn_id,
            //                             &now_service,
            //                             &test_char_id,
            //                             &char_descr_id,
            //                             ESP_GATT_AUTH_REQ_MITM);
            // if(err!=ESP_OK){
            //     ESP_LOGE(TAG,"read failed");
            // }else{
            //     ESP_LOGI(TAG,"read successfully");
            // }

        }
    }
    
}

