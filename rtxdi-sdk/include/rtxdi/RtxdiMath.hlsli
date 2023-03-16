/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_MATH_HLSLI
#define RTXDI_MATH_HLSLI

static const float RTXDI_PI = 3.1415926535;

// Compares two values and returns true if their relative difference is lower than the threshold.
// Zero or negative threshold makes test always succeed, not fail.
bool RTXDI_CompareRelativeDifference(float reference, float candidate, float threshold)
{
    return (threshold <= 0) || abs(reference - candidate) <= threshold * max(reference, candidate);
}

// See if we will reuse this neighbor or history sample using
//    edge-stopping functions (e.g., per a bilateral filter).
bool RTXDI_IsValidNeighbor(float3 ourNorm, float3 theirNorm, float ourDepth, float theirDepth, float normalThreshold, float depthThreshold)
{
    return (dot(theirNorm.xyz, ourNorm.xyz) >= normalThreshold)
        && RTXDI_CompareRelativeDifference(ourDepth, theirDepth, depthThreshold);
}

// "Explodes" an integer, i.e. inserts a 0 between each bit.  Takes inputs up to 16 bit wide.
//      For example, 0b11111111 -> 0b1010101010101010
uint RTXDI_IntegerExplode(uint x)
{
    x = (x | (x << 8)) & 0x00FF00FF;
    x = (x | (x << 4)) & 0x0F0F0F0F;
    x = (x | (x << 2)) & 0x33333333;
    x = (x | (x << 1)) & 0x55555555;
    return x;
}

// Reverse of RTXDI_IntegerExplode, i.e. takes every other bit in the integer and compresses
// those bits into a dense bit firld. Takes 32-bit inputs, produces 16-bit outputs.
//    For example, 0b'abcdefgh' -> 0b'0000bdfh'
uint RTXDI_IntegerCompact(uint x)
{
    x = (x & 0x11111111) | ((x & 0x44444444) >> 1);
    x = (x & 0x03030303) | ((x & 0x30303030) >> 2);
    x = (x & 0x000F000F) | ((x & 0x0F000F00) >> 4);
    x = (x & 0x000000FF) | ((x & 0x00FF0000) >> 8);
    return x;
}

// Converts a 2D position to a linear index following a Z-curve pattern.
uint RTXDI_ZCurveToLinearIndex(uint2 xy)
{
    return RTXDI_IntegerExplode(xy[0]) | (RTXDI_IntegerExplode(xy[1]) << 1);
}

// Converts a linear to a 2D position following a Z-curve pattern.
uint2 RTXDI_LinearIndexToZCurve(uint index)
{
    return uint2(
        RTXDI_IntegerCompact(index),
        RTXDI_IntegerCompact(index >> 1));
}

// 32 bit Jenkins hash
uint RTXDI_JenkinsHash(uint a)
{
    // http://burtleburtle.net/bob/hash/integer.html
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

void RTXDI_CartesianToSpherical(float3 cartesian, out float r, out float azimuth, out float elevation)
{
    r = length(cartesian);
    cartesian /= r;

    azimuth = atan2(cartesian.z, cartesian.x);
    elevation = asin(cartesian.y);
}

float3 RTXDI_SphericalToCartesian(float r, float azimuth, float elevation)
{
    float sinAz, cosAz, sinEl, cosEl;
    sincos(azimuth, sinAz, cosAz);
    sincos(elevation, sinEl, cosEl);

    float x = r * cosAz * cosEl;
    float y = r * sinEl;
    float z = r * sinAz * cosEl;

    return float3(x, y, z);
}

// Computes a multiplier to the effective sample count (M) for pairwise MIS
float RTXDI_MFactor(float q0, float q1)
{
    return (q0 <= 0.0f)
        ? 1.0f
        : clamp(pow(min(q1 / q0, 1.0f), 8.0f), 0.0f, 1.0f);
}

// Compute the pairwise MIS weight
float RTXDI_PairwiseMisWeight(float w0, float w1, float M0, float M1)
{
    // Using a balance heuristic
    float balanceDenom = (M0 * w0 + M1 * w1);
    return (balanceDenom <= 0.0f) ? 0.0f : max(0.0f, M0 * w0) / balanceDenom;
}

// return the luminance of the given RGB color
float RTXDI_Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// Unpack two 16-bit snorm values from the lo/hi bits of a dword.
//  - packed: Two 16-bit snorm in low/high bits.
//  - returns: Two float values in [-1,1].
float2 RTXDI_UnpackSnorm2x16(uint packed)
{
    int2 bits = int2(packed << 16, packed) >> 16;
    float2 unpacked = max(float2(bits) / 32767.0, -1.0);
    return unpacked;
}

// Pack two floats into 16-bit snorm values in the lo/hi bits of a dword.
//  - returns: Two 16-bit snorm in low/high bits.
uint RTXDI_PackSnorm2x16(float2 v)
{
    v = any(isnan(v)) ? float2(0, 0) : clamp(v, -1.0, 1.0);
    int2 iv = int2(round(v * 32767.0));
    uint packed = (iv.x & 0x0000ffff) | (iv.y << 16);

    return packed;
}

// Converts normalized direction to the octahedral map (non-equal area, signed normalized).
//  - n: Normalized direction.
//  - returns: Position in octahedral map in [-1,1] for each component.
float2 RTXDI_NormalizedVectorToOctahedralMapping(float3 n)
{
    // Project the sphere onto the octahedron (|x|+|y|+|z| = 1) and then onto the xy-plane.
    float2 p = float2(n.x, n.y) * (1.0 / (abs(n.x) + abs(n.y) + abs(n.z)));

    // Reflect the folds of the lower hemisphere over the diagonals.
    if (n.z < 0.0) {
        p = float2(
            (1.0 - abs(p.y)) * (p.x >= 0.0 ? 1.0 : -1.0),
            (1.0 - abs(p.x)) * (p.y >= 0.0 ? 1.0 : -1.0)
            );
    }

    return p;
}

// Converts point in the octahedral map to normalized direction (non-equal area, signed normalized).
//  - p: Position in octahedral map in [-1,1] for each component.
//  - returns: Normalized direction.
float3 RTXDI_OctahedralMappingToNormalizedVector(float2 p)
{
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));

    // Reflect the folds of the lower hemisphere over the diagonals.
    if (n.z < 0.0) {
        n.xy = float2(
            (1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0),
            (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0)
            );
    }

    return normalize(n);
}

// Encode a normal packed as 2x 16-bit snorms in the octahedral mapping.
uint RTXDI_EncodeNormalizedVectorToSnorm2x16(float3 normal)
{
    float2 octNormal = RTXDI_NormalizedVectorToOctahedralMapping(normal);
    return RTXDI_PackSnorm2x16(octNormal);
}

// Decode a normal packed as 2x 16-bit snorms in the octahedral mapping.
float3 RTXDI_DecodeNormalizedVectorFromSnorm2x16(uint packedNormal)
{
    float2 octNormal = RTXDI_UnpackSnorm2x16(packedNormal);
    return RTXDI_OctahedralMappingToNormalizedVector(octNormal);
}

// Transforms an RGB color in Rec.709 to CIE XYZ.
float3 RTXDI_RGBToXYZInRec709(float3 c)
{
    static const float3x3 M = float3x3(
        0.4123907992659595, 0.3575843393838780, 0.1804807884018343,
        0.2126390058715104, 0.7151686787677559, 0.0721923153607337,
        0.0193308187155918, 0.1191947797946259, 0.9505321522496608
    );
#ifdef RTXDI_GLSL
    return c * M; // GLSL initializes matrices in a column-major order, HLSL in row-major
#else
    return mul(M, c);
#endif
}

// Transforms an XYZ color to RGB in Rec.709.
float3 RTXDI_XYZToRGBInRec709(float3 c)
{
    static const float3x3 M = float3x3(
        3.240969941904522, -1.537383177570094, -0.4986107602930032,
        -0.9692436362808803, 1.875967501507721, 0.04155505740717569,
        0.05563007969699373, -0.2039769588889765, 1.056971514242878
    );
#ifdef RTXDI_GLSL
    return c * M; // GLSL initializes matrices in a column-major order, HLSL in row-major
#else
    return mul(M, c);
#endif
}

// Encode an RGB color into a 32-bit LogLuv HDR format.
//
// The supported luminance range is roughly 10^-6..10^6 in 0.17% steps.
// The log-luminance is encoded with 14 bits and chroma with 9 bits each.
// This was empirically more accurate than using 8 bit chroma.
// Black (all zeros) is handled exactly.
uint RTXDI_EncodeRGBToLogLuv(float3 color)
{
    // Convert RGB to XYZ.
    float3 XYZ = RTXDI_RGBToXYZInRec709(color);

    // Encode log2(Y) over the range [-20,20) in 14 bits (no sign bit).
    // TODO: Fast path that uses the bits from the fp32 representation directly.
    float logY = 409.6 * (log2(XYZ.y) + 20.0); // -inf if Y==0
    uint Le = uint(clamp(logY, 0.0, 16383.0));

    // Early out if zero luminance to avoid NaN in chroma computation.
    // Note Le==0 if Y < 9.55e-7. We'll decode that as exactly zero.
    if (Le == 0) return 0;

    // Compute chroma (u,v) values by:
    //  x = X / (X + Y + Z)
    //  y = Y / (X + Y + Z)
    //  u = 4x / (-2x + 12y + 3)
    //  v = 9y / (-2x + 12y + 3)
    //
    // These expressions can be refactored to avoid a division by:
    //  u = 4X / (-2X + 12Y + 3(X + Y + Z))
    //  v = 9Y / (-2X + 12Y + 3(X + Y + Z))
    //
    float invDenom = 1.0 / (-2.0 * XYZ.x + 12.0 * XYZ.y + 3.0 * (XYZ.x + XYZ.y + XYZ.z));
    float2 uv = float2(4.0, 9.0) * XYZ.xy * invDenom;

    // Encode chroma (u,v) in 9 bits each.
    // The gamut of perceivable uv values is roughly [0,0.62], so scale by 820 to get 9-bit values.
    uint2 uve = uint2(clamp(820.0 * uv, 0.0, 511.0));

    return (Le << 18) | (uve.x << 9) | uve.y;
}

// Decode an RGB color stored in a 32-bit LogLuv HDR format.
//    See RTXDI_EncodeRGBToLogLuv() for details.
float3 RTXDI_DecodeLogLuvToRGB(uint packedColor)
{
    // Decode luminance Y from encoded log-luminance.
    uint Le = packedColor >> 18;
    if (Le == 0) return float3(0, 0, 0);

    float logY = (float(Le) + 0.5) / 409.6 - 20.0;
    float Y = pow(2.0, logY);

    // Decode normalized chromaticity xy from encoded chroma (u,v).
    //
    //  x = 9u / (6u - 16v + 12)
    //  y = 4v / (6u - 16v + 12)
    //
    uint2 uve = uint2(packedColor >> 9, packedColor) & 0x1ff;
    float2 uv = (float2(uve)+0.5) / 820.0;

    float invDenom = 1.0 / (6.0 * uv.x - 16.0 * uv.y + 12.0);
    float2 xy = float2(9.0, 4.0) * uv * invDenom;

    // Convert chromaticity to XYZ and back to RGB.
    //  X = Y / y * x
    //  Z = Y / y * (1 - x - y)
    //
    float s = Y / xy.y;
    float3 XYZ = float3(s * xy.x, Y, s * (1.f - xy.x - xy.y));

    // Convert back to RGB and clamp to avoid out-of-gamut colors.
    return max(RTXDI_XYZToRGBInRec709(XYZ), 0.0);
}

#endif // RTXDI_MATH_HLSLI
