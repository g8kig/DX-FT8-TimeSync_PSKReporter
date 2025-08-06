/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "main.h"
#include "workqueue.h"

#define MAX_WORK_ITEMS 20

static WorkItem workItems[MAX_WORK_ITEMS];
static SemaphoreHandle_t workMutex;
static QueueHandle_t workQueue;

static int allocateWorkItem()
{
    int index = -1;
    xSemaphoreTake(workMutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WORK_ITEMS; i++)
    {
        if (workItems[i].state == ITEM_FREE)
        {
            workItems[i].state = ITEM_ALLOCATED;
            index = i;
            break;
        }
    }
    xSemaphoreGive(workMutex);
    return index;
}

void addWorkQueueItem(I2COperation operation, const uint8_t *buffer, int bufferSize)
{
    int index = allocateWorkItem();
    if (index >= 0)
    {
        Serial.printf("addWorkQueueItem(): queued %d with op. %d\n", index, operation);
        WorkItem *workItem = workItems + index;
        workItem->operation = operation;
        if (buffer != NULL && bufferSize > 0)
        {
            memcpy(workItem->buffer, buffer, bufferSize);
            memset(workItem->buffer + bufferSize, 0, sizeof(workItem->buffer) - bufferSize);
        }
        xQueueSend(workQueue, &index, portMAX_DELAY);
    }
}

void initialiseWorkQueue()
{
    workQueue = xQueueCreate(MAX_WORK_ITEMS, sizeof(int));
    workMutex = xSemaphoreCreateMutex();
    memset(workItems, 0, sizeof(workItems));
}

void processWorkQueue()
{
    int index = 0;
    if (xQueueReceive(workQueue, &index, 0) == pdPASS)
    {
        xSemaphoreTake(workMutex, portMAX_DELAY);
        WorkItem *workItem = workItems + index;
        I2COperation operation = workItem->operation;
        switch (operation)
        {
        case OP_TIME_REQUEST:
            processTimeRequest((const RTCTime *)workItem->buffer);
            break;
        case OP_SENDER_RECORD:
            processSenderRecord(workItem->buffer);
            break;
        case OP_SENDER_SOFTWARE_RECORD:
            processSenderSoftwareRecord(workItem->buffer);
            break;
        case OP_RECEIVER_RECORD:
            processReceiverRecord(workItem->buffer);
            break;
        case OP_SEND_REQUEST:
            processSendRequest();
            break;
        }
        workItem->state = ITEM_FREE;
        xSemaphoreGive(workMutex);
        Serial.printf("processWorkQueue(): processed %d with op. %d\n", index, operation);
    }
}
