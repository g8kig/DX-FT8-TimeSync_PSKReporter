/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <ESP32Time.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_wifi.h>

#include "main.h"
#include "workqueue.h"
#include "SafeString.h"
#include "PSKReporter.h"

static const uint8_t RTC_I2C_ADDRESS = 0x2A;
static const uint8_t BUTTON_PIN_C3 = 9;
static const uint8_t BUTTON_PIN_S2 = 0;
static TaskHandle_t timeTaskHandle = 0;
static TaskHandle_t wifiTaskHandle = 0;
static RTCTime rtcTime = {0};
static ESP32Time rtc(0);
static volatile bool timeIsValid = false;
static uint32_t sequenceNumber = 0;

// forward references
static void TimeTask(void *parameter);
static void WiFiTask(void *parameter);
static void WiFiProcessing();

#ifdef TESTING
static TaskHandle_t testTaskHandle = 0;
static void TestTask(void *parameter);
static const bool testMode = true;
static int8_t lastStateC3 = HIGH;
static int8_t lastStateS2 = HIGH;
static bool testTaskRunning = false;
#else
static const bool testMode = false;
#endif

// I2C slave API
static void receiveEvent(int length)
{
    if (length > 0 && Wire.available() > 0)
    {
        uint8_t buffer[31] = {0};
        int idx = 0;
        uint8_t operation = Wire.read();
        switch (operation)
        {
        case OP_TIME_REQUEST:
            // Nothing to do - see request event
            return;

        case OP_SENDER_RECORD:
            for (idx = 0; Wire.available() && (idx < sizeof(buffer)); ++idx)
            {
                buffer[idx] = Wire.read();
            }
            if (idx > 0)
                addWorkQueueItem(OP_SENDER_RECORD, buffer, idx);
            break;

        case OP_SENDER_SOFTWARE_RECORD:
            for (idx = 0; Wire.available() && (idx < sizeof(buffer)); ++idx)
            {
                buffer[idx] = Wire.read();
            }
            if (idx > 0)
                addWorkQueueItem(OP_SENDER_SOFTWARE_RECORD, buffer, idx);
            break;

        case OP_RECEIVER_RECORD:
            for (idx = 0; Wire.available() && (idx < sizeof(buffer)); ++idx)
            {
                buffer[idx] = Wire.read();
            }
            if (idx > 0)
                addWorkQueueItem(OP_RECEIVER_RECORD, buffer, idx);
            break;

        case OP_SEND_REQUEST:
            addWorkQueueItem(OP_SEND_REQUEST, NULL, 0);
            break;
        }
        while (Wire.available())
            Wire.read();
    }
}

// I2C slave API
static void requestEvent()
{
    Wire.write((const uint8_t *)&rtcTime, sizeof(rtcTime));
}

// Processing functions - all called on the main thread
void processTimeRequest(const RTCTime *pRtcTime)
{
    memcpy(&rtcTime, pRtcTime, sizeof(rtcTime));
}

static uint32_t crc32(uint8_t *message, size_t messageSize)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t idx = 0; idx < messageSize; ++idx)
    {
        crc ^= message[idx];
        for (int j = 7; j >= 0; j--)
        {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

static uint32_t readMacAddress()
{
    uint8_t baseMac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, baseMac) == ESP_OK)
    {
        return crc32(baseMac, sizeof(baseMac));
    }
    return 0;
}

static PskReporter &getPskReporter()
{
    static PskReporter pskReporter(readMacAddress(), testMode);
    return pskReporter;
}

void processSenderRecord(const uint8_t *buffer)
{
    getPskReporter().createSenderRecord(buffer);
}

void processSenderSoftwareRecord(const uint8_t *buffer)
{
    getPskReporter().createSenderSoftwareRecord(buffer);
}

void processReceiverRecord(const uint8_t *buffer)
{
    getPskReporter().addReceivedRecord(buffer);
}

void processSendRequest()
{
    getPskReporter().send();
}

#ifdef TESTIING
static void startTestTask()
{
    if (testTaskRunning)
    {
        Serial.println("TestTask already running, skipping");
    }
    else
    {
        xTaskCreate(TestTask, "TestTask", 8192, NULL, 1, &testTaskHandle);
    }
}
#endif

void setup()
{
    Serial.begin(115200);
#ifdef TESTIING
    pinMode(BUTTON_PIN_C3, INPUT_PULLUP);
    pinMode(BUTTON_PIN_S2, INPUT_PULLUP);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_18_5dBm);
    WiFi.disconnect();

    Serial.println("WifiTimeSync started");
    initialiseWorkQueue();

    WiFiProcessing();
    xTaskCreate(WiFiTask, "WiFiTask", 16384, NULL, 1, &wifiTaskHandle);
    xTaskCreate(TimeTask, "TimeTask", 16384, NULL, 1, &timeTaskHandle);

    Wire.begin(RTC_I2C_ADDRESS);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);
}

void loop()
{
    static unsigned long halfSecondCall = 0;
    static unsigned long fiveMinuteCall = 0;
    unsigned long now = millis();

    if (now - halfSecondCall >= 500) // 1/2 second
    {
        halfSecondCall = now;
        if (timeIsValid)
        {
            rtcTime.seconds = rtc.getSecond();
            rtcTime.minutes = rtc.getMinute();
            rtcTime.hours = rtc.getHour(true);
            rtcTime.dayOfWeek = rtc.getDayofWeek();
            rtcTime.day = rtc.getDay();
            rtcTime.month = rtc.getMonth() + 1;
            rtcTime.year = rtc.getYear() - 2000;
        }
        else
        {
            memset(&rtcTime, 0, sizeof(rtcTime));
        }
    }

    if (now - fiveMinuteCall >= 5 * 60 * 1000) // 5 minutes
    {
        fiveMinuteCall = now;
        addWorkQueueItem(OP_SEND_REQUEST, NULL, 0);
    }

    processWorkQueue();

#ifdef TESTING
    // debugging
    int8_t currentStateC3 = digitalRead(BUTTON_PIN_C3);
    if (lastStateC3 != currentStateC3 && currentStateC3 == LOW)
    {
        startTestTask();
    }
    lastStateC3 = currentStateC3;

    int8_t currentStateS2 = digitalRead(BUTTON_PIN_S2);
    if (lastStateS2 != currentStateS2 && currentStateS2 == LOW)
    {
        startTestTask();
    }
    lastStateS2 = currentStateS2;
#endif
}

static void WiFiProcessing()
{
    int n = WiFi.scanNetworks();
    if (n == 0)
    {
        Serial.println("no networks found");
    }
    else
    {
        Serial.print(n);
        Serial.println(" networks found");
        Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
        for (int i = 0; i < n; ++i)
        {
            Serial.printf("%2d", i + 1);
            Serial.print(" | ");
            Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
            Serial.print(" | ");
            Serial.printf("%4d", WiFi.RSSI(i));
            Serial.print(" | ");
            Serial.printf("%2d", WiFi.channel(i));
            Serial.print(" | ");
            switch (WiFi.encryptionType(i))
            {
            case WIFI_AUTH_OPEN:
                Serial.print("open");
                break;
            case WIFI_AUTH_WEP:
                Serial.print("WEP");
                break;
            case WIFI_AUTH_WPA_PSK:
                Serial.print("WPA");
                break;
            case WIFI_AUTH_WPA2_PSK:
                Serial.print("WPA2");
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                Serial.print("WPA+WPA2");
                break;
            case WIFI_AUTH_WPA2_ENTERPRISE:
                Serial.print("WPA2-EAP");
                break;
            case WIFI_AUTH_WPA3_PSK:
                Serial.print("WPA3");
                break;
            case WIFI_AUTH_WPA2_WPA3_PSK:
                Serial.print("WPA2+WPA3");
                break;
            case WIFI_AUTH_WAPI_PSK:
                Serial.print("WAPI");
                break;
            default:
                Serial.print("unknown");
            }
            Serial.println();
            delay(10);
        }
    }

    Serial.println();
    WiFi.scanDelete();
}

static void TimeTask(void *parameter)
{
    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA)
        {
            WiFiUDP ntpUDP;
            NTPClient timeClient(ntpUDP);
            timeClient.begin();
            if (timeClient.update())
            {
                timeIsValid = false;
                rtc.setTime(timeClient.getEpochTime());
                timeIsValid = true;
                Serial.print("Time updated: ");
                Serial.println(rtc.getDateTime(true));
            }
            timeClient.end();
            delay(120000);
        }
        else
        {
            delay(500);
        }
    }
    vTaskDelete(NULL);
}

static void WiFiTask(void *parameter)
{
    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            WiFiManager mgr;
            bool res = mgr.autoConnect("DX_FT8_Xceiver");
            if (!res)
            {
                mgr.resetSettings();
                Serial.println("Failed to connect");
                delay(1000);
            }
            else
            {
                Serial.println("WiFi connected");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());
            }
        }
        delay(30000);
    }
    vTaskDelete(NULL);
}

static size_t addSenderRecord(uint8_t *buffer, size_t bufferSizeIn, const char *callsign, const char *gridSquare)
{
    size_t result = 0;
    size_t callsignLength = strlen(callsign);
    size_t gridSquareLength = strlen(gridSquare);

    size_t bufferSize = sizeof(uint8_t) + callsignLength + sizeof(uint8_t) + gridSquareLength;
    if (bufferSize < bufferSizeIn)
    {
        uint8_t *ptr = buffer;
        // Add callsign as length-delimited
        *ptr++ = (uint8_t)callsignLength;
        memcpy(ptr, callsign, callsignLength);
        ptr += callsignLength;

        // Add gridSquare as length-delimited
        *ptr++ = (uint8_t)gridSquareLength;
        memcpy(ptr, gridSquare, gridSquareLength);
        ptr += gridSquareLength;
        result = ptr - buffer;
    }
    return result;
}

static size_t addSenderSoftwareRecord(uint8_t *buffer, size_t bufferSizeIn, const char *software)
{
    size_t result = 0;
    size_t softwareLength = strlen(software);

    size_t bufferSize = sizeof(uint8_t) + softwareLength;
    if (bufferSize < bufferSizeIn)
    {
        uint8_t *ptr = buffer;
        // Add software as length-delimited
        *ptr++ = (uint8_t)softwareLength;
        memcpy(ptr, software, softwareLength);
        ptr += softwareLength;

        result = ptr - buffer;
    }
    return result;
}

static size_t addReceivedRecord(uint8_t *buffer, size_t bufferSizeIn, const char *callsign, uint32_t frequency, uint8_t snr)
{
    size_t result = 0;
    size_t callsignLength = strlen(callsign);
    size_t bufferSize = sizeof(uint8_t) + callsignLength + sizeof(uint32_t) + sizeof(uint8_t);
    if (bufferSize < bufferSizeIn)
    {
        uint8_t *ptr = buffer;
        // Add callsign as length-delimited
        *ptr++ = (uint8_t)callsignLength;
        memcpy(ptr, callsign, callsignLength);
        ptr += (uint8_t)callsignLength;

        // Add frequency
        memcpy(ptr, &frequency, sizeof(frequency));
        ptr += sizeof(frequency);

        // Add SNR (1 byte)
        *ptr++ = snr;

        result = ptr - buffer;
    }
    return result;
}

#ifdef TESTING
static void TestTask(void *parameter)
{
    testTaskRunning = true;

    PskReporter &pskReporter = getPskReporter();
    int sequenceNumber = 0;
    Serial.println("TestTask started");
    uint8_t encodedBuff[32];
    memset(encodedBuff, 0, sizeof(encodedBuff));
    addSenderRecord(encodedBuff, sizeof(encodedBuff), "G8KIG", "IO91iq");
    pskReporter.createSenderRecord(encodedBuff);

    memset(encodedBuff, 0, sizeof(encodedBuff));
    addSenderSoftwareRecord(encodedBuff, sizeof(encodedBuff), "DX FT8 Transceiver (Test)");
    pskReporter.createSenderSoftwareRecord(encodedBuff);
    Serial.println("TestTask add received records");
    for (int idx = 0; idx < 10; ++idx)
    {
        char callsign[16];
        sprintf(callsign, "G8KIG-%u", idx);
        memset(encodedBuff, 0, sizeof(encodedBuff));
        addReceivedRecord(encodedBuff, sizeof(encodedBuff), callsign, (uint32_t)(14031 + idx), (uint8_t)(127 + (-idx)));
        pskReporter.addReceivedRecord(encodedBuff);
        delay(1000);
    }
    pskReporter.send();
    Serial.println("TestTask completed");
    testTaskRunning = false;
    vTaskDelete(NULL);
}
#endif
