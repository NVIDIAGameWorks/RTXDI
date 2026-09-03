/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RAB_SPATIAL_HELPERS_HLSLI
#define RAB_SPATIAL_HELPERS_HLSLI

// This function is called in the spatial resampling passes to make sure that 
// the samples actually land on the screen and not outside of its boundaries.
// It can clamp the position or reflect it across the nearest screen edge.
// The simplest implementation will just return the input pixelPosition.
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool prevFrame)
{
    int width = int(g_Const.view.viewportSize.x);
    int height = int(g_Const.view.viewportSize.y);

    // Reflect the position across the screen edges.
    // Compared to simple clamping, this prevents the spread of colorful blobs from screen edges.
    if (pixelPosition.x < 0) pixelPosition.x = -pixelPosition.x;
    if (pixelPosition.y < 0) pixelPosition.y = -pixelPosition.y;
    if (pixelPosition.x >= width) pixelPosition.x = 2 * width - pixelPosition.x - 1;
    if (pixelPosition.y >= height) pixelPosition.y = 2 * height - pixelPosition.y - 1;

    return pixelPosition;
}

// Check if the sample is fine to be used as a valid spatial sample.
// This function also be able to clamp the value of the Jacobian.
bool RAB_ValidateGISampleWithJacobian(inout float jacobian)
{
    // Sold angle ratio is too different. Discard the sample.
    if (jacobian > 10.0 || jacobian < 1 / 10.0) {
        return false;
    }

    // clamp Jacobian.
    jacobian = clamp(jacobian, 1 / 3.0, 3.0);

    return true;
}

#endif // RAB_SPATIAL_HELPERS_HLSLI