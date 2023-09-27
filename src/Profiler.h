/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <nvrhi/nvrhi.h>
#include <array>
#include <memory>

#include "ProfilerSections.h"

class RenderTargets;

namespace donut::app
{
    class DeviceManager;
}

class Profiler
{
private:
    bool m_Enabled = true;
    bool m_IsAccumulating = false;
    uint32_t m_AccumulatedFrames = 0;
    uint32_t m_ActiveBank = 0;

    std::array<nvrhi::TimerQueryHandle, ProfilerSection::Count * 2> m_TimerQueries;
    std::array<double, ProfilerSection::Count> m_TimerValues{};
    std::array<size_t, ProfilerSection::Count> m_RayCounts{};
    std::array<size_t, ProfilerSection::Count> m_HitCounts{};
    std::array<bool, ProfilerSection::Count * 2> m_TimersUsed{};

    donut::app::DeviceManager& m_DeviceManager;
    nvrhi::DeviceHandle m_Device;
    nvrhi::BufferHandle m_RayCountBuffer;
    std::array<nvrhi::BufferHandle, 2> m_RayCountReadback;
    std::weak_ptr<RenderTargets> m_RenderTargets;
    
public:
    explicit Profiler(donut::app::DeviceManager& deviceManager);

    bool IsEnabled() const { return m_Enabled; }
    void EnableProfiler(bool enable);
    void EnableAccumulation(bool enable);
    void ResetAccumulation();
    void ResolvePreviousFrame();
    void BeginFrame(nvrhi::ICommandList* commandList);
    void EndFrame(nvrhi::ICommandList* commandList);
    void BeginSection(nvrhi::ICommandList* commandList, ProfilerSection::Enum section);
    void EndSection(nvrhi::ICommandList* commandList, ProfilerSection::Enum section);
    void SetRenderTargets(const std::shared_ptr<RenderTargets>& renderTargets) { m_RenderTargets = renderTargets; }

    double GetTimer(ProfilerSection::Enum section);
    double GetRayCount(ProfilerSection::Enum section);
    double GetHitCount(ProfilerSection::Enum section);
    int GetMaterialReadback();

    void BuildUI(bool enableRayCounts);
    std::string GetAsText();

    [[nodiscard]] nvrhi::IBuffer* GetRayCountBuffer() const { return m_RayCountBuffer; }
};

class ProfilerScope
{
private:
    Profiler& m_Profiler;
    nvrhi::ICommandList* m_CommandList;
    ProfilerSection::Enum m_Section;

public:
    ProfilerScope(Profiler& profiler, nvrhi::ICommandList* commandList, ProfilerSection::Enum section);
    ~ProfilerScope();

    // Non-copyable and non-movable
    ProfilerScope(const ProfilerScope&) = delete;
    ProfilerScope(const ProfilerScope&&) = delete;
    ProfilerScope& operator=(const ProfilerScope&) = delete;
    ProfilerScope& operator=(const ProfilerScope&&) = delete;
};
