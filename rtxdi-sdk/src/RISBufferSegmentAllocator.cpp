#include "rtxdi/RISBufferSegmentAllocator.h"

namespace rtxdi
{

RISBufferSegmentAllocator::RISBufferSegmentAllocator() :
    m_totalSizeInElements(0)
{

}

uint32_t RISBufferSegmentAllocator::allocateSegment(uint32_t sizeInElements)
{
    uint32_t prevSize = m_totalSizeInElements;
    m_totalSizeInElements += sizeInElements;
    return prevSize;
}

uint32_t RISBufferSegmentAllocator::getTotalSizeInElements() const
{
    return m_totalSizeInElements;
}

}