/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "Profiler.h"
#include <donut/app/DeviceManager.h>
#include <imgui.h>
#include <sstream>

#include "RenderTargets.h"


static const char* g_SectionNames[ProfilerSection::Count] = {
    "TLAS Update",
    "Environment Map",
    "G-Buffer Fill",
    "Mesh Processing",
    "Light PDF Map",
    "Presample Lights",
    "Presample Env. Map",
    "ReGIR Build",
    "Initial Samples",
    "Temporal Resampling",
    "Spatial Resampling",
    "Shade Primary Surf.",
    "BRDF or MIS Rays",
    "Shade Secondary Surf.",
    "GI - Temporal Resampling",
    "GI - Spatial Resampling",
    "GI - Fused Resampling",
    "GI - Final Shading",
    "PT - Generate Initial Samples",
    "PT - Temporal Resampling",
    "PT - Spatial Neighbor Selection",
    "PT - Spatial Resampling",
    "PT - Compute Duplication Map",
    "PT - Compute Smoothed Duplication Map",
    "PT - Final Shading",
    "Gradients",
    "Denoising",
    "Glass",
    "TAA or DLSS",
    "Frame Time (GPU)",
    "(Material Readback)"
};

Profiler::Profiler(donut::app::DeviceManager& deviceManager)
    : m_deviceManager(deviceManager)
    , m_device(deviceManager.GetDevice())
{
    for (auto& query : m_timerQueries)
        query = m_device->createTimerQuery();

    nvrhi::BufferDesc rayCountBufferDesc;
    rayCountBufferDesc.byteSize = sizeof(uint32_t) * 2 * ProfilerSection::Count;
    rayCountBufferDesc.format = nvrhi::Format::R32_UINT;
    rayCountBufferDesc.canHaveUAVs = true;
    rayCountBufferDesc.canHaveTypedViews = true;
    rayCountBufferDesc.debugName = "RayCount";
    rayCountBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    rayCountBufferDesc.keepInitialState = true;
    m_rayCountBuffer = m_device->createBuffer(rayCountBufferDesc);

    rayCountBufferDesc.canHaveUAVs = false;
    rayCountBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    rayCountBufferDesc.initialState = nvrhi::ResourceStates::Common;
    rayCountBufferDesc.debugName = "RayCountReadback";
    for (size_t bank = 0; bank < 2; bank++)
    {
        m_rayCountReadback[bank] = m_device->createBuffer(rayCountBufferDesc);
    }
}

bool Profiler::IsEnabled() const
{
    return m_enabled;
}

void Profiler::EnableProfiler(bool enable)
{
    m_enabled = enable;
}

void Profiler::EnableAccumulation(bool enable)
{
    m_isAccumulating = enable;
}

void Profiler::ResetAccumulation()
{
    m_accumulatedFrames = 0;
    m_timerValues.fill(0.0);
    m_rayCounts.fill(0);
    m_hitCounts.fill(0);
}

void Profiler::ResolvePreviousFrame()
{
    m_activeBank = !m_activeBank;

    if (!m_enabled)
        return;

    const uint32_t* rayCountData = static_cast<const uint32_t*>(m_device->mapBuffer(m_rayCountReadback[m_activeBank], nvrhi::CpuAccessMode::Read));
    
    for (uint32_t section = 0; section < ProfilerSection::MaterialReadback; section++)
    {
        double time = 0;
        uint32_t rayCount = 0;
        uint32_t hitCount = 0;

        uint32_t timerIndex = section + m_activeBank * ProfilerSection::Count;
        
        if (m_timersUsed[timerIndex])
        {
            time = double(m_device->getTimerQueryTime(m_timerQueries[timerIndex]));
            time *= 1000.0; // seconds -> milliseconds

            if (rayCountData)
            {
                rayCount = rayCountData[section * 2];
                hitCount = rayCountData[section * 2 + 1];
            }
        }

        m_timersUsed[timerIndex] = false;

        if (m_isAccumulating)
        {
            m_timerValues[section] += time;
            m_rayCounts[section] += rayCount;
            m_hitCounts[section] += hitCount;
        }
        else
        {
            m_timerValues[section] = time;
            m_rayCounts[section] = rayCount;
            m_hitCounts[section] = hitCount;
        }
    }

    if (rayCountData)
        m_rayCounts[ProfilerSection::MaterialReadback] = rayCountData[ProfilerSection::MaterialReadback * 2];
    else
        m_rayCounts[ProfilerSection::MaterialReadback] = 0;

    if (rayCountData)
    {
        m_device->unmapBuffer(m_rayCountReadback[m_activeBank]);
    }

    if (m_isAccumulating)
        m_accumulatedFrames += 1;
    else
        m_accumulatedFrames = 1;
}

void Profiler::BeginFrame(nvrhi::ICommandList* commandList)
{
    if (!m_enabled)
        return;

    commandList->clearBufferUInt(m_rayCountBuffer, 0);

    BeginSection(commandList, ProfilerSection::Frame);
}

void Profiler::EndFrame(nvrhi::ICommandList* commandList)
{
    EndSection(commandList, ProfilerSection::Frame);

    if (m_enabled)
    {
        commandList->copyBuffer(
            m_rayCountReadback[m_activeBank],
            0,
            m_rayCountBuffer,
            0,
            ProfilerSection::Count * sizeof(uint32_t) * 2);
    }
}

void Profiler::BeginSection(nvrhi::ICommandList* commandList, const ProfilerSection::Enum section)
{
    if (!m_enabled)
        return;

    uint32_t timerIndex = section + m_activeBank * ProfilerSection::Count;
    commandList->beginTimerQuery(m_timerQueries[timerIndex]);
    m_timersUsed[timerIndex] = true;
}

void Profiler::EndSection(nvrhi::ICommandList* commandList, const ProfilerSection::Enum section)
{
    if (!m_enabled)
        return;
    
    uint32_t timerIndex = section + m_activeBank * ProfilerSection::Count;
    commandList->endTimerQuery(m_timerQueries[timerIndex]);
}

void Profiler::SetRenderTargets(const std::shared_ptr<RenderTargets>& renderTargets)
{
    m_renderTargets = renderTargets;
}

double Profiler::GetTimer(ProfilerSection::Enum section)
{
    if (m_accumulatedFrames == 0)
        return 0.0;

    return m_timerValues[section] / double(m_accumulatedFrames);
}

double Profiler::GetRayCount(ProfilerSection::Enum section)
{
    if (m_accumulatedFrames == 0)
        return 0.0;

    return double(m_rayCounts[section]) / double(m_accumulatedFrames);
}

double Profiler::GetHitCount(ProfilerSection::Enum section)
{
    if (m_accumulatedFrames == 0)
        return 0.0;

    return double(m_hitCounts[section]) / double(m_accumulatedFrames);
}

int Profiler::GetMaterialReadback()
{
    return int(m_rayCounts[ProfilerSection::MaterialReadback]) - 1;
}

void Profiler::BuildUI(const bool enableRayCounts)
{
    auto renderTargets = m_renderTargets.lock();
    if (!renderTargets)
        return;

    const int renderPixels = renderTargets->Size.x * renderTargets->Size.y;

    const float timeColumnWidth = 70.f;
    const float otherColumnsWidth = 40.f;

    ImGui::BeginTable("Profiler", enableRayCounts ? 4 : 2);
    ImGui::TableSetupColumn(" Section");
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, timeColumnWidth);
    if (enableRayCounts)
    {
        ImGui::TableSetupColumn("RPP", ImGuiTableColumnFlags_WidthFixed, otherColumnsWidth);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, otherColumnsWidth);
    }
    ImGui::TableHeadersRow();
    
    for (uint32_t section = 0; section < ProfilerSection::MaterialReadback; section++)
    {
        if (section == ProfilerSection::InitialSamples ||
            section == ProfilerSection::Gradients || 
            section == ProfilerSection::Frame)
            ImGui::Separator();

        const double time = GetTimer(ProfilerSection::Enum(section));
        const double rayCount = GetRayCount(ProfilerSection::Enum(section));
        const double hitCount = GetHitCount(ProfilerSection::Enum(section));
        
        if (time == 0.0 && rayCount == 0.0)
            continue;

        const bool highlightRow = (section == ProfilerSection::Frame);

        if(highlightRow)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xff, 0xff, 0x40, 0xff));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", g_SectionNames[section]);
        ImGui::TableSetColumnIndex(1);

        char text[16];
        snprintf(text, sizeof(text), "%.3f ms", time);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SameLine(timeColumnWidth - textSize.x);
        ImGui::Text("%s", text);
        
        if (enableRayCounts && rayCount != 0.0)
        {
            double raysPerPixel = rayCount / renderPixels;
            double hitPercentage = 100.0 * hitCount / rayCount;

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", raysPerPixel);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.0f%%", hitPercentage);
        }

        if (highlightRow)
            ImGui::PopStyleColor();
    }

    ImGui::EndTable();
}

std::string Profiler::GetAsText()
{
    auto renderTargets = m_renderTargets.lock();
    if (!renderTargets)
        return "";

    const int renderPixels = renderTargets->Size.x * renderTargets->Size.y;
    
    std::stringstream text;
    text << "Renderer: " << m_deviceManager.GetRendererString() << std::endl;
    text << "Resolution: " << renderTargets->Size.x << " x " << renderTargets->Size.y << std::endl;

    for (uint32_t section = 0; section < ProfilerSection::MaterialReadback; section++)
    {
        const double time = GetTimer(ProfilerSection::Enum(section));
        const double rayCount = GetRayCount(ProfilerSection::Enum(section));
        const double hitCount = GetHitCount(ProfilerSection::Enum(section));

        if (time == 0.0 && rayCount == 0.0)
            continue;

        text << g_SectionNames[section] << ": ";

        text.precision(3);
        text << std::fixed << time << " ms";

        if (section == ProfilerSection::Frame)
        {
            text.precision(2);
            text << " (" << std::fixed << 1000.0 / time << " FPS)" << std::endl;
        }
        else if (rayCount != 0.0)
        {
            const double raysPerPixel = rayCount / renderPixels;
            const double hitPercentage = 100.0 * hitCount / rayCount;

            text.precision(3);
            text << " (" << std::fixed << raysPerPixel << " rpp, ";
            text.precision(0);
            text << hitPercentage << "% hits)";
        }

        text << std::endl;
    }

    return text.str();
}

nvrhi::IBuffer* Profiler::GetRayCountBuffer() const
{
    return m_rayCountBuffer;
}

ProfilerScope::ProfilerScope(Profiler& profiler, nvrhi::ICommandList* commandList, ProfilerSection::Enum section)
    : m_Profiler(profiler)
    , m_CommandList(commandList)
    , m_Section(section)
{
    assert(&m_Profiler);
    assert(m_CommandList);
    m_Profiler.BeginSection(m_CommandList, m_Section);
}

ProfilerScope::~ProfilerScope()
{
    assert(m_CommandList);
    m_Profiler.EndSection(m_CommandList, m_Section);
    m_CommandList = nullptr;
}
