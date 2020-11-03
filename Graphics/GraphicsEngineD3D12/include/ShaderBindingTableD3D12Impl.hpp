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
/// Declaration of Diligent::ShaderBindingTableD3D12Impl class

#include "ShaderBindingTableD3D12.h"
#include "RenderDeviceD3D12.h"
#include "ShaderBindingTableBase.hpp"
#include "D3D12ResourceBase.hpp"
#include "RenderDeviceD3D12Impl.hpp"
#include "PipelineStateD3D12Impl.hpp"

namespace Diligent
{

/// Shader binding table object implementation in Direct3D12 backend.
class ShaderBindingTableD3D12Impl final : public ShaderBindingTableBase<IShaderBindingTableD3D12, PipelineStateD3D12Impl, RenderDeviceD3D12Impl>, public D3D12ResourceBase
{
public:
    using TShaderBindingTableBase = ShaderBindingTableBase<IShaderBindingTableD3D12, PipelineStateD3D12Impl, RenderDeviceD3D12Impl>;

    ShaderBindingTableD3D12Impl(IReferenceCounters*           pRefCounters,
                                class RenderDeviceD3D12Impl*  pDeviceD3D12,
                                const ShaderBindingTableDesc& Desc,
                                bool                          bIsDeviceInternal = false);
    ~ShaderBindingTableD3D12Impl();

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final;

    virtual void DILIGENT_CALL_TYPE ResetHitGroups(Uint32 HitShadersPerInstance) override;

    virtual void DILIGENT_CALL_TYPE BindAll(const BindAllAttribs& Attribs) override;

    virtual void DILIGENT_CALL_TYPE GetD3D12AddressRangeAndStride(IDeviceContextD3D12*                        pContext,
                                                                  RESOURCE_STATE_TRANSITION_MODE              TransitionMode,
                                                                  D3D12_GPU_VIRTUAL_ADDRESS_RANGE&            RaygenShaderBindingTable,
                                                                  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE& MissShaderBindingTable,
                                                                  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE& HitShaderBindingTable,
                                                                  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE& CallableShaderBindingTable) override;

private:
    RefCntAutoPtr<IBuffer> m_pBuffer;
};

} // namespace Diligent
