/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <stdint.h>

#include "RISBufferSegmentParameters.h"

namespace rtxdi
{

class RISBufferSegmentAllocator
{
public:
    RISBufferSegmentAllocator();
    // Returns starting offset of segment in buffer
    uint32_t allocateSegment(uint32_t sizeInElements);
    uint32_t getTotalSizeInElements() const;

private:
    uint32_t m_totalSizeInElements;
};

}
