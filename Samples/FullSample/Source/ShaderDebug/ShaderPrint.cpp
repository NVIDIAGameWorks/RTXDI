//=================================================================================================
//
//  MJP's DX12 Sample Framework
//  https://therealmjp.github.io/
//
//  All code licensed under the MIT license
//
//=================================================================================================

#include "ShaderPrint.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include "SharedShaderInclude/ShaderDebug/ShaderDebugPrintShared.h"

#define ArraySize_(x) ((sizeof(x) / sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

static const std::string ArgPlaceHolders[] =
{
    "{0}",
    "{1}",
    "{2}",
    "{3}",
    "{4}",
    "{5}",
    "{6}",
    "{7}",
    "{8}",
    "{9}",
    "{10}",
    "{11}",
    "{12}",
    "{13}",
    "{14}",
    "{15}",
};
// StaticAssert_(ArraySize_(ArgPlaceHolders) == MaxDebugPrintArgs);

static const size_t ArgCodeSizes[] =
{
    sizeof(uint32_t),            // DebugPrint_Bool
    sizeof(uint32_t),            // DebugPrint_Uint
    sizeof(donut::math::uint2),  // DebugPrint_Uint2
    sizeof(donut::math::uint3),  // DebugPrint_Uint3
    sizeof(donut::math::uint4),  // DebugPrint_Uint4
    sizeof(int),                 // DebugPrint_Int
    sizeof(donut::math::int2),   // DebugPrint_Int2
    sizeof(donut::math::int3),   // DebugPrint_Int3
    sizeof(donut::math::int4),   // DebugPrint_Int4
    sizeof(float),               // DebugPrint_Float
    sizeof(donut::math::float2), // DebugPrint_Float2
    sizeof(donut::math::float3), // DebugPrint_Float3
    sizeof(donut::math::float4), // DebugPrint_Float4
};

static void ReplaceStringInPlace(std::string& subject, const std::string& search, const std::string& replace)
{
    size_t pos = 0;
    while((pos = subject.find(search, pos)) != std::string::npos)
    {
         subject.replace(pos, search.length(), replace);
         pos += replace.length();
    }
}

ShaderPrintReadbackPass::ShaderPrintReadbackPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    uint32_t bufferSizeInBytes,
    uint32_t numFramesInFlight) :
    m_device(device),
    m_shaderFactory(shaderFactory),
    m_numFramesInFlight(numFramesInFlight),
    m_currentFrameIndex(0),
    m_cbData({})
{
    auto cbDesc = nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(ShaderPrintCBData), "ShaderPrintCBData");
    cbDesc.keepInitialState = true;
    m_cbDataBuffer = m_device->createBuffer(cbDesc);

    m_cbData.CursorXY = donut::math::uint2(0, 0);
    m_cbData.PrintBufferIndex = 0;
    m_cbData.PrintBufferSliceSizeInBytes = bufferSizeInBytes;

    nvrhi::BufferDesc printBufferDesc;
    printBufferDesc.byteSize = m_numFramesInFlight * m_cbData.PrintBufferSliceSizeInBytes;
    printBufferDesc.format = nvrhi::Format::R8_UINT;
    printBufferDesc.canHaveTypedViews = true;
    printBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    printBufferDesc.debugName = "DebugPrintBuffer";
    printBufferDesc.canHaveUAVs = true;
    printBufferDesc.keepInitialState = true;
    printBufferDesc.canHaveRawViews = true;
    m_printBuffer = device->createBuffer(printBufferDesc);

    printBufferDesc.canHaveUAVs = false;
    printBufferDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
    printBufferDesc.initialState = nvrhi::ResourceStates::Common;
    printBufferDesc.debugName = "DebugPrintReadbackBuffer";
    m_printReadbackBuffers.resize(m_numFramesInFlight);
    for (size_t bank = 0; bank < m_numFramesInFlight; bank++)
    {
        m_printReadbackBuffers[bank] = m_device->createBuffer(printBufferDesc);
    }
}

void ShaderPrintReadbackPass::InitializeFrame(nvrhi::ICommandList* commandList)
{
    commandList->writeBuffer(m_cbDataBuffer, &m_cbData, sizeof(m_cbData));
}

uint32_t ShaderPrintReadbackPass::GetBufferSizeInBytes() const
{
    return m_cbData.PrintBufferSliceSizeInBytes;
}

donut::math::uint2 ShaderPrintReadbackPass::GetSelectedPixelCoordinates() const
{
    return m_cbData.CursorXY;
}

void ShaderPrintReadbackPass::SetSelectedPixelCoordinates(donut::math::uint2 pixelCoordinates)
{
    m_cbData.CursorXY = pixelCoordinates;
}

void ShaderPrintReadbackPass::Enable(bool enable)
{
    m_cbData.enable = enable;
}

bool ShaderPrintReadbackPass::IsEnabled() const
{
    return m_cbData.enable;
}

const std::vector<std::string>& ShaderPrintReadbackPass::GetLines() const
{
    return m_lines;
}

struct DebugPrintReader
{
    const uint8_t* PrintBufferData = nullptr;
    uint32_t BufferSize = 0;
    uint32_t TotalNumBytes = 0;
    uint32_t CurrOffset = 0;

    DebugPrintReader(const uint8_t* printBufferData, uint32_t bufferSize)
    {
        PrintBufferData = printBufferData;
        BufferSize = bufferSize;
        memcpy(&TotalNumBytes, printBufferData, sizeof(uint32_t));
        TotalNumBytes += sizeof(uint32_t);
        TotalNumBytes = std::min(TotalNumBytes, BufferSize);
        CurrOffset = sizeof(uint32_t);
    }

    template<typename T> T Consume(T defVal = T())
    {
        T x;

        if(CurrOffset + sizeof(T) > TotalNumBytes)
            return defVal;

        memcpy(&x, PrintBufferData + CurrOffset, sizeof(T));
        CurrOffset += sizeof(T);
        return x;
    }

    const char* ConsumeString(uint32_t expectedStringSize)
    {
        if(expectedStringSize == 0)
            return "";

        if(CurrOffset + expectedStringSize > TotalNumBytes)
            return "";

        const char* stringData = reinterpret_cast<const char*>(PrintBufferData + CurrOffset);
        CurrOffset += expectedStringSize;
        if(stringData[expectedStringSize - 1] != '\0')
            return "";

        return stringData;
    }

    bool HasMoreData(uint32_t numBytes) const
    {
        return (CurrOffset + numBytes) <= TotalNumBytes;
    }
};

static std::string MakeString(const char* format, ...)
{
    char buffer[1024 * 16] = { 0 };
    va_list args;
    va_start(args, format);

#ifdef _WIN32
    vsprintf_s(buffer, ArraySize_(buffer), format, args);
#else
    vsnprintf(buffer, ArraySize_(buffer), format, args);
#endif
    va_end(args);
    return std::string(buffer);
}

static std::string MakeArgString(DebugPrintReader& printReader, ArgCode argCode)
{
    using namespace donut::math;

    if (argCode == DebugPrint_Bool)
    {
        return MakeString("%u", printReader.Consume<uint32_t>());
    }
    if(argCode == DebugPrint_Uint)
    {
        return MakeString("%u", printReader.Consume<uint32_t>());
    }
    if(argCode == DebugPrint_Uint2)
    {
        const uint2 x = printReader.Consume<uint2>();
        return MakeString("(%u, %u)", x.x, x.y);
    }
    if(argCode == DebugPrint_Uint3)
    {
        const uint3 x = printReader.Consume<uint3>();
        return MakeString("(%u, %u, %u)", x.x, x.y, x.z);
    }
    if(argCode == DebugPrint_Uint4)
    {
        const uint4 x = printReader.Consume<uint4>();
        return MakeString("(%u, %u, %u, %u)", x.x, x.y, x.z, x.w);
    }
    if(argCode == DebugPrint_Int)
    {
        return MakeString("%i", printReader.Consume<int32_t>());
    }
    if(argCode == DebugPrint_Int2)
    {
        const int2 x = printReader.Consume<int2>();
        return MakeString("(%i, %i)", x.x, x.y);
    }
    if(argCode == DebugPrint_Int3)
    {
        const int3 x = printReader.Consume<int3>();
        return MakeString("(%i, %i, %i)", x.x, x.y, x.z);
    }
    if(argCode == DebugPrint_Int4)
    {
        const int4 x = printReader.Consume<int4>();
        return MakeString("(%i, %i, %i, %i)", x.x, x.y, x.z, x.w);
    }
    if(argCode == DebugPrint_Float)
    {
        return MakeString("%f", printReader.Consume<float>());
    }
    if(argCode == DebugPrint_Float2)
    {
        const float2 x = printReader.Consume<float2>();
        return MakeString("(%f, %f)", x.x, x.y);
    }
    if(argCode == DebugPrint_Float3)
    {
        const float3 x = printReader.Consume<float3>();
        return MakeString("(%f, %f, %f)", x.x, x.y, x.z);
    }
    if(argCode == DebugPrint_Float4)
    {
        const float4 x = printReader.Consume<float4>();
        return MakeString("(%f, %f, %f, %f)", x.x, x.y, x.z, x.w);
    }

    return "???";
}

static inline uint32_t AlignTo(uint32_t num, uint32_t alignment)
{
    //Assert_(alignment > 0);
    return ((num + alignment - 1) / alignment) * alignment;
}

void ShaderPrintReadbackPass::ReadBackPrintData(nvrhi::ICommandList* commandList)
{
    commandList->beginMarker("ShaderPrint Readout");

    commandList->writeBuffer(m_cbDataBuffer, &m_cbData, sizeof(m_cbData));

    uint8_t* rawCBPtr = (uint8_t*)m_device->mapBuffer(m_printReadbackBuffers[m_cbData.PrintBufferIndex], nvrhi::CpuAccessMode::Read);

    m_lines.clear();
    
    DebugPrintReader printReader(rawCBPtr, m_cbData.PrintBufferSliceSizeInBytes);

    while(printReader.HasMoreData(sizeof(DebugPrintHeader)))
    {
        const DebugPrintHeader header = printReader.Consume<DebugPrintHeader>(DebugPrintHeader{});
        if(header.NumBytes == 0 || printReader.HasMoreData(header.NumBytes) == false)
            break;

        std::string formatStr = printReader.ConsumeString(header.StringSize);
        if(formatStr.length() == 0)
            break;

        if(header.NumArgs > MaxDebugPrintArgs)
            break;

        for(uint32_t argIdx = 0; argIdx < header.NumArgs; ++argIdx)
        {

            const ArgCode argCode = (ArgCode)printReader.Consume<uint8_t>(0xFF);
            if(argCode >= NumDebugPrintArgCodes)
                break;

            const uint32_t argSize = static_cast<uint32_t>(ArgCodeSizes[argCode]);
            if(printReader.HasMoreData(argSize) == false)
                break;

            const std::string argStr = MakeArgString(printReader, argCode);
            ReplaceStringInPlace(formatStr, ArgPlaceHolders[argIdx], argStr);
        }

        m_lines.push_back(formatStr);

        printReader.CurrOffset = AlignTo(printReader.CurrOffset, 4);
    }

    if(rawCBPtr != nullptr)
        m_device->unmapBuffer(m_printReadbackBuffers[m_cbData.PrintBufferIndex]);

    commandList->copyBuffer(
        m_printReadbackBuffers[m_cbData.PrintBufferIndex],
        0,
        m_printBuffer,
        0,
        m_cbData.PrintBufferSliceSizeInBytes);

    commandList->clearBufferUInt(m_printBuffer, 0);

    commandList->endMarker();

    m_cbData.PrintBufferIndex = (m_cbData.PrintBufferIndex + 1) % m_numFramesInFlight;
    m_currentFrameIndex++;
}

nvrhi::BufferHandle ShaderPrintReadbackPass::GetCBDataBuffer() const
{
    return m_cbDataBuffer;
}

nvrhi::BufferHandle ShaderPrintReadbackPass::GetPrintBuffer() const
{
    return m_printBuffer;
}
