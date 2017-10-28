#ifndef RECORD_TASK_H
#define RECORD_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define RECORD_START BIT0
#define RECORD_STOP  BIT1
#define RECORD_DONE  BIT2

extern EventGroupHandle_t record_event_group;
void record_task(void *pvParameters);


#endif