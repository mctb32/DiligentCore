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

#pragma once

/// \file
/// Implementation of the Diligent::ShaderBindingTableBase template class

#include <unordered_map>

#include "ShaderBindingTable.h"
#include "DeviceObjectBase.hpp"
#include "RenderDeviceBase.hpp"
#include "StringPool.hpp"
#include "HashUtils.hpp"

namespace Diligent
{

/// Template class implementing base functionality for a shader binding table object.

/// \tparam BaseInterface - base interface that this class will inheret
///                          (Diligent::IShaderBindingTableD3D12 or Diligent::IShaderBindingTableVk).
/// \tparam RenderDeviceImplType - type of the render device implementation
///                                (Diligent::RenderDeviceD3D12Impl or Diligent::RenderDeviceVkImpl)
template <class BaseInterface, class PipelineStateImplType, class RenderDeviceImplType>
class ShaderBindingTableBase : public DeviceObjectBase<BaseInterface, RenderDeviceImplType, ShaderBindingTableDesc>
{
public:
    using TDeviceObjectBase = DeviceObjectBase<BaseInterface, RenderDeviceImplType, ShaderBindingTableDesc>;

    /// \param pRefCounters      - reference counters object that controls the lifetime of this SBT.
    /// \param pDevice           - pointer to the device.
    /// \param Desc              - SBT description.
    /// \param bIsDeviceInternal - flag indicating if the BLAS is an internal device object and
    ///							   must not keep a strong reference to the device.
    ShaderBindingTableBase(IReferenceCounters*           pRefCounters,
                           RenderDeviceImplType*         pDevice,
                           const ShaderBindingTableDesc& Desc,
                           bool                          bIsDeviceInternal = false) :
        TDeviceObjectBase{pRefCounters, pDevice, Desc, bIsDeviceInternal}
    {
        ValidateShaderBindingTableDesc(Desc);

        this->m_pPSO               = ValidatedCast<PipelineStateImplType>(this->m_Desc.pPSO);
        this->m_ShaderRecordSize   = this->m_pPSO->GetRayTracingPipelineDesc().ShaderRecordSize;
        this->m_ShaderRecordStride = this->m_ShaderRecordSize + this->m_pDevice->GetShaderGroupHandleSize();
    }

    ~ShaderBindingTableBase()
    {
    }

    void DILIGENT_CALL_TYPE Reset(const ShaderBindingTableDesc& Desc) override final
    {
        this->m_RayGenShaderRecord.clear();
        this->m_MissShadersRecord.clear();
        this->m_CallableShadersRecord.clear();
        this->m_HitGroupsRecord.clear();
        this->m_Changed = true;
        this->m_pPSO    = nullptr;
        this->m_Desc    = {};

        try
        {
            ValidateShaderBindingTableDesc(Desc);
        }
        catch (const std::runtime_error&)
        {
            return;
        }

        this->m_Desc               = Desc;
        this->m_pPSO               = ValidatedCast<PipelineStateImplType>(this->m_Desc.pPSO);
        this->m_ShaderRecordSize   = this->m_pPSO->GetRayTracingPipelineDesc().ShaderRecordSize;
        this->m_ShaderRecordStride = this->m_ShaderRecordSize + this->m_pDevice->GetShaderGroupHandleSize();
    }

    void DILIGENT_CALL_TYPE BindRayGenShader(const char* ShaderGroupName, const void* Data, Uint32 DataSize) override final
    {
        VERIFY_EXPR((Data == nullptr) == (DataSize == 0));
        VERIFY_EXPR(Data == nullptr || (DataSize == this->m_ShaderRecordSize));

        this->m_RayGenShaderRecord.resize(this->m_ShaderRecordStride, EmptyElem);
        this->m_pPSO->CopyShaderHandle(ShaderGroupName, this->m_RayGenShaderRecord.data(), this->m_ShaderRecordStride);

        const Uint32 GroupSize = this->m_pDevice->GetShaderGroupHandleSize();
        std::memcpy(this->m_RayGenShaderRecord.data() + GroupSize, Data, DataSize);
        this->m_Changed = true;
    }

    void DILIGENT_CALL_TYPE BindMissShader(const char* ShaderGroupName, Uint32 MissIndex, const void* Data, Uint32 DataSize) override final
    {
        VERIFY_EXPR((Data == nullptr) == (DataSize == 0));
        VERIFY_EXPR(Data == nullptr || (DataSize == this->m_ShaderRecordSize));

        const Uint32 GroupSize = this->m_pDevice->GetShaderGroupHandleSize();
        const Uint32 Offset    = MissIndex * this->m_ShaderRecordStride;
        this->m_MissShadersRecord.resize(std::max<size_t>(this->m_MissShadersRecord.size(), Offset + this->m_ShaderRecordStride), EmptyElem);

        this->m_pPSO->CopyShaderHandle(ShaderGroupName, this->m_MissShadersRecord.data() + Offset, this->m_ShaderRecordStride);
        std::memcpy(this->m_MissShadersRecord.data() + Offset + GroupSize, Data, DataSize);
        this->m_Changed = true;
    }

    void DILIGENT_CALL_TYPE BindHitGroup(ITopLevelAS* pTLAS,
                                         const char*  InstanceName,
                                         const char*  GeometryName,
                                         Uint32       RayOffsetInHitGroupIndex,
                                         const char*  ShaderGroupName,
                                         const void*  Data,
                                         Uint32       DataSize) override final
    {
        VERIFY_EXPR((Data == nullptr) == (DataSize == 0));
        VERIFY_EXPR(Data == nullptr || (DataSize == this->m_ShaderRecordSize));
        VERIFY_EXPR(pTLAS != nullptr);
        VERIFY_EXPR(RayOffsetInHitGroupIndex < this->m_Desc.HitShadersPerInstance);
        VERIFY_EXPR(pTLAS->GetDesc().BindingMode == SHADER_BINDING_MODE_PER_GEOMETRY);

        const auto Desc = pTLAS->GetInstanceDesc(InstanceName);
        VERIFY_EXPR(Desc.pBLAS != nullptr);

        const Uint32 InstanceIndex = Desc.ContributionToHitGroupIndex;
        const Uint32 GeometryIndex = Desc.pBLAS->GetGeometryIndex(GeometryName);
        const Uint32 Index         = InstanceIndex + GeometryIndex * this->m_Desc.HitShadersPerInstance + RayOffsetInHitGroupIndex;
        const Uint32 Offset        = Index * this->m_ShaderRecordStride;
        const Uint32 GroupSize     = this->m_pDevice->GetShaderGroupHandleSize();

        this->m_HitGroupsRecord.resize(std::max<size_t>(this->m_HitGroupsRecord.size(), Offset + this->m_ShaderRecordStride), EmptyElem);

        this->m_pPSO->CopyShaderHandle(ShaderGroupName, this->m_HitGroupsRecord.data() + Offset, this->m_ShaderRecordStride);
        std::memcpy(this->m_HitGroupsRecord.data() + Offset + GroupSize, Data, DataSize);
        this->m_Changed = true;
    }

    void DILIGENT_CALL_TYPE BindHitGroups(ITopLevelAS* pTLAS,
                                          const char*  InstanceName,
                                          Uint32       RayOffsetInHitGroupIndex,
                                          const char*  ShaderGroupName,
                                          const void*  Data,
                                          Uint32       DataSize) override final
    {
        VERIFY_EXPR((Data == nullptr) == (DataSize == 0));
        VERIFY_EXPR(pTLAS != nullptr);
        VERIFY_EXPR(RayOffsetInHitGroupIndex < this->m_Desc.HitShadersPerInstance);
        VERIFY_EXPR(pTLAS->GetDesc().BindingMode == SHADER_BINDING_MODE_PER_GEOMETRY ||
                    pTLAS->GetDesc().BindingMode == SHADER_BINDING_MODE_PER_INSTANCE);

        const auto Desc = pTLAS->GetInstanceDesc(InstanceName);
        VERIFY_EXPR(Desc.pBLAS != nullptr);

        const Uint32 InstanceIndex = Desc.ContributionToHitGroupIndex;
        const auto&  GeometryDesc  = Desc.pBLAS->GetDesc();
        Uint32       GeometryCount = 0;

        switch (pTLAS->GetDesc().BindingMode)
        {
            // clang-format off
            case SHADER_BINDING_MODE_PER_GEOMETRY: GeometryCount = GeometryDesc.BoxCount + GeometryDesc.TriangleCount; break;
            case SHADER_BINDING_MODE_PER_INSTANCE: GeometryCount = 1;                                                  break;
            default:                               UNEXPECTED("unknown binding mode");
                // clang-format on
        }

        VERIFY_EXPR(Data == nullptr || (DataSize == this->m_ShaderRecordSize * GeometryCount));

        const Uint32 BeginIndex = InstanceIndex + 0 * this->m_Desc.HitShadersPerInstance + RayOffsetInHitGroupIndex;
        const Uint32 EndIndex   = InstanceIndex + GeometryCount * this->m_Desc.HitShadersPerInstance + RayOffsetInHitGroupIndex;
        const Uint32 GroupSize  = this->m_pDevice->GetShaderGroupHandleSize();
        const auto*  DataPtr    = static_cast<const Uint8*>(Data);

        this->m_HitGroupsRecord.resize(std::max<size_t>(this->m_HitGroupsRecord.size(), EndIndex * this->m_ShaderRecordStride), EmptyElem);

        for (Uint32 i = 0; i < GeometryCount; ++i)
        {
            Uint32 Offset = (BeginIndex + i) * this->m_ShaderRecordStride;
            this->m_pPSO->CopyShaderHandle(ShaderGroupName, this->m_HitGroupsRecord.data() + Offset, this->m_ShaderRecordStride);

            std::memcpy(this->m_HitGroupsRecord.data() + Offset + GroupSize, DataPtr, this->m_ShaderRecordSize);
            DataPtr += this->m_ShaderRecordSize;
        }
        this->m_Changed = true;
    }

    void DILIGENT_CALL_TYPE BindCallableShader(const char* ShaderGroupName,
                                               Uint32      CallableIndex,
                                               const void* Data,
                                               Uint32      DataSize) override final
    {
        VERIFY_EXPR((Data == nullptr) == (DataSize == 0));
        VERIFY_EXPR(Data == nullptr || (DataSize == this->m_ShaderRecordSize));

        const Uint32 GroupSize = this->m_pDevice->GetShaderGroupHandleSize();
        const Uint32 Offset    = CallableIndex * this->m_ShaderRecordStride;
        this->m_CallableShadersRecord.resize(std::max<size_t>(this->m_CallableShadersRecord.size(), Offset + this->m_ShaderRecordStride), EmptyElem);

        this->m_pPSO->CopyShaderHandle(ShaderGroupName, this->m_CallableShadersRecord.data() + Offset, this->m_ShaderRecordStride);
        std::memcpy(this->m_CallableShadersRecord.data() + Offset + GroupSize, Data, DataSize);
        this->m_Changed = true;
    }

    Bool DILIGENT_CALL_TYPE Verify() const override final
    {
        // AZ TODO
        return true;
    }

protected:
    void ValidateShaderBindingTableDesc(const ShaderBindingTableDesc& Desc) const
    {
#define LOG_SBT_ERROR_AND_THROW(...) LOG_ERROR_AND_THROW("Description of Shader binding table '", (Desc.Name ? Desc.Name : ""), "' is invalid: ", ##__VA_ARGS__)

        if (Desc.pPSO == nullptr)
        {
            LOG_SBT_ERROR_AND_THROW("pPSO must not be null");
        }

        if (Desc.pPSO->GetDesc().PipelineType != PIPELINE_TYPE_RAY_TRACING)
        {
            LOG_SBT_ERROR_AND_THROW("pPSO must be ray tracing pipeline");
        }

        const auto ShaderGroupHandleSize = this->m_pDevice->GetShaderGroupHandleSize();
        const auto MaxShaderRecordStride = this->m_pDevice->GetMaxShaderRecordStride();
        const auto ShaderRecordSize      = Desc.pPSO->GetRayTracingPipelineDesc().ShaderRecordSize;
        const auto ShaderRecordStride    = ShaderRecordSize + ShaderGroupHandleSize;

        if (ShaderRecordStride > MaxShaderRecordStride)
        {
            LOG_SBT_ERROR_AND_THROW("ShaderRecordSize(", ShaderRecordSize, ") is too big, max size is: ", MaxShaderRecordStride - ShaderGroupHandleSize);
        }

        if (ShaderRecordStride % ShaderGroupHandleSize != 0)
        {
            LOG_SBT_ERROR_AND_THROW("ShaderRecordSize(", ShaderRecordSize, ") plus ShaderGroupHandleSize(", ShaderGroupHandleSize, ") must be multiple of ", ShaderGroupHandleSize);
        }
#undef LOG_SBT_ERROR_AND_THROW
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ShaderBindingTable, TDeviceObjectBase)

protected:
    std::vector<Uint8> m_RayGenShaderRecord;
    std::vector<Uint8> m_MissShadersRecord;
    std::vector<Uint8> m_CallableShadersRecord;
    std::vector<Uint8> m_HitGroupsRecord;

    RefCntAutoPtr<PipelineStateImplType> m_pPSO;

    Uint32 m_ShaderRecordSize   = 0;
    Uint32 m_ShaderRecordStride = 0;
    bool   m_Changed            = true;

    static const Uint8 EmptyElem = 0xA7;
};

} // namespace Diligent
