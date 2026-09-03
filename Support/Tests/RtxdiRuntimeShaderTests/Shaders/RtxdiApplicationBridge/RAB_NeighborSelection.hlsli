#ifndef RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI
#define RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI

bool RAB_GetNeighborSelectionSurface(int2 pixel, out float3 worldNormal, out float3 worldPos, out float viewDepth)
{
    worldNormal = float3(0.0, 0.0, 0.0);
    worldPos = float3(0.0, 0.0, 0.0);
    viewDepth = 0.0;
    return false;
}

#endif // RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI
