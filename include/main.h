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

extern bool syncTime;

void processTimeRequest(const RTCTime *rtcTime);
void processSenderRecord(const uint8_t *buffer);
void processReceiverRecord(const uint8_t *buffer);
void processSendRequest();
