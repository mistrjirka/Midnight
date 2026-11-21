/* PTP byte stream deframer for the ESP32-S3 USB host.
 * Handles partial and coalesced USB bulk packets. No Arduino dependencies.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

struct PtpContainer {
    uint16_t type = 0, code = 0;
    uint32_t transaction = 0;
    size_t payloadSize = 0;
    uint8_t payload[244] = {};
};

class PtpWireReader {
public:
    static constexpr size_t MAX_FRAME = 256;
    enum Result { NEED_MORE, FRAME_READY, BAD_FRAME };
    void reset();
    bool feed(const uint8_t *bytes, size_t count);
    Result next(PtpContainer &out);
    size_t buffered() const {return filled_;}
private:
    uint8_t buffer_[MAX_FRAME] = {};
    size_t filled_ = 0;
};
