/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "SampleScene.h"
#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/BindlessScene.h>
#include <json/value.h>

#include "donut/engine/TextureCache.h"


using namespace donut;
using namespace donut::math;

#include "../shaders/ShaderParameters.h"

void SpotLightWithProfile::Load(Json::Value& node)
{
    engine::SpotLight::Load(node);
    
    profileName = json::Read<std::string>(node["profile"], "");
}

void SpotLightWithProfile::Store(Json::Value& node) const
{
    engine::SpotLight::Store(node);

    node["profile"] = profileName;
}

void CylinderLight::Load(Json::Value& node)
{
    node["name"] >> name;
    node["center"] >> center;
    node["axis"] >> axis;
    node["color"] >> color;
    node["flux"] >> flux;
    node["radius"] >> radius;
    node["length"] >> length;

    axis = normalize(axis);
}

void CylinderLight::Store(Json::Value& node) const
{
    node["name"] << name;
    node["center"] << center;
    node["axis"] << axis;
    node["color"] << color;
    node["flux"] << flux;
    node["radius"] << radius;
    node["length"] << length;
}

void DiskLight::Load(Json::Value& node)
{
    node["name"] >> name;
    node["center"] >> center;
    node["center"] >> normal;
    node["center"] >> color;
    node["flux"] >> flux;
    node["radius"] >> radius;

    normal = normalize(normal);
}

void DiskLight::Store(Json::Value& node) const
{
    node["name"] << name;
    node["center"] << center;
    node["center"] << normal;
    node["center"] << color;
    node["flux"] << flux;
    node["radius"] << radius;
}

void RectLight::Load(Json::Value& node)
{
    node["name"] >> name;
    node["center"] >> center;
    node["normal"] >> normal;
    node["color"] >> color;
    node["flux"] >> flux;
    node["rotation"] >> rotation;
    node["width"] >> width;
    node["height"] >> height;

    normal = normalize(normal);
}

void RectLight::Store(Json::Value& node) const
{
    node["name"] << name;
    node["center"] << center;
    node["normal"] << normal;
    node["color"] << color;
    node["flux"] << flux;
    node["rotation"] << rotation;
    node["width"] << width;
    node["height"] << height;
}

std::shared_ptr<engine::Light> SampleScene::CreateLight(const std::string& type)
{
    if (type == "spot")
    {
        return std::make_shared<SpotLightWithProfile>();
    }
    else if (type == "environment")
    {
        return std::make_shared<EnvironmentLight>();
    }
    else if (type == "cylinder")
    {
        return std::make_shared<CylinderLight>();
    }
    else if (type == "disk")
    {
        return std::make_shared<DiskLight>();
    }
    else if (type == "rect")
    {
        return std::make_shared<RectLight>();
    }

    return engine::Scene::CreateLight(type);
}

void SampleScene::LoadCustomData(Json::Value& rootNode, engine::TextureCache& textureCache, concurrency::task_group& taskGroup, bool& success)
{
    m_CameraPath.Load(rootNode["camera_path"]);

    // Enumerate the available environment maps
    std::vector<std::string> environmentMapNames;
    const std::string texturePath = "/media/environment/";
    m_fs->enumerate(texturePath + "*.exr", false, environmentMapNames);

    m_EnvironmentMaps.clear();
    m_EnvironmentMaps.push_back(""); // Procedural env.map with no name

    for (const std::string& mapName : environmentMapNames)
    {
        m_EnvironmentMaps.push_back(texturePath + mapName);
    }
}

bool SampleScene::Load(const std::filesystem::path& jsonFileName, donut::engine::TextureCache& textureCache)
{
    if (!Scene::Load(jsonFileName, textureCache))
        return false;

    for (auto* material : GetMaterials())
    {
        material->emissiveColor *= 100.f;

        if (material->name.find("StringLights") == std::string::npos)
            continue;

        if (all(material->emissiveColor == 0.f))
            continue;

        AnimatedMaterial am;
        am.radiance = material->emissiveColor;
        am.material = material;
        m_AnimatedMaterials.push_back(am);
    }

    for (auto* instance : GetMeshInstances())
    {
        const std::string& materialName = instance->mesh->material->name;

        if (materialName == "LMBR_0000002_Mesh")
        {
            m_RotatingFans.push_back(instance);
        }
        else if (materialName == "sphere_case")
        {
            m_RotatingSphereParts.push_back(instance);
        }
    }
    
    return true;
}

float SampleScene::GetCameraPathDuration() const
{
    return m_CameraPath.GetEndTime();
}

bool SampleScene::InterpolateCameraPath(const float time, float3& position, float3& direction)
{
    auto optionalView = m_CameraPath.Evaluate(time, false);

    if (!optionalView.has_value())
        return false;

    position = optionalView.value().position;
    direction = optionalView.value().direction;

    if (length(direction) > 0)
        direction = normalize(direction);
    else
        direction = float3(1, 0, 0);

    return true;
}

void SampleScene::BuildMeshBLASes(nvrhi::IDevice* device)
{
    nvrhi::CommandListParameters clparams;
    clparams.scratchChunkSize = clparams.scratchMaxMemory;

    nvrhi::CommandListHandle commandList = device->createCommandList(clparams);
    commandList->open();

    for (auto* mesh : GetMeshes())
    {
        nvrhi::rt::BottomLevelAccelStructDesc blasDesc;
        nvrhi::rt::GeometryDesc geometryDesc;
        auto& triangles = geometryDesc.geometryData.triangles;
        triangles.indexBuffer = mesh->buffers->indexBuffer;
        triangles.indexOffset = mesh->indexOffset * sizeof(uint32_t);
        triangles.indexFormat = nvrhi::Format::R32_UINT;
        triangles.indexCount = mesh->numIndices;
        triangles.vertexBuffer = mesh->buffers->vertexBuffers[int(engine::VertexAttribute::Position)];
        triangles.vertexOffset = mesh->vertexOffset * sizeof(float3);
        triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
        triangles.vertexStride = sizeof(float3);
        triangles.vertexCount = mesh->numVertices;
        geometryDesc.geometryType = nvrhi::rt::AccelStructGeometryType::TRIANGLES;
        geometryDesc.flags = (mesh->material->domain == engine::MD_OPAQUE)
            ? nvrhi::rt::GeometryFlags::OPAQUE_
            : nvrhi::rt::GeometryFlags::NONE;
        blasDesc.geometries.push_back(geometryDesc);
        blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PERFER_FAST_TRACE;
        blasDesc.trackLiveness = false;

        nvrhi::rt::AccelStructHandle as = device->createBottomLevelAccelStruct(blasDesc);
        commandList->buildBottomLevelAccelStruct(as, blasDesc);

        mesh->accelStruct = as;
    }

    const uint32_t maxInstances = uint32_t(GetMeshInstances().size());
    m_TopLevelAS = device->createTopLevelAccelStruct(maxInstances);
    m_PrevTopLevelAS = device->createTopLevelAccelStruct(maxInstances);

    commandList->close();
    device->executeCommandList(commandList);

    device->waitForIdle();
    device->runGarbageCollection();
}

void SampleScene::BuildTopLevelAccelStruct(nvrhi::ICommandList* commandList)
{
    m_TlasDesc.instances.resize(GetMeshInstances().size());

    // TODO - updates are not properly supported by NVRHI
    //if(m_FrameIndex != 0)
    //    tlasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PERFORM_UPDATE;

    uint32_t index = 0;

    for (auto* instance : GetMeshInstances())
    {
        engine::MeshInfo* mesh = instance->mesh;

        if (!mesh->accelStruct)
            continue;

        nvrhi::rt::InstanceDesc& instanceDesc = m_TlasDesc.instances[index++];

        switch (mesh->material->domain)
        {
        case engine::MD_OPAQUE:
            instanceDesc.instanceMask = INSTANCE_MASK_OPAQUE;
            break;
        case engine::MD_ALPHA_TESTED:
            instanceDesc.instanceMask = INSTANCE_MASK_ALPHA_TESTED;
            break;
        case engine::MD_TRANSPARENT:
            if (mesh->material->shininess > 0.9f)
                instanceDesc.instanceMask = INSTANCE_MASK_TRANSPARENT;
            else
                instanceDesc.instanceMask = INSTANCE_MASK_ALPHA_TESTED;
            break;
        default:
            continue;
        }

        instanceDesc.bottomLevelAS = mesh->accelStruct;

        // Copy the transform in the right layout - doing it through the dm library is much slower
        instanceDesc.transform[0][0] = instance->localTransform.m_linear.m00;
        instanceDesc.transform[0][1] = instance->localTransform.m_linear.m10;
        instanceDesc.transform[0][2] = instance->localTransform.m_linear.m20;
        instanceDesc.transform[0][3] = instance->localTransform.m_translation.x;
        instanceDesc.transform[1][0] = instance->localTransform.m_linear.m01;
        instanceDesc.transform[1][1] = instance->localTransform.m_linear.m11;
        instanceDesc.transform[1][2] = instance->localTransform.m_linear.m21;
        instanceDesc.transform[1][3] = instance->localTransform.m_translation.y;
        instanceDesc.transform[2][0] = instance->localTransform.m_linear.m02;
        instanceDesc.transform[2][1] = instance->localTransform.m_linear.m12;
        instanceDesc.transform[2][2] = instance->localTransform.m_linear.m22;
        instanceDesc.transform[2][3] = instance->localTransform.m_translation.z;

        instanceDesc.instanceID = uint(instance->globalInstanceIndex);
    }

    commandList->buildTopLevelAccelStruct(m_TopLevelAS, m_TlasDesc);
}

void SampleScene::NextFrame()
{
    std::swap(m_TopLevelAS, m_PrevTopLevelAS);
}

void SampleScene::Animate(float fElapsedTimeSeconds, bool animateLights, bool animateMeshes, donut::engine::BindlessScene& bindlessScene)
{
    m_WallclockTimeMs += (int)(fElapsedTimeSeconds * 1e3f);

    int index = 0;
    for (auto& am : m_AnimatedMaterials)
    {
        int phase = m_WallclockTimeMs + index * 400;
        bool on = animateLights ? (phase & 2048) != 0 : true;

        am.material->emissiveColor = am.radiance * (on ? 1.f : 0.f);
        bindlessScene.UpdateMaterial(am.material);

        index++;
    }

    for (auto* inst : m_RotatingFans)
    {
        inst->previousTransform = inst->localTransform;

        if (animateMeshes)
            inst->localTransform = dm::rotation(dm::float3(0, 0, 1), fElapsedTimeSeconds * 2.f) * inst->localTransform;

        bindlessScene.UpdateInstance(inst);
    }

    for (auto* inst : m_RotatingSphereParts)
    {
        inst->previousTransform = inst->localTransform;

        if (animateMeshes)
            inst->localTransform = dm::rotation(dm::float3(0, 1, 0), fElapsedTimeSeconds * 3.f) * inst->localTransform;

        bindlessScene.UpdateInstance(inst);
    }
}
