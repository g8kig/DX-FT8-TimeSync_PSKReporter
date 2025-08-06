/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#pragma once

enum I2COperation
{
    OP_TIME_REQUEST = 0,
    OP_SENDER_RECORD,
    OP_SENDER_SOFTWARE_RECORD,
    OP_RECEIVER_RECORD,
    OP_SEND_REQUEST
};

static const int BUFFER_SIZE = 32;

enum WorkItemState
{
    ITEM_FREE,
    ITEM_ALLOCATED
};

struct WorkItem
{
    WorkItemState state;
    I2COperation operation;
    uint8_t buffer[BUFFER_SIZE];
};

void initialiseWorkQueue();
void addWorkQueueItem(I2COperation operation, const uint8_t *buffer, int bufferSize);
void processWorkQueue();
