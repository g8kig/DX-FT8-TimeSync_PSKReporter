/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

 #pragma once

struct RTCTime
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t dayOfWeek;
    uint8_t day;
    uint8_t month;
    uint8_t year;
};

void processTimeRequest(const RTCTime *rtcTime);
void processSenderRecord(const uint8_t *buffer);
void processSenderSoftwareRecord(const uint8_t *buffer);
void processReceiverRecord(const uint8_t *buffer);
void processSendRequest();

const IPAddress cloudfareDns1 = IPAddress(1, 1, 1, 1);
const IPAddress cloudfareDns2 = IPAddress(1, 0, 0, 1);

