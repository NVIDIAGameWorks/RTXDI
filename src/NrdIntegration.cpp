/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if WITH_NRD

#include "NrdIntegration.h"
#include "RenderTargets.h"
#include <nvrhi/utils.h>
#include <donut/core/math/math.h>
#include <donut/engine/View.h>
#include <sstream>

static void* NrdAllocate(void* userArg, size_t size, size_t alignment)
{
    return malloc(size);
}

static void* NrdReallocate(void* userArg, void* memory, size_t size, size_t alignment)
{
    return realloc(memory, size);
}

static void NrdFree(void* userArg, void* memory)
{
    free(memory);
}

static nvrhi::Format GetNvrhiFormat(nrd::Format format)
{
    switch (format)
    {
    case nrd::Format::R8_UNORM:             return nvrhi::Format::R8_UNORM;
    case nrd::Format::R8_SNORM:             return nvrhi::Format::R8_SNORM;
    case nrd::Format::R8_UINT:              return nvrhi::Format::R8_UINT;
    case nrd::Format::R8_SINT:              return nvrhi::Format::R8_SINT;
    case nrd::Format::RG8_UNORM:            return nvrhi::Format::RG8_UNORM;
    case nrd::Format::RG8_SNORM:            return nvrhi::Format::RG8_SNORM;
    case nrd::Format::RG8_UINT:             return nvrhi::Format::RG8_UINT;
    case nrd::Format::RG8_SINT:             return nvrhi::Format::RG8_SINT;
    case nrd::Format::RGBA8_UNORM:          return nvrhi::Format::RGBA8_UNORM;
    case nrd::Format::RGBA8_SNORM:          return nvrhi::Format::RGBA8_SNORM;
    case nrd::Format::RGBA8_UINT:           return nvrhi::Format::RGBA8_UINT;
    case nrd::Format::RGBA8_SINT:           return nvrhi::Format::RGBA8_SINT;
    case nrd::Format::RGBA8_SRGB:           return nvrhi::Format::SRGBA8_UNORM;
    case nrd::Format::R16_UNORM:            return nvrhi::Format::R16_UNORM;
    case nrd::Format::R16_SNORM:            return nvrhi::Format::R16_SNORM;
    case nrd::Format::R16_UINT:             return nvrhi::Format::R16_UINT;
    case nrd::Format::R16_SINT:             return nvrhi::Format::R16_SINT;
    case nrd::Format::R16_SFLOAT:           return nvrhi::Format::R16_FLOAT;
    case nrd::Format::RG16_UNORM:           return nvrhi::Format::RG16_UNORM;
    case nrd::Format::RG16_SNORM:           return nvrhi::Format::RG16_SNORM;
    case nrd::Format::RG16_UINT:            return nvrhi::Format::RG16_UINT;
    case nrd::Format::RG16_SINT:            return nvrhi::Format::RG16_SINT;
    case nrd::Format::RG16_SFLOAT:          return nvrhi::Format::RG16_FLOAT;
    case nrd::Format::RGBA16_UNORM:         return nvrhi::Format::RGBA16_UNORM;
    case nrd::Format::RGBA16_SNORM:         return nvrhi::Format::RGBA16_SNORM;
    case nrd::Format::RGBA16_UINT:          return nvrhi::Format::RGBA16_UINT;
    case nrd::Format::RGBA16_SINT:          return nvrhi::Format::RGBA16_SINT;
    case nrd::Format::RGBA16_SFLOAT:        return nvrhi::Format::RGBA16_FLOAT;
    case nrd::Format::R32_UINT:             return nvrhi::Format::R32_UINT;
    case nrd::Format::R32_SINT:             return nvrhi::Format::R32_SINT;
    case nrd::Format::R32_SFLOAT:           return nvrhi::Format::R32_FLOAT;
    case nrd::Format::RG32_UINT:            return nvrhi::Format::RG32_UINT;
    case nrd::Format::RG32_SINT:            return nvrhi::Format::RG32_SINT;
    case nrd::Format::RG32_SFLOAT:          return nvrhi::Format::RG32_FLOAT;
    case nrd::Format::RGB32_UINT:           return nvrhi::Format::RGB32_UINT;
    case nrd::Format::RGB32_SINT:           return nvrhi::Format::RGB32_SINT;
    case nrd::Format::RGB32_SFLOAT:         return nvrhi::Format::RGB32_FLOAT;
    case nrd::Format::RGBA32_UINT:          return nvrhi::Format::RGBA32_UINT;
    case nrd::Format::RGBA32_SINT:          return nvrhi::Format::RGBA32_SINT;
    case nrd::Format::RGBA32_SFLOAT:        return nvrhi::Format::RGBA32_FLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM: return nvrhi::Format::R10G10B10A2_UNORM;
    case nrd::Format::R10_G10_B10_A2_UINT:  return nvrhi::Format::UNKNOWN; // not representable and not used
    case nrd::Format::R11_G11_B10_UFLOAT:   return nvrhi::Format::R11G11B10_FLOAT;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:   return nvrhi::Format::UNKNOWN; // not representable and not used
    default:                                return nvrhi::Format::UNKNOWN;
    }
}

NrdIntegration::NrdIntegration(nvrhi::IDevice* device, nrd::Method method)
    : m_Device(device)
    , m_Initialized(false)
    , m_Denoiser(nullptr)
    , m_Method(method)
    , m_BindingCache(device)
{
}

bool NrdIntegration::Initialize(uint32_t width, uint32_t height)
{
    const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();

    const nrd::MethodDesc methods[] = {
        { m_Method, uint16_t(width), uint16_t(height) }
    };

    nrd::DenoiserCreationDesc denoiserCreationDesc;
    denoiserCreationDesc.memoryAllocatorInterface.Allocate = NrdAllocate;
    denoiserCreationDesc.memoryAllocatorInterface.Reallocate = NrdReallocate;
    denoiserCreationDesc.memoryAllocatorInterface.Free = NrdFree;
    denoiserCreationDesc.requestedMethodNum = dim(methods);
    denoiserCreationDesc.requestedMethods = methods;

    nrd::Result res = nrd::CreateDenoiser(denoiserCreationDesc, m_Denoiser);
    if (res != nrd::Result::SUCCESS)
        return false;

    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);
    const bool isVulkan = m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN;


    const nvrhi::BufferDesc constantBufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc(
        denoiserDesc.constantBufferDesc.maxDataSize, 
        "NrdConstantBuffer", 
        denoiserDesc.descriptorSetDesc.constantBufferMaxNum * 4);

    m_ConstantBuffer = m_Device->createBuffer(constantBufferDesc);


    for (uint32_t samplerIndex = 0; samplerIndex < denoiserDesc.staticSamplerNum; samplerIndex++)
    {
        const nrd::Sampler& samplerMode = denoiserDesc.staticSamplers[samplerIndex].sampler;

        nvrhi::SamplerAddressMode addressMode = nvrhi::SamplerAddressMode::Wrap;
        bool filter = false;

        switch (samplerMode)
        {
        case nrd::Sampler::NEAREST_CLAMP:
            addressMode = nvrhi::SamplerAddressMode::Clamp;
            filter = false;
            break;
        case nrd::Sampler::NEAREST_MIRRORED_REPEAT:
            addressMode = nvrhi::SamplerAddressMode::Mirror;
            filter = false;
            break;
        case nrd::Sampler::LINEAR_CLAMP:
            addressMode = nvrhi::SamplerAddressMode::Clamp;
            filter = true;
            break;
        case nrd::Sampler::LINEAR_MIRRORED_REPEAT:
            addressMode = nvrhi::SamplerAddressMode::Mirror;
            filter = true;
            break;
        default:
            assert(!"Unknown NRD sampler mode");
            break;
        }

        auto samplerDesc = nvrhi::SamplerDesc()
            .setAllAddressModes(addressMode)
            .setAllFilters(filter);

        const nvrhi::SamplerHandle sampler = m_Device->createSampler(samplerDesc);

        if (!sampler)
        {
            assert(!"Cannot create an NRD sampler");
            return false;
        }

        m_Samplers.push_back(sampler);
    }

    nvrhi::VulkanBindingOffsets bindingOffsets;
    bindingOffsets.shaderResource = libraryDesc.spirvBindingOffsets.textureOffset;
    bindingOffsets.sampler = libraryDesc.spirvBindingOffsets.samplerOffset;
    bindingOffsets.constantBuffer = libraryDesc.spirvBindingOffsets.constantBufferOffset;
    bindingOffsets.unorderedAccess = libraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;

    for (uint32_t pipelineIndex = 0; pipelineIndex < denoiserDesc.pipelineNum; pipelineIndex++)
    {
        const nrd::PipelineDesc& nrdPipelineDesc = denoiserDesc.pipelines[pipelineIndex];
        const nrd::ComputeShader& nrdComputeShader = isVulkan ? nrdPipelineDesc.computeShaderSPIRV : nrdPipelineDesc.computeShaderDXIL;

        NrdPipeline pipeline;
        pipeline.Shader = m_Device->createShader(nvrhi::ShaderDesc(nvrhi::ShaderType::Compute), nrdComputeShader.bytecode, nrdComputeShader.size);

        if (!pipeline.Shader)
        {
            assert(!"Cannot create an NRD shader");
            return false;
        }

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Compute;
        layoutDesc.bindingOffsets = bindingOffsets;

        nvrhi::BindingLayoutItem constantBufferItem = {};
        constantBufferItem.type = nvrhi::ResourceType::VolatileConstantBuffer;
        constantBufferItem.slot = denoiserDesc.constantBufferDesc.registerIndex;
        layoutDesc.bindings.push_back(constantBufferItem);

        for (uint32_t samplerIndex = 0; samplerIndex < denoiserDesc.staticSamplerNum; samplerIndex++)
        {
            const nrd::StaticSamplerDesc& nrdStaticSampler = denoiserDesc.staticSamplers[samplerIndex];

            nvrhi::BindingLayoutItem samplerItem = {};
            samplerItem.type = nvrhi::ResourceType::Sampler;
            samplerItem.slot = nrdStaticSampler.registerIndex;
            layoutDesc.bindings.push_back(samplerItem);
        }

        for (uint32_t descriptorRangeIndex = 0; descriptorRangeIndex < nrdPipelineDesc.descriptorRangeNum; descriptorRangeIndex++)
        {
            const nrd::DescriptorRangeDesc& nrdDescriptorRange = nrdPipelineDesc.descriptorRanges[descriptorRangeIndex];

            nvrhi::BindingLayoutItem resourceItem = {};
            switch (nrdDescriptorRange.descriptorType)
            {
            case nrd::DescriptorType::TEXTURE:
                resourceItem.type = nvrhi::ResourceType::Texture_SRV;
                break;
            case nrd::DescriptorType::STORAGE_TEXTURE:
                resourceItem.type = nvrhi::ResourceType::Texture_UAV;
                break;
            default:
                assert(!"Unknown NRD descriptor type");
                break;
            }

            for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorNum; descriptorOffset++)
            {
                resourceItem.slot = nrdDescriptorRange.baseRegisterIndex + descriptorOffset;
                layoutDesc.bindings.push_back(resourceItem);
            }
        }

        pipeline.BindingLayout = m_Device->createBindingLayout(layoutDesc);

        if (!pipeline.BindingLayout)
        {
            assert(!"Cannot create an NRD binding layout");
            return false;
        }

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { pipeline.BindingLayout };
        pipelineDesc.CS = pipeline.Shader;
        pipeline.Pipeline = m_Device->createComputePipeline(pipelineDesc);

        if (!pipeline.Pipeline)
        {
            assert(!"Cannot create an NRD pipeline");
            return false;
        }

        m_Pipelines.push_back(pipeline);
    }


    const uint32_t poolSize = denoiserDesc.permanentPoolSize + denoiserDesc.transientPoolSize;

    for (uint32_t i = 0; i < poolSize; i++)
    {
        const bool isPermanent = (i < denoiserDesc.permanentPoolSize);

        const nrd::TextureDesc& nrdTextureDesc = isPermanent 
            ? denoiserDesc.permanentPool[i] 
            : denoiserDesc.transientPool[i - denoiserDesc.permanentPoolSize];

        const nvrhi::Format format = GetNvrhiFormat(nrdTextureDesc.format);

        if (format == nvrhi::Format::UNKNOWN)
        {
            assert(!"Unknown or unsupported NRD format");
            return false;
        }

        std::stringstream ss;
        ss << "NRD " << (isPermanent ? "Permanent" : "Transient") << "Texture [" << (isPermanent ? i : i - denoiserDesc.permanentPoolSize) << "]";

        nvrhi::TextureDesc textureDesc;
        textureDesc.width = nrdTextureDesc.width;
        textureDesc.height = nrdTextureDesc.height;
        textureDesc.format = format;
        textureDesc.mipLevels = nrdTextureDesc.mipNum;
        textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        textureDesc.keepInitialState = true;
        textureDesc.isUAV = true;
        textureDesc.debugName = ss.str();

        const nvrhi::TextureHandle texture = m_Device->createTexture(textureDesc);

        if (!texture)
        {
            assert(!"Cannot create an NRD texture");
            return false;
        }

        if (isPermanent)
            m_PermanentTextures.push_back(texture);
        else
            m_TransientTextures.push_back(texture);
    }

    m_Initialized = true;
    return true;
}

bool NrdIntegration::IsAvailable() const
{
    return m_Initialized;
}

static inline void MatrixToNrd(float* dest, const dm::float4x4& m)
{
    dm::float4x4 tm = dm::transpose(m);
    memcpy(dest, &m, sizeof(m));
}

void NrdIntegration::RunDenoiserPasses(
    nvrhi::ICommandList* commandList,
    const RenderTargets& renderTargets,
    const donut::engine::PlanarView& view, 
    const donut::engine::PlanarView& viewPrev, 
    uint32_t frameIndex,
    bool enableConfidenceInputs,
    const void* methodSettings)
{
    if (methodSettings)
    {
        nrd::SetMethodSettings(*m_Denoiser, m_Method, methodSettings);
    }

    nrd::CommonSettings commonSettings;
    MatrixToNrd(commonSettings.worldToViewMatrix, dm::affineToHomogeneous(view.GetViewMatrix()));
    MatrixToNrd(commonSettings.worldToViewMatrixPrev, dm::affineToHomogeneous(viewPrev.GetViewMatrix()));
    MatrixToNrd(commonSettings.viewToClipMatrix, view.GetProjectionMatrix(false));
    MatrixToNrd(commonSettings.viewToClipMatrixPrev, viewPrev.GetProjectionMatrix(false));

    const auto& motionVectorDesc = renderTargets.MotionVectors->getDesc();

    // Convert our render size to the resolutionScale parameters
    float widthScale = float(view.GetViewExtent().width()) / float(motionVectorDesc.width);
    float heightScale = float(view.GetViewExtent().height()) / float(motionVectorDesc.height);

    // Figure out what NRD will think our render size is based on the resolutionScale
    float scaledWidth = round(float(motionVectorDesc.width) * widthScale);
    float scaledHeight = round(float(motionVectorDesc.height) * heightScale);

    dm::float2 pixelOffset = view.GetPixelOffset();
    commonSettings.motionVectorScale[0] = 1.f / scaledWidth;
    commonSettings.motionVectorScale[1] = 1.f / scaledHeight;
    commonSettings.resolutionScale[0] = widthScale;
    commonSettings.resolutionScale[1] = heightScale;
    commonSettings.cameraJitter[0] = pixelOffset.x;
    commonSettings.cameraJitter[1] = pixelOffset.y;
    commonSettings.frameIndex = frameIndex;
    commonSettings.isMotionVectorInWorldSpace = false;
    commonSettings.isHistoryConfidenceInputsAvailable = enableConfidenceInputs;

    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescNum = 0;
    nrd::Result result = nrd::GetComputeDispatches(*m_Denoiser, commonSettings, dispatchDescs, dispatchDescNum);

    if (result != nrd::Result::SUCCESS)
    {
        assert(!"nrd::GetComputeDispatches failed");
        return;
    }

    const nrd::DenoiserDesc& denoiserDesc = nrd::GetDenoiserDesc(*m_Denoiser);

    for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchDescNum; dispatchIndex++)
    {
        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[dispatchIndex];

        if (dispatchDesc.name)
        {
            commandList->beginMarker(dispatchDesc.name);
        }

        assert(m_ConstantBuffer);
        commandList->writeBuffer(m_ConstantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(denoiserDesc.constantBufferDesc.registerIndex, m_ConstantBuffer));

        for (uint32_t samplerIndex = 0; samplerIndex < denoiserDesc.staticSamplerNum; samplerIndex++)
        {
            const nrd::StaticSamplerDesc& nrdStaticSampler = denoiserDesc.staticSamplers[samplerIndex];

            assert(m_Samplers[samplerIndex]);
            setDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(nrdStaticSampler.registerIndex, m_Samplers[samplerIndex]));
        }

        const nrd::PipelineDesc& nrdPipelineDesc = denoiserDesc.pipelines[dispatchDesc.pipelineIndex];
        uint32_t resourceIndex = 0;

        for (uint32_t descriptorRangeIndex = 0; descriptorRangeIndex < nrdPipelineDesc.descriptorRangeNum; descriptorRangeIndex++)
        {
            const nrd::DescriptorRangeDesc& nrdDescriptorRange = nrdPipelineDesc.descriptorRanges[descriptorRangeIndex];

            for (uint32_t descriptorOffset = 0; descriptorOffset < nrdDescriptorRange.descriptorNum; descriptorOffset++)
            {
                assert(resourceIndex < dispatchDesc.resourceNum);
                const nrd::Resource& resource = dispatchDesc.resources[resourceIndex];

                assert(resource.stateNeeded == nrdDescriptorRange.descriptorType);

                nvrhi::TextureHandle texture;
                switch (resource.type)
                {
                case nrd::ResourceType::IN_MV:
                    texture = renderTargets.MotionVectors;
                    break;
                case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
                    texture = renderTargets.NormalRoughness;
                    break;
                case nrd::ResourceType::IN_VIEWZ:
                    texture = renderTargets.Depth;
                    break;
                case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
                    texture = renderTargets.DiffuseLighting;
                    break;
                case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
                    texture = renderTargets.SpecularLighting;
                    break;
                case nrd::ResourceType::IN_DIFF_CONFIDENCE:
                    texture = renderTargets.DiffuseConfidence;
                    break;
                case nrd::ResourceType::IN_SPEC_CONFIDENCE:
                    texture = renderTargets.SpecularConfidence;
                    break;
                case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
                    texture = renderTargets.DenoisedDiffuseLighting;
                    break;
                case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
                    texture = renderTargets.DenoisedSpecularLighting;
                    break;
                case nrd::ResourceType::TRANSIENT_POOL:
                    texture = m_TransientTextures[resource.indexInPool];
                    break;
                case nrd::ResourceType::PERMANENT_POOL:
                    texture = m_PermanentTextures[resource.indexInPool];
                    break;
                default:
                    assert(!"Unavailable resource type");
                    break;
                }

                assert(texture);

                nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources;
                subresources.baseMipLevel = resource.mipOffset;
                subresources.numMipLevels = resource.mipNum;

                nvrhi::BindingSetItem setItem = nvrhi::BindingSetItem::None();
                setItem.resourceHandle = texture;
                setItem.slot = nrdDescriptorRange.baseRegisterIndex + descriptorOffset;
                setItem.subresources = subresources;
                setItem.type = (nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE)
                    ? nvrhi::ResourceType::Texture_SRV
                    : nvrhi::ResourceType::Texture_UAV;

                setDesc.bindings.push_back(setItem);

                resourceIndex++;
            }
        }

        assert(resourceIndex == dispatchDesc.resourceNum);

        const NrdPipeline& pipeline = m_Pipelines[dispatchDesc.pipelineIndex];

        nvrhi::BindingSetHandle bindingSet = m_BindingCache.GetOrCreateBindingSet(setDesc, pipeline.BindingLayout);

        nvrhi::ComputeState state;
        state.bindings = { bindingSet };
        state.pipeline = pipeline.Pipeline;
        commandList->setComputeState(state);

        commandList->dispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight);

        if (dispatchDesc.name)
        {
            commandList->endMarker();
        }
    }
}

#endif
