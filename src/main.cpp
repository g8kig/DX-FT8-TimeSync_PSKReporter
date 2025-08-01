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
static TaskHandle_t timeTaskHandle = 0;
static TaskHandle_t wifiTaskHandle = 0;
static RTCTime rtcTime = {0};
static PskReporter *pskReporter = NULL;
static ESP32Time rtc(0);
static volatile bool timeIsValid = false;

// forward references
static void TimeTask(void *parameter);
static void WiFiTask(void *parameter);
static void WiFiProcessing();

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

void processSenderRecord(const uint8_t *buffer)
{
    if (pskReporter != NULL)
        delete pskReporter;
    pskReporter = new PskReporter(buffer, false);
}

void processReceiverRecord(const uint8_t *buffer)
{
    if (pskReporter != NULL)
        pskReporter->addReceivedRecord(buffer);
}

void processSendRequest()
{
    if (pskReporter != NULL)
        pskReporter->send();
}

static void readMacAddress()
{
    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK)
    {
        Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                      baseMac[0], baseMac[1], baseMac[2],
                      baseMac[3], baseMac[4], baseMac[5]);
    }
    else
    {
        Serial.println("Failed to read MAC address");
    }
}

void setup()
{
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_18_5dBm);
    WiFi.disconnect();

    Serial.println("WifiTimeSync started");
    readMacAddress();
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
        if (WiFi.status() == WL_CONNECTED)
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
            delay(30000);
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