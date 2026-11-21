#pragma once
#include <stddef.h>
#include <stdint.h>
struct CameraMountMailbox {
    uint32_t commandSequence=0, targetRa=0, requestedId=0, completedId=0;
    uint32_t pointSequence=0;
    int32_t targetDec=0;
    uint16_t captureState=0;
    uint8_t command=0;
    bool targetValid=false;
};
bool parseMountMailbox(const uint8_t *buf,size_t size,CameraMountMailbox &out);
