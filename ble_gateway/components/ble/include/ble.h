#ifndef BLE_H
#define BLE_H
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "freertos/event_groups.h"
void gattc_client_test();
void ble_task(void *param);
void ble_free_list();
void ble_start_scan();
void ble_connect_device(char* mac_addr);
void ble_send_way(uint16_t s_id,uint16_t c_id,uint8_t way);
//void ble_connect_device(char* mac_addr);
#define BLE_SCAN_DONE_EVENT BIT0
#define BLE_WEB_SCAN_DONE_EVENT BIT1

#define BLE_SERVICES_DONE_EVENT BIT2
#define BLE_WEB_SERVICES_DONE_EVENT BIT3

#define BLE_SERVICE_DONE_EVENT BIT4
#define BLE_WEB_SERVICE_DONE_EVENT BIT5

#define BLE_CHAR_DONE_EVENT BIT6
#define BLE_WEB_CHAR_DONE_EVENT BIT7

#define BLE_WAY_DONE_EVENT BIT8
#define BLE_WEB_WAY_DONE_EVENT BIT9

extern char * ble_scan_result_json;
extern char * ble_char_result_json;
extern char * ble_way_result_json;
extern EventGroupHandle_t ble_event_group;

struct BleDeviceTypeDef
{
    uint8_t id;
    esp_bd_addr_t bda;                          /*!< Bluetooth device address which has been searched */
    int rssi;                                   /*!< Searched device's RSSI */
    char *adv_data;
	struct BleDeviceTypeDef* next;
};
struct BleCharTypeDef
{
    esp_gatt_id_t   id;
    esp_gatt_char_prop_t prop;
    struct BleCharTypeDef* next;
};
struct BleServiceTypeDef
{
    bool is_primary;
    esp_gatt_id_t   id;
    struct BleCharTypeDef* character;
    struct BleServiceTypeDef* next;
};


#endif