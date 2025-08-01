#include <WiFi.h>

#include <string>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "SafeString.h"
#include "PSKReporter.h"

// Live server
#define PSK_REPORTER_HOSTNAME "report.pskreporter.info"
#define PSK_REPORTER_ADDRESS "74.116.41.13"
#define PSK_REPORTER_PORT 4739
#define PSK_REPORTER_TEST_PORT 14739

static const int datagramSize = 16 * 60;
s
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
      flowTimeSeconds(time(0))
{
}

size_t ReceivedRecord::recordSize() const
{
    return (sizeof(uint8_t) + callsign.length()) +
           sizeof(uint32_t) +
           sizeof(uint8_t) +
           (sizeof(uint8_t) + mode.length()) +
           sizeof(uint8_t) +
           sizeof(uint32_t);
}

void ReceivedRecord::encode(uint8_t *buf) const
{
    // Callsign
    buf = writeLengthPrefixedString(buf, callsign);

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
}

PskReporter::PskReporter(const uint8_t *encodedBuf, bool isTestMode) : testMode(isTestMode),
                                                                       currentSequenceNumber(0),
                                                                       randomIdentifier(static_cast<uint32_t>(time(0)) ^ static_cast<uint32_t>(getpid()))
{
    encodedBuf = readLengthPrefixedString(encodedBuf, reporterCallsign);
    encodedBuf = readLengthPrefixedString(encodedBuf, reporterGridSquare);
    readLengthPrefixedString(encodedBuf, decodingSoftware);
}

PskReporter::~PskReporter()
{
    recordList.clear();
}

void PskReporter::addReceivedRecord(const uint8_t *encodedBuf)
{
    SafeString callsign;
    encodedBuf = readLengthPrefixedString(encodedBuf, callsign);

    uint32_t frequency = *(const uint32_t *)encodedBuf;
    encodedBuf += sizeof(uint32_t);

    uint8_t snr = *encodedBuf++;
    recordList.push_back(ReceivedRecord(callsign, frequency, snr));
}

bool PskReporter::send()
{
    WiFiUDP wifiUdp;

    size_t dataSize = datagramSize;
    SafeString packet(datagramSize);
    if (packet.c_str() == NULL)
        return false; // Memory allocation failed

    // Encode packet header and fields
    uint8_t *start = (uint8_t *)packet.get();
    uint8_t *p = start;
    *p++ = 0x00;
    *p++ = 0x0A;
    dataSize -= 2 * sizeof(uint8_t);

    *((uint16_t *)p) = htons(datagramSize);
    p += sizeof(uint16_t);
    dataSize -= sizeof(uint16_t);
    *((uint32_t *)p) = htonl(time(0));
    dataSize -= sizeof(uint32_t);
    p += sizeof(uint32_t);
    dataSize -= sizeof(uint32_t);
    *((uint32_t *)p) = htonl(currentSequenceNumber++);
    p += sizeof(uint32_t);
    dataSize -= sizeof(uint32_t);
    *((uint32_t *)p) = htonl(randomIdentifier);
    p += sizeof(uint32_t);
    dataSize -= sizeof(uint32_t);

    memcpy(p, rxFormatHeader, sizeof(rxFormatHeader));
    p += sizeof(rxFormatHeader);
    dataSize -= sizeof(rxFormatHeader);

    if (dataSize > 0)
    {
        size_t headerSize = sizeof(txFormatHeader);
        memcpy(p, txFormatHeader, headerSize);
        p += headerSize;
        dataSize -= headerSize;
    }

    uint8_t newp = encodeReporterRecord(p);
    dataSize -= p - newp;
    encodeReceivedRecords(p, dataSize);

    if (testMode)
    {
        wifiUdp.beginPacket(PSK_REPORTER_HOSTNAME, PSK_REPORTER_TEST_PORT);
    }
    else
    {
        wifiUdp.beginPacket(PSK_REPORTER_HOSTNAME, PSK_REPORTER_PORT);
    }
    size_t written = wifiUdp.write((const uint8_t *)packet.c_str(), datagramSize);
    wifiUdp.endPacket();
    ++currentSequenceNumber;
    return written == datagramSize;
}

inline size_t pad4(size_t size)
{
    constexpr size_t align = 4;
    return (size % align) ? (size + (align - (size % align))) : size;
}

inline uint8_t *pad4ptr(size_t size, uint 8_t * ptr)
{
    constexpr size_t align = 4;
    return ptr + (size % align) ? (size + (align - (size % align))) : size;
}

size_t PskReporter::getRxDataSize()
{
    size_t size = sizeof(uint32_t) +
                  (sizeof(uint8_t) + reporterCallsign.length()) +
                  (sizeof(uint8_t) + reporterGridSquare.length()) +
                  (sizeof(uint8_t) + decodingSoftware.length());
    return pad4(size);
}

size_t PskReporter::getTxDataSize(size_t remainingSize)
{
    if (recordList.empty())
        return 0;

    size_t size = sizeof(uint32_t);
    for (auto &item : recordList)
    {
        size_t recordSize = item.recordSize();
        remainingSize -= recordSize;
        if (remainingSize < 0)
            break;

        size += recordSize;
    }
    return pad4(size);
}

uint8_t *PskReporter::encodeReporterRecord(uint8_t *buf)
{
    const uint8_t *start = buf;
    *buf++ = 0x99;
    *buf++ = 0x92;
    *((uint16_t *)buf) = htons(getRxDataSize());
    buf += sizeof(uint16_t);

    buf = writeLengthPrefixedString(buf, reporterCallsign);
    buf = writeLengthPrefixedString(buf, reporterGridSquare);
    buf = writeLengthPrefixedString(buf, decodingSoftware);
    return pad4ptr(buf - start, buf)
}

void PskReporter::encodeReceivedRecords(uint8_t *buf, int remainingSize)
{
    if (recordList.empty())
        return;

    *buf++ = 0x99;
    *buf++ = 0x93;
    remainingSize -= 2 * sizeof(uint8_t);

    *((uint16_t *)buf) = htons(getTxDataSize(remainingSize));
    buf += sizeof(uint16_t);
    remainingSize -= sizeof(uint16_t);

    while (!recordList.empty())
    {
        auto rec = recordList.front();
        size_t recordSize = rec.recordSize();
        remainingSize -= recordSize;
        if (remainingSize > 0)
        {
            rec.encode(buf);
            recordList.pop_front();
            buf += recordSize;
        }
        else
            break;
    }
}
