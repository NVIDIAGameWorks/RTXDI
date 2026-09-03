#ifndef RTXDI_RAB_RANDOM_SAMPLER_STATE_HLSLI
#define RTXDI_RAB_RANDOM_SAMPLER_STATE_HLSLI

struct RTXDI_RandomSamplerState
{
    uint unused;
};

RTXDI_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint pass)
{
    RTXDI_RandomSamplerState rng;
    return rng;
}

float RAB_GetNextRandom(inout RTXDI_RandomSamplerState rng)
{
    return 0.0;
}

#endif // RTXDI_RAB_RANDOM_SAMPLER_STATE_HLSLI
