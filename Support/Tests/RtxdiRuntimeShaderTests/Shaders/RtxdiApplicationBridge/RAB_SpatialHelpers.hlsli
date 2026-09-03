#ifndef RAB_SPATIAL_HELPERS_HLSLI
#define RAB_SPATIAL_HELPERS_HLSLI

int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool prevFrame)
{
    return int2(0, 0);
}

bool RAB_ValidateGISampleWithJacobian(inout float jacobian)
{
    return true;
}

#endif // RAB_SPATIAL_HELPERS_HLSLI
