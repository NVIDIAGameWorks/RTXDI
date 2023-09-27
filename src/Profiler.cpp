/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

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
    "Gradients",
    "Denoising",
    "Glass",
    "TAA or DLSS",
    "Frame Time (GPU)",
    "(Material Readback)"
};

Profiler::Profiler(donut::app::DeviceManager& deviceManager)
    : m_DeviceManager(deviceManager)
    , m_Device(deviceManager.GetDevice())
{
    for (auto& query : m_TimerQueries)
        query = m_Device->createTimerQuery();

    nvrhi::BufferDesc rayCountBufferDesc;
    rayCountBufferDesc.byteSize = sizeof(uint32_t) * 2 * ProfilerSection::Count;
    rayCountBufferDesc.format = nvrhi::Format::R32_UINT;
    rayCountBufferDesc.canHaveUAVs = true;
    rayCountBufferDesc.canHaveTypedViews = true;
    rayCountBufferDesc.debugName = "RayCount";
    rayCountBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    rayCountBufferDesc.keepInitialState = true;
    m_RayCountBuffer = m_Device->createBuffer(rayCountBufferDesc);

    rayCountBufferDesc.canHaveUAVs = false;
    rayCountBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    rayCountBufferDesc.initialState = nvrhi::ResourceStates::Common;
    rayCountBufferDesc.debugName = "RayCountReadback";
    for (size_t bank = 0; bank < 2; bank++)
    {
        m_RayCountReadback[bank] = m_Device->createBuffer(rayCountBufferDesc);
    }
}

void Profiler::EnableProfiler(bool enable)
{
    m_Enabled = enable;
}

void Profiler::EnableAccumulation(bool enable)
{
    m_IsAccumulating = enable;
}

void Profiler::ResetAccumulation()
{
    m_AccumulatedFrames = 0;
    m_TimerValues.fill(0.0);
    m_RayCounts.fill(0);
    m_HitCounts.fill(0);
}

void Profiler::ResolvePreviousFrame()
{
    m_ActiveBank = !m_ActiveBank;

    if (!m_Enabled)
        return;

    const uint32_t* rayCountData = static_cast<const uint32_t*>(m_Device->mapBuffer(m_RayCountReadback[m_ActiveBank], nvrhi::CpuAccessMode::Read));
    
    for (uint32_t section = 0; section < ProfilerSection::MaterialReadback; section++)
    {
        double time = 0;
        uint32_t rayCount = 0;
        uint32_t hitCount = 0;

        uint32_t timerIndex = section + m_ActiveBank * ProfilerSection::Count;
        
        if (m_TimersUsed[timerIndex])
        {
            time = double(m_Device->getTimerQueryTime(m_TimerQueries[timerIndex]));
            time *= 1000.0; // seconds -> milliseconds

            if (rayCountData)
            {
                rayCount = rayCountData[section * 2];
                hitCount = rayCountData[section * 2 + 1];
            }
        }

        m_TimersUsed[timerIndex] = false;

        if (m_IsAccumulating)
        {
            m_TimerValues[section] += time;
            m_RayCounts[section] += rayCount;
            m_HitCounts[section] += hitCount;
        }
        else
        {
            m_TimerValues[section] = time;
            m_RayCounts[section] = rayCount;
            m_HitCounts[section] = hitCount;
        }
    }

    if (rayCountData)
        m_RayCounts[ProfilerSection::MaterialReadback] = rayCountData[ProfilerSection::MaterialReadback * 2];
    else
        m_RayCounts[ProfilerSection::MaterialReadback] = 0;

    if (rayCountData)
    {
        m_Device->unmapBuffer(m_RayCountReadback[m_ActiveBank]);
    }

    if (m_IsAccumulating)
        m_AccumulatedFrames += 1;
    else
        m_AccumulatedFrames = 1;
}

void Profiler::BeginFrame(nvrhi::ICommandList* commandList)
{
    if (!m_Enabled)
        return;

    commandList->clearBufferUInt(m_RayCountBuffer, 0);

    BeginSection(commandList, ProfilerSection::Frame);
}

void Profiler::EndFrame(nvrhi::ICommandList* commandList)
{
    EndSection(commandList, ProfilerSection::Frame);

    if (m_Enabled)
    {
        commandList->copyBuffer(
            m_RayCountReadback[m_ActiveBank],
            0,
            m_RayCountBuffer,
            0,
            ProfilerSection::Count * sizeof(uint32_t) * 2);
    }
}

void Profiler::BeginSection(nvrhi::ICommandList* commandList, const ProfilerSection::Enum section)
{
    if (!m_Enabled)
        return;

    uint32_t timerIndex = section + m_ActiveBank * ProfilerSection::Count;
    commandList->beginTimerQuery(m_TimerQueries[timerIndex]);
    m_TimersUsed[timerIndex] = true;
}

void Profiler::EndSection(nvrhi::ICommandList* commandList, const ProfilerSection::Enum section)
{
    if (!m_Enabled)
        return;
    
    uint32_t timerIndex = section + m_ActiveBank * ProfilerSection::Count;
    commandList->endTimerQuery(m_TimerQueries[timerIndex]);
}

double Profiler::GetTimer(ProfilerSection::Enum section)
{
    if (m_AccumulatedFrames == 0)
        return 0.0;

    return m_TimerValues[section] / double(m_AccumulatedFrames);
}

double Profiler::GetRayCount(ProfilerSection::Enum section)
{
    if (m_AccumulatedFrames == 0)
        return 0.0;

    return double(m_RayCounts[section]) / double(m_AccumulatedFrames);
}

double Profiler::GetHitCount(ProfilerSection::Enum section)
{
    if (m_AccumulatedFrames == 0)
        return 0.0;

    return double(m_HitCounts[section]) / double(m_AccumulatedFrames);
}

int Profiler::GetMaterialReadback()
{
    return int(m_RayCounts[ProfilerSection::MaterialReadback]) - 1;
}

void Profiler::BuildUI(const bool enableRayCounts)
{
    auto renderTargets = m_RenderTargets.lock();
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
    auto renderTargets = m_RenderTargets.lock();
    if (!renderTargets)
        return "";

    const int renderPixels = renderTargets->Size.x * renderTargets->Size.y;
    
    std::stringstream text;
    text << "Renderer: " << m_DeviceManager.GetRendererString() << std::endl;
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
