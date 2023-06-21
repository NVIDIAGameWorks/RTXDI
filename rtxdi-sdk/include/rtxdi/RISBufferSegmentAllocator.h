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
