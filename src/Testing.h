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

struct UIData;

namespace donut::app {
    struct DeviceCreationParameters;
}

namespace donut::log {
    enum class Severity;
}

extern const char* g_ApplicationTitle;

struct CommandLineArguments
{
    nvrhi::GraphicsAPI graphicsApi = nvrhi::GraphicsAPI::VULKAN;
    uint32_t saveFrameIndex = 0;
    std::string saveFrameFileName;
    bool verbose = false;
    bool benchmark = false;
    bool disableBackgroundOptimization = false;
    int renderWidth = 0;
    int renderHeight = 0;
};

void ProcessCommandLine(int argc, char** argv, donut::app::DeviceCreationParameters& deviceParams, UIData& ui, CommandLineArguments& args);
void ApplicationLogCallback(donut::log::Severity severity, const char* message);
bool SaveTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* writeFileName);