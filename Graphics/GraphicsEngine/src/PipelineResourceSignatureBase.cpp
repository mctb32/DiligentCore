/*
 *  Copyright 2019-2021 Diligent Graphics LLC
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

#include "PipelineResourceSignatureBase.hpp"

namespace Diligent
{

#define LOG_PRS_ERROR_AND_THROW(...) LOG_ERROR_AND_THROW("Description of a pipeline resource signature '", (Desc.Name ? Desc.Name : ""), "' is invalid: ", ##__VA_ARGS__)

void ValidatePipelineResourceSignatureDesc(const PipelineResourceSignatureDesc& Desc) noexcept(false)
{
    if (Desc.BindingIndex >= MAX_RESOURCE_SIGNATURES)
        LOG_PRS_ERROR_AND_THROW("Desc.BindingIndex (", Desc.BindingIndex, ") exceeds the maximum allowed value (", MAX_RESOURCE_SIGNATURES - 1, ").");

    for (Uint32 i = 0; i < Desc.NumResources; ++i)
    {
        const auto& Res = Desc.Resources[i];

        if (Res.Name == nullptr)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Name must not be null");

        if (Res.ShaderStages == SHADER_TYPE_UNKNOWN)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].ShaderStages must not be SHADER_TYPE_UNKNOWN");

        if (Res.ArraySize == 0)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].ArraySize must not be 0");

#ifdef DILIGENT_DEBUG
        if ((Res.Flags & PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS) &&
            (Res.ResourceType != SHADER_RESOURCE_TYPE_CONSTANT_BUFFER &&
             Res.ResourceType != SHADER_RESOURCE_TYPE_BUFFER_UAV &&
             Res.ResourceType != SHADER_RESOURCE_TYPE_TEXTURE_SRV))
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Flags must not contains PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS if ResourceType is not buffer");

        if ((Res.Flags & PIPELINE_RESOURCE_FLAG_COMBINED_IMAGE) &&
            Res.ResourceType != SHADER_RESOURCE_TYPE_TEXTURE_SRV)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Flags must not contains PIPELINE_RESOURCE_FLAG_COMBINED_IMAGE if ResourceType is not SHADER_RESOURCE_TYPE_TEXTURE_SRV");

        if ((Res.Flags & PIPELINE_RESOURCE_FLAG_FORMATTED_BUFFER) &&
            (Res.ResourceType != SHADER_RESOURCE_TYPE_BUFFER_UAV &&
             Res.ResourceType != SHADER_RESOURCE_TYPE_BUFFER_SRV))
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Flags must not contains PIPELINE_RESOURCE_FLAG_FORMATTED_BUFFER if ResourceType is not buffer");
#endif
    }

    for (Uint32 i = 0; i < Desc.NumImmutableSamplers; ++i)
    {
        if (Desc.ImmutableSamplers[i].SamplerOrTextureName == nullptr)
            LOG_PRS_ERROR_AND_THROW("Desc.ImmutableSamplers[", i, "].SamplerOrTextureName must not be null");
    }
}

#undef LOG_PRS_ERROR_AND_THROW


} // namespace Diligent
