/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

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