/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include <vector>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <WiFi.h>

#include "SafeString.h"
#include "PSKReporter.h"
#include "main.h"

// Live server
constexpr auto PSK_REPORTER_HOSTNAME = "report.pskreporter.info";
constexpr auto PSK_REPORTER_PORT = 4739;
constexpr auto PSK_REPORTER_TEST_PORT = 14739;
constexpr auto PSK_MAX_RECORDS = 40;   // must be less than the max datagram size;
constexpr auto MAX_BUFFER_SIZE = 1471; // must be less than the max datagram size;

// RX record:
/* For receiver callsign, receiver locator, decoding software use */

static const uint8_t rxFormatHeader[] = {
    0x00, 0x03, 0x00, 0x24, 0x99, 0x92, 0x00, 0x03, 0x00, 0x00,
    0x80, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x04, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x00, 0x00};

// TX record:
/* For sender callsign, frequency, SNR (1 byte), mode, information source (1 byte), flow start seconds use */

static const uint8_t txFormatHeader[] = {
    0x00, 0x02, 0x00, 0x34, 0x99, 0x93, 0x00, 0x06,
    0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x05, 0x00, 0x04, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0B, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
    0x00, 0x96, 0x00, 0x04};

// Helper to write a length-prefixed string to a buffer
static uint8_t *writeLengthPrefixedString(uint8_t *buf, const SafeString &str)
{
    size_t length = str.length();
    *buf++ = (uint8_t)length;
    memcpy(buf, str.c_str(), length);
    return buf + length;
}

// Helper to read a length-prefixed string from a buffer
static const uint8_t *readLengthPrefixedString(const uint8_t *buf, SafeString &str)
{
    uint8_t length = *buf++;
    // Create a SafeString from a non-null-terminated buffer and length
    str = SafeString(reinterpret_cast<const char *>(buf), length);
    return buf + length;
}

ReceivedRecord::ReceivedRecord() : frequency(0), snr(0), infoSource(0), flowTimeSeconds(0)
{
}

ReceivedRecord::ReceivedRecord(const SafeString &callsign, uint32_t frequency, uint8_t snr)
    : callsign(callsign),
      frequency(frequency),
      snr(snr),
      mode("FT8"),
      infoSource(1),
      flowTimeSeconds((uint32_t)time(0))
{
}

size_t ReceivedRecord::encode(uint8_t *bufIn) const
{
    // Callsign
    uint8_t *buf = writeLengthPrefixedString(bufIn, callsign);

    // Frequency (network byte order)
    *((uint32_t *)buf) = htonl(frequency);
    buf += sizeof(uint32_t);

    // SNR
    *buf++ = snr;

    // Mode
    buf = writeLengthPrefixedString(buf, mode);

    // Info source
    *buf++ = infoSource;

    // Flow start time (network byte order)
    *((uint32_t *)buf) = htonl(flowTimeSeconds);
    buf += sizeof(uint32_t);
    return buf - bufIn;
}

PskReporter::PskReporter(uint32_t randomIdentifierIn, bool testModeIn) : currentSequenceNumber(0),
                                                                         testMode(testModeIn),
                                                                         randomIdentifier(randomIdentifierIn)
{
}

bool PskReporter::createSenderRecord(const uint8_t *encodedBuf)
{
    if (!encodedBuf)
        return false;

    encodedBuf = readLengthPrefixedString(encodedBuf, reporterCallsign);
    encodedBuf = readLengthPrefixedString(encodedBuf, reporterGridSquare);

    return true;
}

bool PskReporter::createSenderSoftwareRecord(const uint8_t *encodedBuf)
{
    if (!encodedBuf)
        return false;

    encodedBuf = readLengthPrefixedString(encodedBuf, decodingSoftware);

    return true;
}

PskReporter::~PskReporter()
{
    recordList.clear();
}

bool PskReporter::alreadyLogged(const SafeString &callsign) const
{
    bool result = false;
    for (auto &item : recordList)
    {
        if (item.callsign == callsign)
        {
            result = true;
            break;
        }
    }
    return result;
}

bool PskReporter::addReceivedRecord(const uint8_t *encodedBuf)
{
    if (!encodedBuf)
        return false;

    SafeString callsign;
    encodedBuf = readLengthPrefixedString(encodedBuf, callsign);

    uint32_t frequency = *(const uint32_t *)encodedBuf;
    encodedBuf += sizeof(uint32_t);

    uint8_t snr = *encodedBuf++;

    if (recordList.size() < PSK_MAX_RECORDS && !alreadyLogged(callsign))
    {
        recordList.push_back(ReceivedRecord(callsign, frequency, snr));
        return true;
    }
    return false;
}

bool PskReporter::send()
{
    size_t written = 0;
    if (recordList.size() > 0 && WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA)
    {
        WiFiUDP wifiUdp;

        SafeString packet(MAX_BUFFER_SIZE);
        if (packet.c_str() == NULL)
            return false; // Memory allocation failed

        // Encode packet header and fields
        uint8_t *ptrStart = (uint8_t *)packet.get();
        uint8_t *p = ptrStart;
        *p++ = 0x00;
        *p++ = 0x0A;
        p += sizeof(uint16_t);
        *((uint32_t *)p) = htonl((uint32_t)time(0));
        p += sizeof(uint32_t);
        *((uint32_t *)p) = htonl(currentSequenceNumber++);
        p += sizeof(uint32_t);
        *((uint32_t *)p) = htonl(randomIdentifier);
        p += sizeof(uint32_t);

        memcpy(p, rxFormatHeader, sizeof(rxFormatHeader));
        p += sizeof(rxFormatHeader);

        if (!recordList.empty())
        {
            memcpy(p, txFormatHeader, sizeof(txFormatHeader));
            p += sizeof(txFormatHeader);
        }

        size_t size = encodeReporterRecord(p);
        p += size;
        size = encodeReceivedRecords(p);
        p += size;
        recordList.clear();

        if (testMode)
        {
            wifiUdp.beginPacket(PSK_REPORTER_HOSTNAME, PSK_REPORTER_TEST_PORT);
        }
        else
        {
            wifiUdp.beginPacket(PSK_REPORTER_HOSTNAME, PSK_REPORTER_PORT);
        }

        size = p - ptrStart;
        p = ptrStart + 2;
        *((uint16_t *)p) = htons((uint16_t)size);

        written = wifiUdp.write((const uint8_t *)packet.c_str(), size);
        wifiUdp.endPacket();
        wifiUdp.stop();
    }
    return written;
}

inline static size_t pad4(size_t size)
{
    return (size + 3) & 0xfffffffcU;
}

size_t PskReporter::encodeReporterRecord(uint8_t *bufStart) const
{
    uint8_t *buf = bufStart;
    *buf++ = 0x99;
    *buf++ = 0x92;
    // room for the size
    buf += sizeof(uint16_t);

    buf = writeLengthPrefixedString(buf, reporterCallsign);
    buf = writeLengthPrefixedString(buf, reporterGridSquare);
    buf = writeLengthPrefixedString(buf, decodingSoftware);

    size_t size = buf - bufStart;
    size = pad4(size);

    buf = bufStart + 2;
    *((uint16_t *)buf) = htons((uint16_t)size);
    return size;
}

size_t PskReporter::encodeReceivedRecords(uint8_t *bufStart)
{
    uint8_t *buf = bufStart;
    if (recordList.empty())
        return 0;

    *buf++ = 0x99;
    *buf++ = 0x93;
    // room for the size
    buf += sizeof(uint16_t);

    size_t size = 4;

    for (auto &rec : recordList)
    {
        size_t bufSize = rec.encode(buf);
        size += bufSize;
        buf += bufSize;
    }

    size = pad4(size);

    buf = bufStart + 2;
    *((uint16_t *)buf) = htons((uint16_t)size);
    return size;
}
