#ifndef RTXDI_UNIFORM_SAMPLING
#define RTXDI_UNIFORM_SAMPLING

void RTXDI_RandomlySelectLightUniformly(
    inout RAB_RandomSamplerState rng,
    RTXDI_LightsBufferRegion region,
    out RAB_LightInfo lightInfo,
    out uint lightIndex,
    out float invSourcePdf)
{
    float rnd = RAB_GetNextRandom(rng);
    invSourcePdf = float(region.numLights);
    lightIndex = region.firstLightIndex + min(uint(floor(rnd * region.numLights)), region.numLights - 1);
    lightInfo = RAB_LoadLightInfo(lightIndex, false);
}

#endif // RTXDI_UNIFORM_SAMPLING