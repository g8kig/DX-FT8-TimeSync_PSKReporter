/* Copyright (c) 2025 Paul Winwood, G8KIG - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPL-Version 3 license.
 *
 * There is a copy of the GPL-Version 3 license in the same folder as this file.
 */

#pragma once

struct ReceivedRecord
{
    SafeString callsign;
    uint32_t frequency;
    uint8_t snr;
    SafeString mode;
    uint8_t infoSource;
    int flowTimeSeconds;

    ReceivedRecord();
    ReceivedRecord(const SafeString &callsign,
                   uint32_t frequency,
                   uint8_t snr);

    ReceivedRecord &operator=(const ReceivedRecord &other) = delete;

    size_t recordSize() const;
    size_t encode(uint8_t *buf) const;
};

class PskReporter
{
public:
    PskReporter(uint32_t randomIdentifier, bool testMode = false);
    virtual ~PskReporter();

    bool createSenderRecord(const uint8_t *encodedBuf);
    bool createSenderSoftwareRecord(const uint8_t *encodedBuf);
    bool addReceivedRecord(const uint8_t *encodedBuf);
    bool send();

    PskReporter &operator=(const PskReporter &other) = delete;

private:
    uint32_t currentSequenceNumber;
    uint32_t randomIdentifier;
    bool testMode;

    SafeString reporterCallsign;
    SafeString reporterGridSquare;
    SafeString decodingSoftware;
    std::vector<ReceivedRecord> recordList;

    size_t encodeReporterRecord(uint8_t *buf) const;
    size_t encodeReceivedRecords(uint8_t *buf);
    bool alreadyLogged(const SafeString &callsign) const;
};
