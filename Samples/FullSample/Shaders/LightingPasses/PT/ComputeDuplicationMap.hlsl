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

/*
 * Compute duplication map: for each pixel count how many neighbors (17x17)
 * share the same sample ID, cap at 255 for RG8_UNORM. Used for
 * duplication-based temporal history reduction.
 */

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#define RTXDI_PT_ENABLE_DUPLICATION_MAP_COUNT
#include <Rtxdi/PT/DuplicationMap.hlsli>

[numthreads(RTXDI_PT_DUPMAP_GROUP_SIZE, RTXDI_PT_DUPMAP_GROUP_SIZE, 1)]
void main(uint2 groupID : SV_GroupID, uint2 threadID : SV_GroupThreadID)
{
    RTXDI_PTComputeDuplicationMap(groupID, threadID, g_Const.view.viewportSize);
}
