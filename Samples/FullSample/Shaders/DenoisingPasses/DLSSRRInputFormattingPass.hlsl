#include "SharedShaderInclude/ShaderParameters.h"

#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/packing.hlsli>

VK_PUSH_CONSTANT ConstantBuffer<DLSSRRInputFormattingConstants> g_Const : register(b0);

Texture2D<uint> t_PSRDiffuseAlbedo : register(t0);
Texture2D<uint> t_PSRSpecularF0 : register(t1);
RWTexture2D<float3> u_PSRDiffuseAlbedo_RR : register(u0);
RWTexture2D<float3> u_PSRSpecularF0_RR : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
    if (any(globalIdx.xy >= g_Const.viewportSize))
        return;

    u_PSRDiffuseAlbedo_RR[globalIdx] = Unpack_R11G11B10_UFLOAT(t_PSRDiffuseAlbedo[globalIdx]);
    u_PSRSpecularF0_RR[globalIdx] = Unpack_R11G11B10_UFLOAT(t_PSRSpecularF0[globalIdx]);
}