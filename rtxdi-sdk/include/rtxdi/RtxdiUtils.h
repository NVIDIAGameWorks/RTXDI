#pragma once

#include <stdint.h>

#include <rtxdi/RtxdiParameters.h>

namespace rtxdi
{

// Checkerboard sampling modes match those used in NRD, based on frameIndex:
// Even frame(0)  Odd frame(1)   ...
//     B W             W B
//     W B             B W
// BLACK and WHITE modes define cells with VALID data
enum class CheckerboardMode : uint32_t
{
    Off = 0,
    Black = 1,
    White = 2
};

RTXDI_DIReservoirBufferParameters CalculateReservoirBufferParameters(uint32_t renderWidth, uint32_t renderHeight, CheckerboardMode checkerboardMode);

void ComputePdfTextureSize(uint32_t maxItems, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMipLevels);

void FillNeighborOffsetBuffer(uint8_t* buffer, uint32_t neighborOffsetCount);

// 32 bit Jenkins hash
uint32_t JenkinsHash(uint32_t a);

}