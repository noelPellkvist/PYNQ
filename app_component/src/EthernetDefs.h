#pragma once
#include <cstdint>

#define ACK 0
#define START_RECORDING 1
#define RECORDED_WAVEFORM 2
#define TRAININGDATA 3
#define START_INFERNECE 4
#define RESULTS 5

#pragma pack(push, 1)
typedef struct Header
{
    uint8_t  package_type;
    uint32_t packageLength;
} Ethernet_header_t;
#pragma pack(pop)



