
#include <WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <ESP32Time.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "main.h"
#include "workqueue.h"
#include "SafeString.h"
#include "PSKReporter.h"

static const uint8_t RTC_I2C_ADDRESS = 0x2A;
static TaskHandle_t networkTaskHandle = 0;
static RTCTime rtcTime = {0};
static PskReporter *pskReporter = NULL;

// forward reference
static void NetworkTask(void *parameter);

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

        case OP_RECEIVER_RECORD:
            for (idx = 0; Wire.available() && (idx < sizeof(buffer)); ++idx)
            {
                buffer[idx] = Wire.read();
            }
            if (idx > 0)
                addWorkQueueItem(OP_RECEIVER_RECORD, buffer, idx);
            break;

        case OP_SEND_REQUEST:
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

// Processing functions - all called on the main threads
void processTimeRequest(const RTCTime *pRtcTime)
{
    memcpy(&rtcTime, pRtcTime, sizeof(rtcTime));
}

void processSenderRecord(const uint8_t *buffer)
{
    if (pskReporter != NULL)
        delete pskReporter;
    pskReporter = new PskReporter(buffer);
}

void processReceiverRecord(const uint8_t *buffer)
{
    if (pskReporter != NULL)
        pskReporter->addReceivedRecord(buffer);
}

void processSendRequest()
{
    if (pskReporter != NULL)
    {
        pskReporter->send();
        delete pskReporter;
    }
    pskReporter = NULL;
}

// Arduino-style API
void setup()
{
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_18_5dBm);
    WiFi.disconnect();

    Serial.println("WifiTimeSync started");

    initialiseWorkQueue();

    xTaskCreate(NetworkTask, "NetworkTask", 16384, NULL, 1, &networkTaskHandle);

    Wire.begin(RTC_I2C_ADDRESS);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(requestEvent);
}

void loop()
{
    processWorkQueue();
}

// Network thread
static void NetworkTask(void *parameter)
{
    for (;;)
    {
        int n = WiFi.scanNetworks();
        if (n == 0)
        {
            Serial.println("no networks found");
            delay(1000);
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
        delay(100);

        WiFiManager mgr;
        bool res = mgr.autoConnect("DX_FT8_Xceiver", "");
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

            WiFiUDP ntpUDP;
            NTPClient timeClient(ntpUDP);
            timeClient.begin();
            if (timeClient.update())
            {
                char buffer[256];
                ESP32Time rtc(0);
                rtc.setTime(timeClient.getEpochTime());

                Serial.println(rtc.getDateTime(true));

                RTCTime rtcTime;
                rtcTime.seconds = rtc.getSecond();
                rtcTime.minutes = rtc.getMinute();
                rtcTime.hours = rtc.getHour(true);
                rtcTime.dayOfWeek = rtc.getDayofWeek();
                rtcTime.day = rtc.getDay();
                rtcTime.month = rtc.getMonth() + 1;
                rtcTime.year = rtc.getYear() - 2000;

                addWorkQueueItem(OP_TIME_REQUEST, (const uint8_t *)&rtcTime, sizeof(rtcTime));
            }
            timeClient.end();
        }
    }
    vTaskDelete(NULL);
}
