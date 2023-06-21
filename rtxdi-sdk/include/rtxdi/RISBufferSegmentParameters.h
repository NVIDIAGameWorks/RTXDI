#ifndef RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS
#define RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS

#include "RtxdiTypes.h"

struct RTXDI_RISBufferSegmentParameters
{
    uint32_t bufferOffset;
    uint32_t tileSize;
    uint32_t tileCount;
    uint32_t pad1;
};

#endif // RTXDI_RIS_BUFFER_SEGMENT_PARAMETERS