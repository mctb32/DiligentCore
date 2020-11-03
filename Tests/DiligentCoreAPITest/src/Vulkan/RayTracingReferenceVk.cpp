/*
 *  Copyright 2019-2020 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include <functional>

#include "Vulkan/TestingEnvironmentVk.hpp"
#include "Vulkan/TestingSwapChainVk.hpp"
#include "Align.hpp"
#include "BasicMath.hpp"

#include "DeviceContextVk.h"

#include "volk/volk.h"

#include "InlineShaders/RayTracingTestGLSL.h"
#include "RayTracingTestConstants.hpp"

namespace Diligent
{

namespace Testing
{

namespace
{

struct RTContext
{
    struct AccelStruct
    {
        VkDevice                   vkDevice  = VK_NULL_HANDLE;
        VkDeviceMemory             vkMemory  = VK_NULL_HANDLE;
        VkAccelerationStructureKHR vkAS      = VK_NULL_HANDLE;
        VkDeviceAddress            vkAddress = 0;

        AccelStruct()
        {}

        ~AccelStruct()
        {
            if (vkAS)
                vkDestroyAccelerationStructureKHR(vkDevice, vkAS, nullptr);
            if (vkMemory)
                vkFreeMemory(vkDevice, vkMemory, nullptr);
        }
    };

    VkDevice                                vkDevice           = VK_NULL_HANDLE;
    VkCommandBuffer                         vkCmdBuffer        = VK_NULL_HANDLE;
    VkImage                                 vkRenderTarget     = VK_NULL_HANDLE;
    VkImageView                             vkRenderTargetView = VK_NULL_HANDLE;
    VkPipelineLayout                        vkLayout           = VK_NULL_HANDLE;
    VkPipeline                              vkPipeline         = VK_NULL_HANDLE;
    VkDescriptorSetLayout                   vkSetLayout        = VK_NULL_HANDLE;
    VkDescriptorPool                        vkDescriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSet                         vkDescriptorSet    = VK_NULL_HANDLE;
    AccelStruct                             BLAS;
    AccelStruct                             TLAS;
    VkBuffer                                vkSBTBuffer             = VK_NULL_HANDLE;
    VkBuffer                                vkScratchBuffer         = VK_NULL_HANDLE;
    VkBuffer                                vkInstanceBuffer        = VK_NULL_HANDLE;
    VkBuffer                                vkVertexBuffer          = VK_NULL_HANDLE;
    VkBuffer                                vkIndexBuffer           = VK_NULL_HANDLE;
    VkDeviceAddress                         vkScratchBufferAddress  = 0;
    VkDeviceAddress                         vkInstanceBufferAddress = 0;
    VkDeviceAddress                         vkVertexBufferAddress   = 0;
    VkDeviceAddress                         vkIndexBufferAddress    = 0;
    VkDeviceMemory                          vkBufferMemory          = VK_NULL_HANDLE;
    VkPhysicalDeviceLimits                  DeviceLimits            = {};
    VkPhysicalDeviceRayTracingPropertiesKHR RayTracingProps         = {};

    RTContext()
    {}

    ~RTContext()
    {
        if (vkPipeline)
            vkDestroyPipeline(vkDevice, vkPipeline, nullptr);
        if (vkLayout)
            vkDestroyPipelineLayout(vkDevice, vkLayout, nullptr);
        if (vkSetLayout)
            vkDestroyDescriptorSetLayout(vkDevice, vkSetLayout, nullptr);
        if (vkDescriptorPool)
            vkDestroyDescriptorPool(vkDevice, vkDescriptorPool, nullptr);
        if (vkBufferMemory)
            vkFreeMemory(vkDevice, vkBufferMemory, nullptr);
        if (vkSBTBuffer)
            vkDestroyBuffer(vkDevice, vkSBTBuffer, nullptr);
        if (vkScratchBuffer)
            vkDestroyBuffer(vkDevice, vkScratchBuffer, nullptr);
        if (vkVertexBuffer)
            vkDestroyBuffer(vkDevice, vkVertexBuffer, nullptr);
        if (vkIndexBuffer)
            vkDestroyBuffer(vkDevice, vkIndexBuffer, nullptr);
        if (vkInstanceBuffer)
            vkDestroyBuffer(vkDevice, vkInstanceBuffer, nullptr);
    }

    void ClearRenderTarget(TestingSwapChainVk* pTestingSwapChainVk)
    {
        pTestingSwapChainVk->TransitionRenderTarget(vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0);

        VkImageSubresourceRange Range      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue       ClearValue = {};
        vkCmdClearColorImage(vkCmdBuffer, vkRenderTarget, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearValue, 1, &Range);

        pTestingSwapChainVk->TransitionRenderTarget(vkCmdBuffer, VK_IMAGE_LAYOUT_GENERAL, 0);
    }
};

template <typename PSOCtorType>
void InitializeRTContext(RTContext& Ctx, ISwapChain* pSwapChain, PSOCtorType&& PSOCtor)
{
    auto*    pEnv                = TestingEnvironmentVk::GetInstance();
    auto*    pTestingSwapChainVk = ValidatedCast<TestingSwapChainVk>(pSwapChain);
    VkResult res                 = VK_SUCCESS;

    Ctx.vkDevice           = pEnv->GetVkDevice();
    Ctx.vkCmdBuffer        = pEnv->AllocateCommandBuffer();
    Ctx.vkRenderTarget     = pTestingSwapChainVk->GetVkRenderTargetImage();
    Ctx.vkRenderTargetView = pTestingSwapChainVk->GetVkRenderTargetImageView();

    VkPhysicalDeviceProperties2 Props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    Props2.pNext                       = &Ctx.RayTracingProps;
    Ctx.RayTracingProps.sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR;
    vkGetPhysicalDeviceProperties2KHR(pEnv->GetVkPhysicalDevice(), &Props2);

    Ctx.DeviceLimits = Props2.properties.limits;

    // Create ray tracing pipeline
    {
        VkDescriptorSetLayoutCreateInfo                   DescriptorSetCI  = {};
        VkPipelineLayoutCreateInfo                        PipelineLayoutCI = {};
        std::vector<VkDescriptorSetLayoutBinding>         Bindings;
        std::vector<VkShaderModule>                       ShaderModules;
        std::vector<VkPipelineShaderStageCreateInfo>      RTStages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> RTShaderGroups;
        VkRayTracingPipelineCreateInfoKHR                 PipelineCI = {};

        PSOCtor(Bindings, ShaderModules, RTStages, RTShaderGroups);

        VkDescriptorSetLayoutBinding Binding = {};
        Binding.binding                      = 0;
        Binding.descriptorCount              = 1;
        Binding.descriptorType               = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        Binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        Bindings.push_back(Binding);

        Binding.binding        = 1;
        Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Bindings.push_back(Binding);

        DescriptorSetCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        DescriptorSetCI.bindingCount = static_cast<Uint32>(Bindings.size());
        DescriptorSetCI.pBindings    = Bindings.data();

        res = vkCreateDescriptorSetLayout(Ctx.vkDevice, &DescriptorSetCI, nullptr, &Ctx.vkSetLayout);
        ASSERT_GE(res, 0);
        ASSERT_TRUE(Ctx.vkSetLayout != VK_NULL_HANDLE);

        PipelineLayoutCI.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        PipelineLayoutCI.setLayoutCount = 1;
        PipelineLayoutCI.pSetLayouts    = &Ctx.vkSetLayout;

        vkCreatePipelineLayout(Ctx.vkDevice, &PipelineLayoutCI, nullptr, &Ctx.vkLayout);
        ASSERT_TRUE(Ctx.vkLayout != VK_NULL_HANDLE);


        PipelineCI.sType                  = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        PipelineCI.flags                  = 0;
        PipelineCI.stageCount             = static_cast<Uint32>(RTStages.size());
        PipelineCI.pStages                = RTStages.data();
        PipelineCI.groupCount             = static_cast<Uint32>(RTShaderGroups.size());
        PipelineCI.pGroups                = RTShaderGroups.data();
        PipelineCI.maxRecursionDepth      = 0;
        PipelineCI.layout                 = Ctx.vkLayout;
        PipelineCI.libraries.sType        = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
        PipelineCI.libraries.pNext        = nullptr;
        PipelineCI.libraries.libraryCount = 0;
        PipelineCI.libraries.pLibraries   = nullptr;

        res = vkCreateRayTracingPipelinesKHR(Ctx.vkDevice, VK_NULL_HANDLE, 1, &PipelineCI, nullptr, &Ctx.vkPipeline);
        ASSERT_GE(res, 0);
        ASSERT_TRUE(Ctx.vkPipeline != VK_NULL_HANDLE);

        for (auto& SM : ShaderModules)
        {
            vkDestroyShaderModule(Ctx.vkDevice, SM, nullptr);
        }
    }

    // Create descriptor set
    {
        VkDescriptorPoolCreateInfo  DescriptorPoolCI = {};
        VkDescriptorPoolSize        PoolSizes[3]     = {};
        VkDescriptorSetAllocateInfo SetAllocInfo     = {};

        static constexpr uint32_t MaxSetsInPool        = 16;
        static constexpr uint32_t MaxDescriptorsInPool = 16;

        DescriptorPoolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        DescriptorPoolCI.maxSets       = MaxSetsInPool;
        DescriptorPoolCI.poolSizeCount = _countof(PoolSizes);
        DescriptorPoolCI.pPoolSizes    = PoolSizes;

        PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        PoolSizes[0].descriptorCount = MaxDescriptorsInPool;
        PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        PoolSizes[1].descriptorCount = MaxDescriptorsInPool;
        PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        PoolSizes[2].descriptorCount = MaxDescriptorsInPool;

        res = vkCreateDescriptorPool(Ctx.vkDevice, &DescriptorPoolCI, nullptr, &Ctx.vkDescriptorPool);
        ASSERT_GE(res, 0);
        ASSERT_TRUE(Ctx.vkDescriptorPool != VK_NULL_HANDLE);

        SetAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        SetAllocInfo.descriptorPool     = Ctx.vkDescriptorPool;
        SetAllocInfo.descriptorSetCount = 1;
        SetAllocInfo.pSetLayouts        = &Ctx.vkSetLayout;

        vkAllocateDescriptorSets(Ctx.vkDevice, &SetAllocInfo, &Ctx.vkDescriptorSet);
        ASSERT_TRUE(Ctx.vkDescriptorSet != VK_NULL_HANDLE);
    }
}

void UpdateDescriptorSet(RTContext& Ctx)
{
    VkWriteDescriptorSet DescriptorWrite[2] = {};

    DescriptorWrite[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    DescriptorWrite[0].dstSet          = Ctx.vkDescriptorSet;
    DescriptorWrite[0].dstBinding      = 1;
    DescriptorWrite[0].dstArrayElement = 0;
    DescriptorWrite[0].descriptorCount = 1;
    DescriptorWrite[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    DescriptorWrite[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    DescriptorWrite[1].dstSet          = Ctx.vkDescriptorSet;
    DescriptorWrite[1].dstBinding      = 0;
    DescriptorWrite[1].dstArrayElement = 0;
    DescriptorWrite[1].descriptorCount = 1;
    DescriptorWrite[1].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.imageView             = Ctx.vkRenderTargetView;
    ImageInfo.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;
    DescriptorWrite[0].pImageInfo   = &ImageInfo;

    VkWriteDescriptorSetAccelerationStructureKHR TLASInfo = {};

    TLASInfo.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    TLASInfo.accelerationStructureCount = 1;
    TLASInfo.pAccelerationStructures    = &Ctx.TLAS.vkAS;
    DescriptorWrite[1].pNext            = &TLASInfo;

    vkUpdateDescriptorSets(Ctx.vkDevice, _countof(DescriptorWrite), DescriptorWrite, 0, nullptr);
}

void CreateBLAS(const RTContext& Ctx, const VkAccelerationStructureCreateGeometryTypeInfoKHR* pGeometries, Uint32 GeometryCount, RTContext::AccelStruct& BLAS)
{
    BLAS.vkDevice = Ctx.vkDevice;

    VkResult res = VK_SUCCESS;

    VkAccelerationStructureCreateInfoKHR             BLASCI  = {};
    VkAccelerationStructureMemoryRequirementsInfoKHR MemInfo = {};
    VkMemoryRequirements2                            MemReqs = {};

    BLASCI.sType            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    BLASCI.type             = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BLASCI.flags            = 0;
    BLASCI.maxGeometryCount = GeometryCount;
    BLASCI.compactedSize    = 0;
    BLASCI.pGeometryInfos   = pGeometries;

    res = vkCreateAccelerationStructureKHR(Ctx.vkDevice, &BLASCI, nullptr, &BLAS.vkAS);
    ASSERT_GE(res, VK_SUCCESS);
    ASSERT_TRUE(BLAS.vkAS != VK_NULL_HANDLE);

    MemInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
    MemInfo.accelerationStructure = BLAS.vkAS;
    MemInfo.buildType             = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    MemInfo.type                  = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR;

    MemReqs.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);

    VkMemoryAllocateInfo MemAlloc = {};

    MemAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    MemAlloc.allocationSize  = MemReqs.memoryRequirements.size;
    MemAlloc.memoryTypeIndex = TestingEnvironmentVk::GetInstance()->GetMemoryTypeIndex(MemReqs.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_TRUE(MemAlloc.memoryTypeIndex != ~0u);

    res = vkAllocateMemory(Ctx.vkDevice, &MemAlloc, nullptr, &BLAS.vkMemory);
    ASSERT_GE(res, VK_SUCCESS);
    ASSERT_TRUE(BLAS.vkMemory != VK_NULL_HANDLE);

    VkBindAccelerationStructureMemoryInfoKHR BindInfo = {};

    BindInfo.sType                 = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR;
    BindInfo.memory                = BLAS.vkMemory;
    BindInfo.memoryOffset          = 0;
    BindInfo.deviceIndexCount      = 0;
    BindInfo.pDeviceIndices        = nullptr;
    BindInfo.accelerationStructure = BLAS.vkAS;

    res = vkBindAccelerationStructureMemoryKHR(Ctx.vkDevice, 1, &BindInfo);
    ASSERT_GE(res, VK_SUCCESS);

    VkAccelerationStructureDeviceAddressInfoKHR AddressInfo = {};

    AddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    AddressInfo.accelerationStructure = BLAS.vkAS;

    BLAS.vkAddress = vkGetAccelerationStructureDeviceAddressKHR(Ctx.vkDevice, &AddressInfo);
}

void CreateTLAS(const RTContext& Ctx, Uint32 InstanceCount, RTContext::AccelStruct& TLAS)
{
    TLAS.vkDevice = Ctx.vkDevice;

    VkResult res = VK_SUCCESS;

    VkAccelerationStructureCreateInfoKHR             TLASCI    = {};
    VkAccelerationStructureMemoryRequirementsInfoKHR MemInfo   = {};
    VkMemoryRequirements2                            MemReqs   = {};
    VkAccelerationStructureCreateGeometryTypeInfoKHR Instances = {};

    Instances.sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
    Instances.geometryType      = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    Instances.maxPrimitiveCount = InstanceCount;

    TLASCI.sType            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    TLASCI.type             = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TLASCI.flags            = 0;
    TLASCI.compactedSize    = 0;
    TLASCI.maxGeometryCount = 1;
    TLASCI.pGeometryInfos   = &Instances;

    res = vkCreateAccelerationStructureKHR(Ctx.vkDevice, &TLASCI, nullptr, &TLAS.vkAS);
    ASSERT_GE(res, VK_SUCCESS);
    ASSERT_TRUE(TLAS.vkAS != VK_NULL_HANDLE);

    MemInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
    MemInfo.accelerationStructure = TLAS.vkAS;
    MemInfo.buildType             = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;
    MemInfo.type                  = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR;

    MemReqs.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);

    VkMemoryAllocateInfo MemAlloc = {};

    MemAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    MemAlloc.allocationSize  = MemReqs.memoryRequirements.size;
    MemAlloc.memoryTypeIndex = TestingEnvironmentVk::GetInstance()->GetMemoryTypeIndex(MemReqs.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_TRUE(MemAlloc.memoryTypeIndex != ~0u);

    res = vkAllocateMemory(Ctx.vkDevice, &MemAlloc, nullptr, &TLAS.vkMemory);
    ASSERT_GE(res, VK_SUCCESS);
    ASSERT_TRUE(TLAS.vkMemory != VK_NULL_HANDLE);

    VkBindAccelerationStructureMemoryInfoKHR BindInfo = {};

    BindInfo.sType                 = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR;
    BindInfo.memory                = TLAS.vkMemory;
    BindInfo.memoryOffset          = 0;
    BindInfo.deviceIndexCount      = 0;
    BindInfo.pDeviceIndices        = nullptr;
    BindInfo.accelerationStructure = TLAS.vkAS;

    res = vkBindAccelerationStructureMemoryKHR(Ctx.vkDevice, 1, &BindInfo);
    ASSERT_GE(res, VK_SUCCESS);

    VkAccelerationStructureDeviceAddressInfoKHR AddressInfo = {};

    AddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    AddressInfo.accelerationStructure = TLAS.vkAS;

    TLAS.vkAddress = vkGetAccelerationStructureDeviceAddressKHR(Ctx.vkDevice, &AddressInfo);
}

void CreateRTBuffers(RTContext& Ctx, Uint32 VBSize, Uint32 IBSize, Uint32 InstanceCount, Uint32 NumMissShaders, Uint32 NumHitShaders, Uint32 ShaderRecordSize = 0)
{
    VkResult res = VK_SUCCESS;

    VkDeviceSize ScratchSize = 0;
    VkDeviceSize MemSize     = 0;

    VkMemoryRequirements2 MemReqs = {};
    MemReqs.sType                 = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;

    // Get scratch buffer size
    {
        VkAccelerationStructureMemoryRequirementsInfoKHR MemInfo = {};

        MemInfo.sType     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR;
        MemInfo.buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR;

        if (Ctx.BLAS.vkAS)
        {
            MemInfo.accelerationStructure = Ctx.BLAS.vkAS;

            MemInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR;
            vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);
            ScratchSize = std::max(ScratchSize, MemReqs.memoryRequirements.size);

            MemInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_KHR;
            vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);
            ScratchSize = std::max(ScratchSize, MemReqs.memoryRequirements.size);
        }

        if (Ctx.TLAS.vkAS)
        {
            MemInfo.accelerationStructure = Ctx.TLAS.vkAS;

            MemInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR;
            vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);
            ScratchSize = std::max(ScratchSize, MemReqs.memoryRequirements.size);

            MemInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_KHR;
            vkGetAccelerationStructureMemoryRequirementsKHR(Ctx.vkDevice, &MemInfo, &MemReqs);
            ScratchSize = std::max(ScratchSize, MemReqs.memoryRequirements.size);
        }
    }

    VkBufferCreateInfo              BuffCI      = {};
    VkBufferMemoryRequirementsInfo2 MemInfo     = {};
    Uint32                          MemTypeBits = 0;
    VkBufferDeviceAddressInfoKHR    BufferInfo  = {};

    BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;

    BuffCI.sType  = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BuffCI.usage  = VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    MemInfo.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;

    std::vector<std::function<void(VkDeviceMemory Mem, VkDeviceSize & Offset)>> BindMem;

    if (VBSize > 0)
    {
        BuffCI.size = VBSize;
        res         = vkCreateBuffer(Ctx.vkDevice, &BuffCI, nullptr, &Ctx.vkVertexBuffer);
        ASSERT_GE(res, VK_SUCCESS);
        ASSERT_TRUE(Ctx.vkVertexBuffer != VK_NULL_HANDLE);

        MemInfo.buffer = Ctx.vkVertexBuffer;
        vkGetBufferMemoryRequirements2(Ctx.vkDevice, &MemInfo, &MemReqs);

        MemSize = Align(MemSize, MemReqs.memoryRequirements.alignment);
        MemSize += MemReqs.memoryRequirements.size;
        MemTypeBits |= MemReqs.memoryRequirements.memoryTypeBits;

        BindMem.emplace_back([&Ctx, MemReqs, &BufferInfo](VkDeviceMemory Mem, VkDeviceSize& Offset) {
            Offset = Align(Offset, MemReqs.memoryRequirements.alignment);
            vkBindBufferMemory(Ctx.vkDevice, Ctx.vkVertexBuffer, Mem, Offset);
            Offset += MemReqs.memoryRequirements.size;
            BufferInfo.buffer         = Ctx.vkVertexBuffer;
            Ctx.vkVertexBufferAddress = vkGetBufferDeviceAddressKHR(Ctx.vkDevice, &BufferInfo);
            ASSERT_TRUE(Ctx.vkVertexBufferAddress > 0);
        });
    }

    if (IBSize > 0)
    {
        BuffCI.size = VBSize;
        res         = vkCreateBuffer(Ctx.vkDevice, &BuffCI, nullptr, &Ctx.vkIndexBuffer);
        ASSERT_GE(res, VK_SUCCESS);
        ASSERT_TRUE(Ctx.vkIndexBuffer != VK_NULL_HANDLE);

        MemInfo.buffer = Ctx.vkIndexBuffer;
        vkGetBufferMemoryRequirements2(Ctx.vkDevice, &MemInfo, &MemReqs);

        MemSize = Align(MemSize, MemReqs.memoryRequirements.alignment);
        MemSize += MemReqs.memoryRequirements.size;
        MemTypeBits |= MemReqs.memoryRequirements.memoryTypeBits;

        BindMem.emplace_back([&Ctx, MemReqs, &BufferInfo](VkDeviceMemory Mem, VkDeviceSize& Offset) {
            Offset = Align(Offset, MemReqs.memoryRequirements.alignment);
            vkBindBufferMemory(Ctx.vkDevice, Ctx.vkIndexBuffer, Mem, Offset);
            Offset += MemReqs.memoryRequirements.size;
            BufferInfo.buffer        = Ctx.vkIndexBuffer;
            Ctx.vkIndexBufferAddress = vkGetBufferDeviceAddressKHR(Ctx.vkDevice, &BufferInfo);
            ASSERT_TRUE(Ctx.vkIndexBufferAddress > 0);
        });
    }

    if (InstanceCount > 0)
    {
        BuffCI.size = InstanceCount * sizeof(VkAccelerationStructureInstanceKHR);
        res         = vkCreateBuffer(Ctx.vkDevice, &BuffCI, nullptr, &Ctx.vkInstanceBuffer);
        ASSERT_GE(res, VK_SUCCESS);
        ASSERT_TRUE(Ctx.vkInstanceBuffer != VK_NULL_HANDLE);

        MemInfo.buffer = Ctx.vkInstanceBuffer;
        vkGetBufferMemoryRequirements2(Ctx.vkDevice, &MemInfo, &MemReqs);

        MemSize = Align(MemSize, MemReqs.memoryRequirements.alignment);
        MemSize += MemReqs.memoryRequirements.size;
        MemTypeBits |= MemReqs.memoryRequirements.memoryTypeBits;

        BindMem.emplace_back([&Ctx, MemReqs, &BufferInfo](VkDeviceMemory Mem, VkDeviceSize& Offset) {
            Offset = Align(Offset, MemReqs.memoryRequirements.alignment);
            vkBindBufferMemory(Ctx.vkDevice, Ctx.vkInstanceBuffer, Mem, Offset);
            Offset += MemReqs.memoryRequirements.size;
            BufferInfo.buffer           = Ctx.vkInstanceBuffer;
            Ctx.vkInstanceBufferAddress = vkGetBufferDeviceAddressKHR(Ctx.vkDevice, &BufferInfo);
            ASSERT_TRUE(Ctx.vkInstanceBufferAddress > 0);
        });
    }

    if (ScratchSize > 0)
    {
        BuffCI.size = ScratchSize;
        res         = vkCreateBuffer(Ctx.vkDevice, &BuffCI, nullptr, &Ctx.vkScratchBuffer);
        ASSERT_GE(res, VK_SUCCESS);
        ASSERT_TRUE(Ctx.vkScratchBuffer != VK_NULL_HANDLE);

        MemInfo.buffer = Ctx.vkScratchBuffer;
        vkGetBufferMemoryRequirements2(Ctx.vkDevice, &MemInfo, &MemReqs);

        MemSize = Align(MemSize, MemReqs.memoryRequirements.alignment);
        MemSize += MemReqs.memoryRequirements.size;
        MemTypeBits |= MemReqs.memoryRequirements.memoryTypeBits;

        BindMem.emplace_back([&Ctx, MemReqs, &BufferInfo](VkDeviceMemory Mem, VkDeviceSize& Offset) {
            Offset = Align(Offset, MemReqs.memoryRequirements.alignment);
            vkBindBufferMemory(Ctx.vkDevice, Ctx.vkScratchBuffer, Mem, Offset);
            Offset += MemReqs.memoryRequirements.size;
            BufferInfo.buffer          = Ctx.vkScratchBuffer;
            Ctx.vkScratchBufferAddress = vkGetBufferDeviceAddressKHR(Ctx.vkDevice, &BufferInfo);
            ASSERT_TRUE(Ctx.vkScratchBufferAddress > 0);
        });
    }

    // SBT
    {
        const Uint32 GroupSize = Ctx.RayTracingProps.shaderGroupHandleSize + ShaderRecordSize;

        BuffCI.size = Align(GroupSize, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        BuffCI.size = Align(BuffCI.size + GroupSize * NumMissShaders, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        BuffCI.size = Align(BuffCI.size + GroupSize * NumHitShaders, Ctx.RayTracingProps.shaderGroupBaseAlignment);

        res = vkCreateBuffer(Ctx.vkDevice, &BuffCI, nullptr, &Ctx.vkSBTBuffer);
        ASSERT_GE(res, VK_SUCCESS);
        ASSERT_TRUE(Ctx.vkSBTBuffer != VK_NULL_HANDLE);

        MemInfo.buffer = Ctx.vkSBTBuffer;
        vkGetBufferMemoryRequirements2(Ctx.vkDevice, &MemInfo, &MemReqs);

        MemSize = Align(MemSize, MemReqs.memoryRequirements.alignment);
        MemSize += MemReqs.memoryRequirements.size;
        MemTypeBits |= MemReqs.memoryRequirements.memoryTypeBits;

        BindMem.emplace_back([&Ctx, MemReqs, &BufferInfo](VkDeviceMemory Mem, VkDeviceSize& Offset) {
            Offset = Align(Offset, MemReqs.memoryRequirements.alignment);
            vkBindBufferMemory(Ctx.vkDevice, Ctx.vkSBTBuffer, Mem, Offset);
            Offset += MemReqs.memoryRequirements.size;
        });
    }

    VkMemoryAllocateInfo      MemAlloc    = {};
    VkMemoryAllocateFlagsInfo MemFlagInfo = {};

    MemAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    MemAlloc.allocationSize  = MemSize;
    MemAlloc.memoryTypeIndex = TestingEnvironmentVk::GetInstance()->GetMemoryTypeIndex(MemReqs.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_TRUE(MemAlloc.memoryTypeIndex != ~0u);

    MemAlloc.pNext    = &MemFlagInfo;
    MemFlagInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    MemFlagInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    res = vkAllocateMemory(Ctx.vkDevice, &MemAlloc, nullptr, &Ctx.vkBufferMemory);
    ASSERT_GE(res, VK_SUCCESS);
    ASSERT_TRUE(Ctx.vkBufferMemory != VK_NULL_HANDLE);

    VkDeviceSize Offset = 0;
    for (auto& Bind : BindMem)
    {
        Bind(Ctx.vkBufferMemory, Offset);
    }
    ASSERT_GE(MemSize, Offset);
}

void ClearRenderTarget(RTContext& Ctx, TestingSwapChainVk* pTestingSwapChainVk)
{
    pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0);

    VkImageSubresourceRange Range      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkClearColorValue       ClearValue = {};
    vkCmdClearColorImage(Ctx.vkCmdBuffer, Ctx.vkRenderTarget, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearValue, 1, &Range);

    pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_GENERAL, 0);
}
} // namespace


void RayTracingTriangleClosestHitReferenceVk(ISwapChain* pSwapChain)
{
    enum
    {
        RAYGEN_SHADER,
        MISS_SHADER,
        HIT_SHADER,
        NUM_SHADERS
    };
    enum
    {
        RAYGEN_GROUP,
        MISS_GROUP,
        HIT_GROUP,
        NUM_GROUPS
    };

    auto* pEnv                = TestingEnvironmentVk::GetInstance();
    auto* pTestingSwapChainVk = ValidatedCast<TestingSwapChainVk>(pSwapChain);

    const auto& SCDesc = pSwapChain->GetDesc();

    RTContext Ctx = {};
    InitializeRTContext(Ctx, pSwapChain,
                        [pEnv](auto& Bindings, auto& Modules, auto& Stages, auto& Groups) {
                            Modules.resize(NUM_SHADERS);
                            Stages.resize(NUM_SHADERS);
                            Groups.resize(NUM_GROUPS);

                            Modules[RAYGEN_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_GEN, GLSL::RayTracingTest1_RG);
                            Stages[RAYGEN_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[RAYGEN_SHADER].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                            Stages[RAYGEN_SHADER].module = Modules[RAYGEN_SHADER];
                            Stages[RAYGEN_SHADER].pName  = "main";

                            Modules[MISS_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_MISS, GLSL::RayTracingTest1_RM);
                            Stages[MISS_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[MISS_SHADER].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
                            Stages[MISS_SHADER].module = Modules[MISS_SHADER];
                            Stages[MISS_SHADER].pName  = "main";

                            Modules[HIT_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_CLOSEST_HIT, GLSL::RayTracingTest1_RCH);
                            Stages[HIT_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[HIT_SHADER].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                            Stages[HIT_SHADER].module = Modules[HIT_SHADER];
                            Stages[HIT_SHADER].pName  = "main";

                            Groups[RAYGEN_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[RAYGEN_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[RAYGEN_GROUP].generalShader      = RAYGEN_SHADER;
                            Groups[RAYGEN_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[HIT_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[HIT_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                            Groups[HIT_GROUP].generalShader      = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP].closestHitShader   = HIT_SHADER;
                            Groups[HIT_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[MISS_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[MISS_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[MISS_GROUP].generalShader      = MISS_SHADER;
                            Groups[MISS_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;
                        });

    // Create acceleration structures
    {
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

        const auto& Vertices = TestingConstants::TriangleClosestHit::Vertices;

        VkAccelerationStructureCreateGeometryTypeInfoKHR GeometryCI = {};

        GeometryCI.sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI.geometryType      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        GeometryCI.maxPrimitiveCount = 1;
        GeometryCI.indexType         = VK_INDEX_TYPE_NONE_KHR;
        GeometryCI.maxVertexCount    = _countof(Vertices);
        GeometryCI.vertexFormat      = VK_FORMAT_R32G32B32_SFLOAT;
        GeometryCI.allowsTransforms  = VK_FALSE;

        CreateBLAS(Ctx, &GeometryCI, 1, Ctx.BLAS);
        CreateTLAS(Ctx, 1, Ctx.TLAS);
        CreateRTBuffers(Ctx, sizeof(Vertices), 0, 1, 1, 1);

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkVertexBuffer, 0, sizeof(Vertices), Vertices);

        // barrier for vertex & index buffers
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        VkAccelerationStructureBuildGeometryInfoKHR      ASBuildInfo = {};
        VkAccelerationStructureBuildOffsetInfoKHR        Offset      = {};
        VkAccelerationStructureGeometryKHR               Geometry    = {};
        VkAccelerationStructureGeometryKHR const*        GeometryPtr = &Geometry;
        VkAccelerationStructureBuildOffsetInfoKHR const* OffsetPtr   = &Offset;

        Geometry.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometry.flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Geometry.geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geometry.geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Geometry.geometry.triangles.vertexFormat             = GeometryCI.vertexFormat;
        Geometry.geometry.triangles.vertexStride             = sizeof(Vertices[0]);
        Geometry.geometry.triangles.vertexData.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometry.geometry.triangles.indexType                = VK_INDEX_TYPE_NONE_KHR;

        Offset.primitiveCount  = GeometryCI.maxPrimitiveCount;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.BLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);

        VkAccelerationStructureInstanceKHR InstanceData     = {};
        InstanceData.instanceCustomIndex                    = 0;
        InstanceData.instanceShaderBindingTableRecordOffset = 0;
        InstanceData.mask                                   = 0xFF;
        InstanceData.flags                                  = 0;
        InstanceData.accelerationStructureReference         = Ctx.BLAS.vkAddress;
        InstanceData.transform.matrix[0][0]                 = 1.0f;
        InstanceData.transform.matrix[1][1]                 = 1.0f;
        InstanceData.transform.matrix[2][2]                 = 1.0f;

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkInstanceBuffer, 0, sizeof(InstanceData), &InstanceData);

        // barrier for BLAS, scratch buffer, instance buffer
        Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        Geometry.flags                                 = 0;
        Geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        Geometry.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        Geometry.geometry.instances.pNext              = nullptr;
        Geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
        Geometry.geometry.instances.data.deviceAddress = Ctx.vkInstanceBufferAddress;

        Offset.primitiveCount  = 1;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.TLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);
    }

    Ctx.ClearRenderTarget(pTestingSwapChainVk);

    UpdateDescriptorSet(Ctx);

    // Trace rays
    {
        VkStridedBufferRegionKHR RaygenShaderBindingTable   = {};
        VkStridedBufferRegionKHR MissShaderBindingTable     = {};
        VkStridedBufferRegionKHR HitShaderBindingTable      = {};
        VkStridedBufferRegionKHR CallableShaderBindingTable = {};
        const Uint32             ShaderGroupHandleSize      = Ctx.RayTracingProps.shaderGroupHandleSize;

        RaygenShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        RaygenShaderBindingTable.offset = 0;
        RaygenShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride   = ShaderGroupHandleSize;

        MissShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        MissShaderBindingTable.offset = Align(RaygenShaderBindingTable.offset + RaygenShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        MissShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride = ShaderGroupHandleSize;

        HitShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        HitShaderBindingTable.offset = Align(MissShaderBindingTable.offset + MissShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        HitShaderBindingTable.size   = ShaderGroupHandleSize;
        HitShaderBindingTable.stride = ShaderGroupHandleSize;

        char ShaderHandle[64] = {};
        ASSERT_GE(sizeof(ShaderHandle), ShaderGroupHandleSize);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, RAYGEN_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, RaygenShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, MISS_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, MissShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, HIT_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, HitShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        // Barriers for TLAS & SBT
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkPipeline);
        vkCmdBindDescriptorSets(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkLayout, 0, 1, &Ctx.vkDescriptorSet, 0, nullptr);

        vkCmdTraceRaysKHR(Ctx.vkCmdBuffer, &RaygenShaderBindingTable, &MissShaderBindingTable, &HitShaderBindingTable, &CallableShaderBindingTable, SCDesc.Width, SCDesc.Height, 1);

        pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0);
    }

    auto res = vkEndCommandBuffer(Ctx.vkCmdBuffer);
    EXPECT_TRUE(res >= 0) << "Failed to end command buffer";

    pEnv->SubmitCommandBuffer(Ctx.vkCmdBuffer, true);
}


void RayTracingTriangleAnyHitReferenceVk(ISwapChain* pSwapChain)
{
    enum
    {
        RAYGEN_SHADER,
        MISS_SHADER,
        HIT_SHADER,
        ANY_HIT_SHADER,
        NUM_SHADERS
    };
    enum
    {
        RAYGEN_GROUP,
        MISS_GROUP,
        HIT_GROUP,
        NUM_GROUPS
    };

    auto* pEnv                = TestingEnvironmentVk::GetInstance();
    auto* pTestingSwapChainVk = ValidatedCast<TestingSwapChainVk>(pSwapChain);

    const auto& SCDesc = pSwapChain->GetDesc();

    RTContext Ctx = {};
    InitializeRTContext(Ctx, pSwapChain,
                        [pEnv](auto& Bindings, auto& Modules, auto& Stages, auto& Groups) {
                            Modules.resize(NUM_SHADERS);
                            Stages.resize(NUM_SHADERS);
                            Groups.resize(NUM_GROUPS);

                            Modules[RAYGEN_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_GEN, GLSL::RayTracingTest2_RG);
                            Stages[RAYGEN_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[RAYGEN_SHADER].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                            Stages[RAYGEN_SHADER].module = Modules[RAYGEN_SHADER];
                            Stages[RAYGEN_SHADER].pName  = "main";

                            Modules[MISS_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_MISS, GLSL::RayTracingTest2_RM);
                            Stages[MISS_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[MISS_SHADER].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
                            Stages[MISS_SHADER].module = Modules[MISS_SHADER];
                            Stages[MISS_SHADER].pName  = "main";

                            Modules[HIT_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_CLOSEST_HIT, GLSL::RayTracingTest2_RCH);
                            Stages[HIT_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[HIT_SHADER].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                            Stages[HIT_SHADER].module = Modules[HIT_SHADER];
                            Stages[HIT_SHADER].pName  = "main";

                            Modules[ANY_HIT_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_ANY_HIT, GLSL::RayTracingTest2_RAH);
                            Stages[ANY_HIT_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[ANY_HIT_SHADER].stage  = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                            Stages[ANY_HIT_SHADER].module = Modules[ANY_HIT_SHADER];
                            Stages[ANY_HIT_SHADER].pName  = "main";

                            Groups[RAYGEN_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[RAYGEN_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[RAYGEN_GROUP].generalShader      = RAYGEN_SHADER;
                            Groups[RAYGEN_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[MISS_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[MISS_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[MISS_GROUP].generalShader      = MISS_SHADER;
                            Groups[MISS_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[HIT_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[HIT_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                            Groups[HIT_GROUP].generalShader      = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP].closestHitShader   = HIT_SHADER;
                            Groups[HIT_GROUP].anyHitShader       = ANY_HIT_SHADER;
                            Groups[HIT_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;
                        });

    // create acceleration structurea
    {
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

        const auto& Vertices = TestingConstants::TriangleAnyHit::Vertices;

        VkAccelerationStructureCreateGeometryTypeInfoKHR GeometryCI = {};

        GeometryCI.sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI.geometryType      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        GeometryCI.maxPrimitiveCount = 3;
        GeometryCI.indexType         = VK_INDEX_TYPE_NONE_KHR;
        GeometryCI.maxVertexCount    = _countof(Vertices);
        GeometryCI.vertexFormat      = VK_FORMAT_R32G32B32_SFLOAT;
        GeometryCI.allowsTransforms  = VK_FALSE;

        CreateBLAS(Ctx, &GeometryCI, 1, Ctx.BLAS);
        CreateTLAS(Ctx, 1, Ctx.TLAS);
        CreateRTBuffers(Ctx, sizeof(Vertices), 0, 1, 1, 1);

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkVertexBuffer, 0, sizeof(Vertices), Vertices);

        // barrier for vertex & index buffers
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        VkAccelerationStructureBuildGeometryInfoKHR      ASBuildInfo = {};
        VkAccelerationStructureBuildOffsetInfoKHR        Offset      = {};
        VkAccelerationStructureGeometryKHR               Geometry    = {};
        VkAccelerationStructureGeometryKHR const*        GeometryPtr = &Geometry;
        VkAccelerationStructureBuildOffsetInfoKHR const* OffsetPtr   = &Offset;

        Geometry.sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometry.flags                                       = 0;
        Geometry.geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geometry.geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Geometry.geometry.triangles.vertexFormat             = GeometryCI.vertexFormat;
        Geometry.geometry.triangles.vertexStride             = sizeof(Vertices[0]);
        Geometry.geometry.triangles.vertexData.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometry.geometry.triangles.indexType                = VK_INDEX_TYPE_NONE_KHR;

        Offset.primitiveCount  = GeometryCI.maxPrimitiveCount;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.BLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);

        VkAccelerationStructureInstanceKHR InstanceData     = {};
        InstanceData.instanceCustomIndex                    = 0;
        InstanceData.instanceShaderBindingTableRecordOffset = 0;
        InstanceData.mask                                   = 0xFF;
        InstanceData.flags                                  = 0;
        InstanceData.accelerationStructureReference         = Ctx.BLAS.vkAddress;
        InstanceData.transform.matrix[0][0]                 = 1.0f;
        InstanceData.transform.matrix[1][1]                 = 1.0f;
        InstanceData.transform.matrix[2][2]                 = 1.0f;

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkInstanceBuffer, 0, sizeof(InstanceData), &InstanceData);

        // barrier for BLAS, scratch buffer, instance buffer
        Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        Geometry.flags                                 = 0;
        Geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        Geometry.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        Geometry.geometry.instances.pNext              = nullptr;
        Geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
        Geometry.geometry.instances.data.deviceAddress = Ctx.vkInstanceBufferAddress;

        Offset.primitiveCount  = 1;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.TLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);
    }

    Ctx.ClearRenderTarget(pTestingSwapChainVk);

    UpdateDescriptorSet(Ctx);

    // Trace rays
    {
        VkStridedBufferRegionKHR RaygenShaderBindingTable   = {};
        VkStridedBufferRegionKHR MissShaderBindingTable     = {};
        VkStridedBufferRegionKHR HitShaderBindingTable      = {};
        VkStridedBufferRegionKHR CallableShaderBindingTable = {};
        const Uint32             ShaderGroupHandleSize      = Ctx.RayTracingProps.shaderGroupHandleSize;

        RaygenShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        RaygenShaderBindingTable.offset = 0;
        RaygenShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride   = ShaderGroupHandleSize;

        MissShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        MissShaderBindingTable.offset = Align(RaygenShaderBindingTable.offset + RaygenShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        MissShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride = ShaderGroupHandleSize;

        HitShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        HitShaderBindingTable.offset = Align(MissShaderBindingTable.offset + MissShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        HitShaderBindingTable.size   = ShaderGroupHandleSize;
        HitShaderBindingTable.stride = ShaderGroupHandleSize;

        char ShaderHandle[64] = {};
        ASSERT_GE(sizeof(ShaderHandle), ShaderGroupHandleSize);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, RAYGEN_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, RaygenShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, MISS_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, MissShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, HIT_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, HitShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        // Barriers for TLAS & SBT
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkPipeline);
        vkCmdBindDescriptorSets(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkLayout, 0, 1, &Ctx.vkDescriptorSet, 0, nullptr);

        vkCmdTraceRaysKHR(Ctx.vkCmdBuffer, &RaygenShaderBindingTable, &MissShaderBindingTable, &HitShaderBindingTable, &CallableShaderBindingTable, SCDesc.Width, SCDesc.Height, 1);

        pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0);
    }

    auto res = vkEndCommandBuffer(Ctx.vkCmdBuffer);
    EXPECT_TRUE(res >= 0) << "Failed to end command buffer";

    pEnv->SubmitCommandBuffer(Ctx.vkCmdBuffer, true);
}


void RayTracingProceduralIntersectionReferenceVk(ISwapChain* pSwapChain)
{
    enum
    {
        RAYGEN_SHADER,
        MISS_SHADER,
        HIT_SHADER,
        INTERSECTION_SHADER,
        NUM_SHADERS
    };
    enum
    {
        RAYGEN_GROUP,
        MISS_GROUP,
        HIT_GROUP,
        NUM_GROUPS
    };

    auto* pEnv                = TestingEnvironmentVk::GetInstance();
    auto* pTestingSwapChainVk = ValidatedCast<TestingSwapChainVk>(pSwapChain);

    const auto& SCDesc = pSwapChain->GetDesc();

    RTContext Ctx = {};
    InitializeRTContext(Ctx, pSwapChain,
                        [pEnv](auto& Bindings, auto& Modules, auto& Stages, auto& Groups) {
                            Modules.resize(NUM_SHADERS);
                            Stages.resize(NUM_SHADERS);
                            Groups.resize(NUM_GROUPS);

                            Modules[RAYGEN_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_GEN, GLSL::RayTracingTest3_RG);
                            Stages[RAYGEN_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[RAYGEN_SHADER].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                            Stages[RAYGEN_SHADER].module = Modules[RAYGEN_SHADER];
                            Stages[RAYGEN_SHADER].pName  = "main";

                            Modules[MISS_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_MISS, GLSL::RayTracingTest3_RM);
                            Stages[MISS_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[MISS_SHADER].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
                            Stages[MISS_SHADER].module = Modules[MISS_SHADER];
                            Stages[MISS_SHADER].pName  = "main";

                            Modules[HIT_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_CLOSEST_HIT, GLSL::RayTracingTest3_RCH);
                            Stages[HIT_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[HIT_SHADER].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                            Stages[HIT_SHADER].module = Modules[HIT_SHADER];
                            Stages[HIT_SHADER].pName  = "main";

                            Modules[INTERSECTION_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_INTERSECTION, GLSL::RayTracingTest3_RI);
                            Stages[INTERSECTION_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[INTERSECTION_SHADER].stage  = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                            Stages[INTERSECTION_SHADER].module = Modules[INTERSECTION_SHADER];
                            Stages[INTERSECTION_SHADER].pName  = "main";

                            Groups[RAYGEN_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[RAYGEN_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[RAYGEN_GROUP].generalShader      = RAYGEN_SHADER;
                            Groups[RAYGEN_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[HIT_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[HIT_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                            Groups[HIT_GROUP].generalShader      = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP].closestHitShader   = HIT_SHADER;
                            Groups[HIT_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP].intersectionShader = INTERSECTION_SHADER;

                            Groups[MISS_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[MISS_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[MISS_GROUP].generalShader      = MISS_SHADER;
                            Groups[MISS_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;
                        });

    // create acceleration structurea
    {
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

        const auto& Boxes = TestingConstants::ProceduralIntersection::Boxes;

        VkAccelerationStructureCreateGeometryTypeInfoKHR GeometryCI = {};

        GeometryCI.sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI.geometryType      = VK_GEOMETRY_TYPE_AABBS_KHR;
        GeometryCI.maxPrimitiveCount = 1;
        GeometryCI.indexType         = VK_INDEX_TYPE_NONE_KHR;

        CreateBLAS(Ctx, &GeometryCI, 1, Ctx.BLAS);
        CreateTLAS(Ctx, 1, Ctx.TLAS);
        CreateRTBuffers(Ctx, sizeof(Boxes), 0, 1, 1, 1);

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkVertexBuffer, 0, sizeof(Boxes), Boxes);

        // barrier for vertex & index buffers
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        VkAccelerationStructureBuildGeometryInfoKHR      ASBuildInfo = {};
        VkAccelerationStructureBuildOffsetInfoKHR        Offset      = {};
        VkAccelerationStructureGeometryKHR               Geometry    = {};
        VkAccelerationStructureGeometryKHR const*        GeometryPtr = &Geometry;
        VkAccelerationStructureBuildOffsetInfoKHR const* OffsetPtr   = &Offset;

        Geometry.sType                             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometry.flags                             = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Geometry.geometryType                      = VK_GEOMETRY_TYPE_AABBS_KHR;
        Geometry.geometry.aabbs.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        Geometry.geometry.aabbs.pNext              = nullptr;
        Geometry.geometry.aabbs.data.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometry.geometry.aabbs.stride             = sizeof(float3) * 2;

        Offset.primitiveCount  = GeometryCI.maxPrimitiveCount;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.BLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);

        VkAccelerationStructureInstanceKHR InstanceData     = {};
        InstanceData.instanceCustomIndex                    = 0;
        InstanceData.instanceShaderBindingTableRecordOffset = 0;
        InstanceData.mask                                   = 0xFF;
        InstanceData.flags                                  = 0;
        InstanceData.accelerationStructureReference         = Ctx.BLAS.vkAddress;
        InstanceData.transform.matrix[0][0]                 = 1.0f;
        InstanceData.transform.matrix[1][1]                 = 1.0f;
        InstanceData.transform.matrix[2][2]                 = 1.0f;

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkInstanceBuffer, 0, sizeof(InstanceData), &InstanceData);

        // Barrier for BLAS, scratch buffer, instance buffer
        Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        Geometry.flags                                 = 0;
        Geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        Geometry.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        Geometry.geometry.instances.pNext              = nullptr;
        Geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
        Geometry.geometry.instances.data.deviceAddress = Ctx.vkInstanceBufferAddress;

        Offset.primitiveCount  = 1;
        Offset.firstVertex     = 0;
        Offset.primitiveOffset = 0;
        Offset.transformOffset = 0;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.TLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometryPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);
    }

    Ctx.ClearRenderTarget(pTestingSwapChainVk);

    UpdateDescriptorSet(Ctx);

    // Trace rays
    {
        VkStridedBufferRegionKHR RaygenShaderBindingTable   = {};
        VkStridedBufferRegionKHR MissShaderBindingTable     = {};
        VkStridedBufferRegionKHR HitShaderBindingTable      = {};
        VkStridedBufferRegionKHR CallableShaderBindingTable = {};
        const Uint32             ShaderGroupHandleSize      = Ctx.RayTracingProps.shaderGroupHandleSize;

        RaygenShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        RaygenShaderBindingTable.offset = 0;
        RaygenShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride   = ShaderGroupHandleSize;

        MissShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        MissShaderBindingTable.offset = Align(RaygenShaderBindingTable.offset + RaygenShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        MissShaderBindingTable.size   = ShaderGroupHandleSize;
        MissShaderBindingTable.stride = ShaderGroupHandleSize;

        HitShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        HitShaderBindingTable.offset = Align(MissShaderBindingTable.offset + MissShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        HitShaderBindingTable.size   = ShaderGroupHandleSize;
        HitShaderBindingTable.stride = ShaderGroupHandleSize;

        char ShaderHandle[64] = {};
        ASSERT_GE(sizeof(ShaderHandle), ShaderGroupHandleSize);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, RAYGEN_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, RaygenShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, MISS_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, MissShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, HIT_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, HitShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        // barrier for TLAS & SBT
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkPipeline);
        vkCmdBindDescriptorSets(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkLayout, 0, 1, &Ctx.vkDescriptorSet, 0, nullptr);

        vkCmdTraceRaysKHR(Ctx.vkCmdBuffer, &RaygenShaderBindingTable, &MissShaderBindingTable, &HitShaderBindingTable, &CallableShaderBindingTable, SCDesc.Width, SCDesc.Height, 1);

        pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0);
    }

    auto res = vkEndCommandBuffer(Ctx.vkCmdBuffer);
    EXPECT_TRUE(res >= 0) << "Failed to end command buffer";

    pEnv->SubmitCommandBuffer(Ctx.vkCmdBuffer, true);
}


void RayTracingMultiGeometryReferenceVk(ISwapChain* pSwapChain)
{
    static constexpr Uint32 InstanceCount = TestingConstants::MultiGeometry::InstanceCount;
    static constexpr Uint32 GeometryCount = 3;
    static constexpr Uint32 HitGroupCount = InstanceCount * GeometryCount;

    enum
    {
        RAYGEN_SHADER,
        MISS_SHADER,
        HIT_SHADER_1,
        HIT_SHADER_2,
        NUM_SHADERS
    };
    enum
    {
        RAYGEN_GROUP,
        MISS_GROUP,
        HIT_GROUP_1,
        HIT_GROUP_2,
        NUM_GROUPS
    };

    auto* pEnv                = TestingEnvironmentVk::GetInstance();
    auto* pTestingSwapChainVk = ValidatedCast<TestingSwapChainVk>(pSwapChain);

    const auto& SCDesc = pSwapChain->GetDesc();

    RTContext Ctx = {};
    InitializeRTContext(Ctx, pSwapChain,
                        [pEnv](auto& Bindings, auto& Modules, auto& Stages, auto& Groups) {
                            Bindings.resize(3);
                            Bindings[0] = {2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InstanceCount, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr};
                            Bindings[1] = {3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr};
                            Bindings[2] = {4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr};

                            Modules.resize(NUM_SHADERS);
                            Stages.resize(NUM_SHADERS);
                            Groups.resize(NUM_GROUPS);

                            Modules[RAYGEN_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_GEN, GLSL::RayTracingTest4_RG);
                            Stages[RAYGEN_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[RAYGEN_SHADER].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                            Stages[RAYGEN_SHADER].module = Modules[RAYGEN_SHADER];
                            Stages[RAYGEN_SHADER].pName  = "main";

                            Modules[MISS_SHADER]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_MISS, GLSL::RayTracingTest4_RM);
                            Stages[MISS_SHADER].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[MISS_SHADER].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
                            Stages[MISS_SHADER].module = Modules[MISS_SHADER];
                            Stages[MISS_SHADER].pName  = "main";

                            Modules[HIT_SHADER_1]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_CLOSEST_HIT, GLSL::RayTracingTest4_RCH1);
                            Stages[HIT_SHADER_1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[HIT_SHADER_1].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                            Stages[HIT_SHADER_1].module = Modules[HIT_SHADER_1];
                            Stages[HIT_SHADER_1].pName  = "main";

                            Modules[HIT_SHADER_2]       = pEnv->CreateShaderModule(SHADER_TYPE_RAY_CLOSEST_HIT, GLSL::RayTracingTest4_RCH2);
                            Stages[HIT_SHADER_2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                            Stages[HIT_SHADER_2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                            Stages[HIT_SHADER_2].module = Modules[HIT_SHADER_2];
                            Stages[HIT_SHADER_2].pName  = "main";

                            Groups[RAYGEN_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[RAYGEN_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[RAYGEN_GROUP].generalShader      = RAYGEN_SHADER;
                            Groups[RAYGEN_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[RAYGEN_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[HIT_GROUP_1].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[HIT_GROUP_1].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                            Groups[HIT_GROUP_1].generalShader      = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP_1].closestHitShader   = HIT_SHADER_1;
                            Groups[HIT_GROUP_1].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP_1].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[HIT_GROUP_2].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[HIT_GROUP_2].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                            Groups[HIT_GROUP_2].generalShader      = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP_2].closestHitShader   = HIT_SHADER_2;
                            Groups[HIT_GROUP_2].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[HIT_GROUP_2].intersectionShader = VK_SHADER_UNUSED_KHR;

                            Groups[MISS_GROUP].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                            Groups[MISS_GROUP].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                            Groups[MISS_GROUP].generalShader      = MISS_SHADER;
                            Groups[MISS_GROUP].closestHitShader   = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].anyHitShader       = VK_SHADER_UNUSED_KHR;
                            Groups[MISS_GROUP].intersectionShader = VK_SHADER_UNUSED_KHR;
                        });

    const auto& PrimitiveOffsets = TestingConstants::MultiGeometry::PrimitiveOffsets;
    const auto& Primitives       = TestingConstants::MultiGeometry::Primitives;

    // create acceleration structurea
    {
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

        const auto& Vertices = TestingConstants::MultiGeometry::Vertices;
        const auto& Indices  = TestingConstants::MultiGeometry::Indices;

        VkAccelerationStructureCreateGeometryTypeInfoKHR GeometryCI[3] = {};

        GeometryCI[0].sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI[0].geometryType      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        GeometryCI[0].maxPrimitiveCount = PrimitiveOffsets[1];
        GeometryCI[0].indexType         = VK_INDEX_TYPE_UINT32;
        GeometryCI[0].maxVertexCount    = _countof(Vertices);
        GeometryCI[0].vertexFormat      = VK_FORMAT_R32G32B32_SFLOAT;
        GeometryCI[0].allowsTransforms  = VK_FALSE;

        GeometryCI[1].sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI[1].geometryType      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        GeometryCI[1].maxPrimitiveCount = PrimitiveOffsets[2] - PrimitiveOffsets[1];
        GeometryCI[1].indexType         = VK_INDEX_TYPE_UINT32;
        GeometryCI[1].maxVertexCount    = _countof(Vertices);
        GeometryCI[1].vertexFormat      = VK_FORMAT_R32G32B32_SFLOAT;
        GeometryCI[1].allowsTransforms  = VK_FALSE;

        GeometryCI[2].sType             = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR;
        GeometryCI[2].geometryType      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        GeometryCI[2].maxPrimitiveCount = _countof(Primitives) - PrimitiveOffsets[2];
        GeometryCI[2].indexType         = VK_INDEX_TYPE_UINT32;
        GeometryCI[2].maxVertexCount    = _countof(Vertices);
        GeometryCI[2].vertexFormat      = VK_FORMAT_R32G32B32_SFLOAT;
        GeometryCI[2].allowsTransforms  = VK_FALSE;

        CreateBLAS(Ctx, GeometryCI, _countof(GeometryCI), Ctx.BLAS);
        CreateTLAS(Ctx, 1, Ctx.TLAS);
        CreateRTBuffers(Ctx, sizeof(Vertices), sizeof(Indices), InstanceCount, 1, HitGroupCount, TestingConstants::MultiGeometry::ShaderRecordSize);

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkVertexBuffer, 0, sizeof(Vertices), Vertices);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkIndexBuffer, 0, sizeof(Indices), Indices);

        // barrier for vertex & index buffers
        Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        VkAccelerationStructureBuildGeometryInfoKHR      ASBuildInfo   = {};
        VkAccelerationStructureBuildOffsetInfoKHR        Offsets[3]    = {};
        VkAccelerationStructureGeometryKHR               Geometries[3] = {};
        VkAccelerationStructureGeometryKHR const*        GeometriyPtr  = Geometries;
        VkAccelerationStructureBuildOffsetInfoKHR const* OffsetPtr     = Offsets;
        static_assert(_countof(Offsets) == _countof(Geometries), "size mismatch");
        static_assert(_countof(GeometryCI) == _countof(Geometries), "size mismatch");
        static_assert(GeometryCount == _countof(Geometries), "size mismatch");

        Geometries[0].sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometries[0].flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Geometries[0].geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geometries[0].geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Geometries[0].geometry.triangles.vertexFormat             = GeometryCI[0].vertexFormat;
        Geometries[0].geometry.triangles.vertexStride             = sizeof(Vertices[0]);
        Geometries[0].geometry.triangles.vertexData.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometries[0].geometry.triangles.indexType                = GeometryCI[0].indexType;
        Geometries[0].geometry.triangles.indexData.deviceAddress  = Ctx.vkIndexBufferAddress + PrimitiveOffsets[0] * sizeof(uint) * 3;

        Geometries[1].sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometries[1].flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Geometries[1].geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geometries[1].geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Geometries[1].geometry.triangles.vertexFormat             = GeometryCI[1].vertexFormat;
        Geometries[1].geometry.triangles.vertexStride             = sizeof(Vertices[0]);
        Geometries[1].geometry.triangles.vertexData.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometries[1].geometry.triangles.indexType                = GeometryCI[1].indexType;
        Geometries[1].geometry.triangles.indexData.deviceAddress  = Ctx.vkIndexBufferAddress + PrimitiveOffsets[1] * sizeof(uint) * 3;

        Geometries[2].sType                                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Geometries[2].flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        Geometries[2].geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Geometries[2].geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Geometries[2].geometry.triangles.vertexFormat             = GeometryCI[2].vertexFormat;
        Geometries[2].geometry.triangles.vertexStride             = sizeof(Vertices[0]);
        Geometries[2].geometry.triangles.vertexData.deviceAddress = Ctx.vkVertexBufferAddress;
        Geometries[2].geometry.triangles.indexType                = GeometryCI[2].indexType;
        Geometries[2].geometry.triangles.indexData.deviceAddress  = Ctx.vkIndexBufferAddress + PrimitiveOffsets[2] * sizeof(uint) * 3;

        Offsets[0].primitiveCount = GeometryCI[0].maxPrimitiveCount;
        Offsets[1].primitiveCount = GeometryCI[1].maxPrimitiveCount;
        Offsets[2].primitiveCount = GeometryCI[2].maxPrimitiveCount;

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.BLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = _countof(Geometries);
        ASBuildInfo.ppGeometries              = &GeometriyPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);

        VkAccelerationStructureInstanceKHR InstanceData[2] = {};

        InstanceData[0].instanceCustomIndex                    = 0;
        InstanceData[0].instanceShaderBindingTableRecordOffset = 0;
        InstanceData[0].mask                                   = 0xFF;
        InstanceData[0].flags                                  = 0;
        InstanceData[0].accelerationStructureReference         = Ctx.BLAS.vkAddress;
        InstanceData[0].transform.matrix[0][0]                 = 1.0f;
        InstanceData[0].transform.matrix[1][1]                 = 1.0f;
        InstanceData[0].transform.matrix[2][2]                 = 1.0f;

        InstanceData[1].instanceCustomIndex                    = 2;
        InstanceData[1].instanceShaderBindingTableRecordOffset = HitGroupCount / 2;
        InstanceData[1].mask                                   = 0xFF;
        InstanceData[1].flags                                  = 0;
        InstanceData[1].accelerationStructureReference         = Ctx.BLAS.vkAddress;
        InstanceData[1].transform.matrix[0][0]                 = 1.0f;
        InstanceData[1].transform.matrix[1][1]                 = 1.0f;
        InstanceData[1].transform.matrix[2][2]                 = 1.0f;
        InstanceData[1].transform.matrix[0][3]                 = 0.1f;
        InstanceData[1].transform.matrix[1][3]                 = 0.5f;
        InstanceData[1].transform.matrix[2][3]                 = 0.0f;

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkInstanceBuffer, 0, sizeof(InstanceData), InstanceData);

        // barrier for BLAS, scratch buffer, instance buffer
        Barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        VkAccelerationStructureBuildOffsetInfoKHR InstOffsets  = {};
        VkAccelerationStructureGeometryKHR        Instances[2] = {};
        static_assert(_countof(InstanceData) == _countof(Instances), "size mismatch");
        static_assert(InstanceCount == _countof(Instances), "size mismatch");

        GeometriyPtr               = Instances;
        OffsetPtr                  = &InstOffsets;
        InstOffsets.primitiveCount = _countof(Instances);

        Instances[0].flags                                 = 0;
        Instances[0].geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        Instances[0].geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        Instances[0].geometry.instances.pNext              = nullptr;
        Instances[0].geometry.instances.arrayOfPointers    = VK_FALSE;
        Instances[0].geometry.instances.data.deviceAddress = Ctx.vkInstanceBufferAddress;

        Instances[1].flags                                 = 0;
        Instances[1].geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        Instances[1].geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        Instances[1].geometry.instances.pNext              = nullptr;
        Instances[1].geometry.instances.arrayOfPointers    = VK_FALSE;
        Instances[1].geometry.instances.data.deviceAddress = Ctx.vkInstanceBufferAddress + sizeof(VkAccelerationStructureInstanceKHR);

        ASBuildInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        ASBuildInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        ASBuildInfo.flags                     = 0;
        ASBuildInfo.update                    = VK_FALSE;
        ASBuildInfo.srcAccelerationStructure  = VK_NULL_HANDLE;
        ASBuildInfo.dstAccelerationStructure  = Ctx.TLAS.vkAS;
        ASBuildInfo.geometryArrayOfPointers   = VK_FALSE;
        ASBuildInfo.geometryCount             = 1;
        ASBuildInfo.ppGeometries              = &GeometriyPtr;
        ASBuildInfo.scratchData.deviceAddress = Ctx.vkScratchBufferAddress;

        vkCmdBuildAccelerationStructureKHR(Ctx.vkCmdBuffer, 1, &ASBuildInfo, &OffsetPtr);
    }

    ClearRenderTarget(Ctx, pTestingSwapChainVk);
    UpdateDescriptorSet(Ctx);

    VkBuffer       vkPerInstanceBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory vkPerInstanceBufferMemory = VK_NULL_HANDLE;
    VkBuffer       vkPrimitiveBuffer         = VK_NULL_HANDLE;
    VkDeviceMemory vkPrimitiveBufferMemory   = VK_NULL_HANDLE;
    {
        pEnv->CreateBuffer(sizeof(PrimitiveOffsets), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkPerInstanceBufferMemory, vkPerInstanceBuffer);
        pEnv->CreateBuffer(sizeof(Primitives), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkPrimitiveBufferMemory, vkPrimitiveBuffer);

        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, vkPerInstanceBuffer, 0, sizeof(PrimitiveOffsets), PrimitiveOffsets);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, vkPrimitiveBuffer, 0, sizeof(Primitives), Primitives);

        VkWriteDescriptorSet   DescriptorWrite = {};
        VkDescriptorBufferInfo BufInfo         = {};

        DescriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        DescriptorWrite.dstSet          = Ctx.vkDescriptorSet;
        DescriptorWrite.dstBinding      = 4;
        DescriptorWrite.dstArrayElement = 0;
        DescriptorWrite.descriptorCount = 1;
        DescriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        DescriptorWrite.pBufferInfo     = &BufInfo;
        BufInfo.buffer                  = Ctx.vkVertexBuffer;
        BufInfo.range                   = VK_WHOLE_SIZE;
        vkUpdateDescriptorSets(Ctx.vkDevice, 1, &DescriptorWrite, 0, nullptr);

        DescriptorWrite.dstBinding = 3;
        BufInfo.buffer             = vkPrimitiveBuffer;
        vkUpdateDescriptorSets(Ctx.vkDevice, 1, &DescriptorWrite, 0, nullptr);

        DescriptorWrite.dstBinding = 2;
        BufInfo.buffer             = vkPerInstanceBuffer;
        vkUpdateDescriptorSets(Ctx.vkDevice, 1, &DescriptorWrite, 0, nullptr);

        DescriptorWrite.dstArrayElement = 1;
        vkUpdateDescriptorSets(Ctx.vkDevice, 1, &DescriptorWrite, 0, nullptr);
    }

    // trace rays
    {
        VkStridedBufferRegionKHR RaygenShaderBindingTable   = {};
        VkStridedBufferRegionKHR MissShaderBindingTable     = {};
        VkStridedBufferRegionKHR HitShaderBindingTable      = {};
        VkStridedBufferRegionKHR CallableShaderBindingTable = {};
        const Uint32             ShaderGroupHandleSize      = Ctx.RayTracingProps.shaderGroupHandleSize;
        const Uint32             ShaderRecordSize           = ShaderGroupHandleSize + TestingConstants::MultiGeometry::ShaderRecordSize;
        const auto&              Weights                    = TestingConstants::MultiGeometry::Weights;

        RaygenShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        RaygenShaderBindingTable.offset = 0;
        RaygenShaderBindingTable.size   = ShaderRecordSize;
        MissShaderBindingTable.stride   = ShaderRecordSize;

        MissShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        MissShaderBindingTable.offset = Align(RaygenShaderBindingTable.offset + RaygenShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        MissShaderBindingTable.size   = ShaderRecordSize;
        MissShaderBindingTable.stride = ShaderRecordSize;

        HitShaderBindingTable.buffer = Ctx.vkSBTBuffer;
        HitShaderBindingTable.offset = Align(MissShaderBindingTable.offset + MissShaderBindingTable.size, Ctx.RayTracingProps.shaderGroupBaseAlignment);
        HitShaderBindingTable.size   = ShaderRecordSize * HitGroupCount;
        HitShaderBindingTable.stride = ShaderRecordSize;

        char ShaderHandle[64] = {};
        ASSERT_GE(sizeof(ShaderHandle), ShaderGroupHandleSize);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, RAYGEN_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, RaygenShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, MISS_GROUP, 1, ShaderGroupHandleSize, ShaderHandle);
        vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, MissShaderBindingTable.offset, ShaderGroupHandleSize, ShaderHandle);

        const auto SetHitGroup = [&](Uint32 Index, Uint32 ShaderIndex, const void* ShaderRecord) {
            VERIFY_EXPR(Index < HitGroupCount);
            VkDeviceSize Offset = HitShaderBindingTable.offset + Index * ShaderRecordSize;
            vkGetRayTracingShaderGroupHandlesKHR(Ctx.vkDevice, Ctx.vkPipeline, ShaderIndex, 1, ShaderGroupHandleSize, ShaderHandle);
            vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, Offset, ShaderGroupHandleSize, ShaderHandle);
            vkCmdUpdateBuffer(Ctx.vkCmdBuffer, Ctx.vkSBTBuffer, Offset + ShaderGroupHandleSize, sizeof(Weights[0]), ShaderRecord);
        };
        // instance 1
        SetHitGroup(0, HIT_GROUP_1, &Weights[2]); // geometry 1
        SetHitGroup(1, HIT_GROUP_1, &Weights[0]); // geometry 2
        SetHitGroup(2, HIT_GROUP_1, &Weights[1]); // geometry 3
        // instance 2
        SetHitGroup(3, HIT_GROUP_2, &Weights[2]); // geometry 1
        SetHitGroup(4, HIT_GROUP_2, &Weights[1]); // geometry 2
        SetHitGroup(5, HIT_GROUP_2, &Weights[0]); // geometry 3

        // barrier for TLAS & SBT
        VkMemoryBarrier Barrier = {};
        Barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(Ctx.vkCmdBuffer,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkPipeline);
        vkCmdBindDescriptorSets(Ctx.vkCmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Ctx.vkLayout, 0, 1, &Ctx.vkDescriptorSet, 0, nullptr);

        vkCmdTraceRaysKHR(Ctx.vkCmdBuffer, &RaygenShaderBindingTable, &MissShaderBindingTable, &HitShaderBindingTable, &CallableShaderBindingTable, SCDesc.Width, SCDesc.Height, 1);

        pTestingSwapChainVk->TransitionRenderTarget(Ctx.vkCmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0);
    }

    auto res = vkEndCommandBuffer(Ctx.vkCmdBuffer);
    VERIFY(res >= 0, "Failed to end command buffer");

    pEnv->SubmitCommandBuffer(Ctx.vkCmdBuffer, true);

    vkDestroyBuffer(Ctx.vkDevice, vkPerInstanceBuffer, nullptr);
    vkDestroyBuffer(Ctx.vkDevice, vkPrimitiveBuffer, nullptr);
    vkFreeMemory(Ctx.vkDevice, vkPerInstanceBufferMemory, nullptr);
    vkFreeMemory(Ctx.vkDevice, vkPrimitiveBufferMemory, nullptr);
}


} // namespace Testing

} // namespace Diligent
