#ifndef RAB_SPATIAL_HELPERS_HLSLI
#define RAB_SPATIAL_HELPERS_HLSLI

// This function is called in the spatial resampling passes to make sure that 
// the samples actually land on the screen and not outside of its boundaries.
// It can clamp the position or reflect it about the nearest screen edge.
// The simplest implementation will just return the input pixelPosition.
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool prevFrame)
{
    return clamp(pixelPosition, 0, int2(g_Const.view.viewportSize) - 1);
}

bool RAB_ValidateGISampleWithJacobian(inout float jacobian)
{
    return true;
}

#endif // RAB_SPATIAL_HELPERS_HLSLI
