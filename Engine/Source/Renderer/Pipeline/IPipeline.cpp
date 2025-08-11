#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Pipeline/IPipeline.h"
#include "Resource/ResourceLoader.h"
#include "Renderer/Device/Direct12/D3D12CommandList.h"


namespace MRenderer
{
    ShadingState::ShadingState()
        :mShaderProgram(nullptr), mShaderConstantBuffer(nullptr)
    {
    }

    ShadingState::ShadingState(ShadingState&& other)
        :ShadingState()
    {
        Swap(*this, other);
    }

    ShadingState& ShadingState::operator=(ShadingState other)
    {
        Swap(*this, other);
        return *this;
    }

    void ShadingState::SetShader(std::string_view filename, bool is_compute)
    {
        if (mShaderProgram && filename == mShaderProgram->mFilePath)
        {
            return;
        }

        // compile shader
        mShaderProgram = ShaderLibrary::Instance().ComplieShader(filename, is_compute);
        mIsCompute = is_compute;

        // update shader constant buffer size
        const ShaderConstantBufferAttribute* attr = mShaderProgram->GetPrimaryShader()->FindConstantBufferAttribute(ConstantBufferShader::SemanticName);
        uint32 size = attr ? attr->mSize : 0; // 0 means create samllest buffer, which is 256 bytes in dx12
        
        if (!mShaderConstantBuffer || mShaderConstantBuffer->BufferSize() != size)
        {
            mShaderConstantBuffer = GD3D12Device->CreateConstBuffer(size);
        }
    }

    bool ShadingState::SetTexture(std::string_view semantic_name, DeviceTexture* texture)
    {
        ASSERT(mShaderProgram);
        ASSERT(texture);

        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_Texture, semantic_name);

        if (attr) 
        {
            mResourceBinding.SRVs[attr->mBindPoint] = texture->GetShaderResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice)
    {
        ASSERT(mShaderProgram);
        ASSERT(texture);

        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_Texture, semantic_name);

        if (attr)
        {
            mResourceBinding.SRVs[attr->mBindPoint] = texture->GetMipSliceSRV(mip_slice);
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (attr) 
        {
            ASSERT(attr->mBindCount == 1);

            mResourceBinding.UAVs[attr->mBindPoint] = texture->GetUnorderedResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetRWTexture(std::string_view semantic_name, DeviceTexture2D* texture, uint32 mip_slice)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name);

        if (attr)
        {
            ASSERT(attr->mBindCount == 1);

            mResourceBinding.UAVs[attr->mBindPoint] = texture->GetMipSliceUAV(mip_slice);
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetRWTextureArray(std::string_view semantic_name, DeviceTexture2DArray* texture_array)
    {
        ASSERT(mShaderProgram && mIsCompute);
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWTexture, semantic_name); //RWTextureArray is the RWTexture, just the bind count are different
        
        if (attr) 
        {
            //e.g shader code: "RWTexture2DArray PrefilterEnvMap[5]" require array has at least 5 mip levels
            ASSERT(attr->mBindCount <= texture_array->MipLevels());

            // assign each mip slice to the corresponding texture array
            for (uint32 mip_level = 0; mip_level < texture_array->MipLevels(); mip_level++)
            {
                mResourceBinding.UAVs[attr->mBindPoint + mip_level] = texture_array->GetMipSliceUAV(mip_level);
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    bool ShadingState::SetStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_StructuredBuffer, semantic_name);

        if (attr) 
        {
            mResourceBinding.SRVs[attr->mBindPoint] = buffer->GetShaderResourceView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    bool ShadingState::SetRWStructuredBuffer(std::string_view semantic_name, DeviceStructuredBuffer* buffer)
    {
        const ShaderAttribute* attr = FindShaderAttribute(EShaderAttrType_RWStructuredBuffer, semantic_name);

        if (attr) 
        {
            mResourceBinding.UAVs[attr->mBindPoint] = buffer->GetUnorderedAccessView();
            return true;
        }
        else 
        {
            return false;
        }
    }

    void ShadingState::ClearResourceBinding()
    {
        mResourceBinding = {};
    }

    const ResourceBinding* ShadingState::GetResourceBinding() const
    {
        return &mResourceBinding;
    }

    DeviceConstantBuffer* ShadingState::GetConstantBuffer()
    {
        return mShaderConstantBuffer.get();
    }

    const ShaderAttribute* ShadingState::FindShaderAttribute(EShaderAttrType type, std::string_view semantic_name)
    {
        const ShaderAttribute* attr = mShaderProgram->GetPrimaryShader()->FindAttribute(type, semantic_name);

        if (!attr)
        {
            Log("Try to assign undefined or unused shader attribute:", semantic_name, "to shader:", mShaderProgram->GetFilePath());
            return nullptr;
        }

        return attr;
    }

    D3D12ShaderProgram* ShadingState::GetShader()
    {
        return mShaderProgram;
    }

    RenderPassNode* IRenderPass::SampleTexture(RenderPassNode* node)
    {
        ASSERT(node);
        mInputNodes.push_back(node);
        return node;
    }

    RenderPassNode* IRenderPass::SampleTexture(IRenderPass* pass, std::string_view output_name)
    {
        ASSERT(pass);
        RenderPassNode* node = pass->FindOutput(output_name);
        ASSERT(node);
        mInputNodes.push_back(node);
        return node;
    }

    void IRenderPass::WriteRenderTarget(std::string_view name, TextureFormatKey key)
    {
        ASSERT(mNumRenderTargets != MaxRenderTargets);
        mOutputNodes.push_back(RenderPassNode::TransientPassResource(name, this, key));
        mRenderTargets[mNumRenderTargets++] = mOutputNodes.back().get();
    }

    void IRenderPass::WriteRenderTarget(std::string_view name, RenderPassNode* node)
    {
        ASSERT(mNumRenderTargets != MaxRenderTargets);
        mOutputNodes.push_back(RenderPassNode::TransientPassResourceReference(name, this, node));
        mRenderTargets[mNumRenderTargets++] = mOutputNodes.back().get();
    }

    void IRenderPass::WriteDepthStencil(uint32 width, uint32 height)
    {
        ASSERT(!mDepthStencil && "caon only write to one depth-stencil at a time");
        mOutputNodes.push_back(RenderPassNode::TransientPassResource("DepthStencil", this, TextureFormatKey(width, height, ETextureFormat_DepthStencil)));
        mDepthStencil = mOutputNodes.back().get();
    }

    void IRenderPass::WriteDepthStencil(RenderPassNode* node)
    {
        ASSERT(!mDepthStencil && "caon only write to one depth-stencil at a time");
        mOutputNodes.push_back(RenderPassNode::TransientPassResourceReference("DepthStencil", this, node));
        mDepthStencil = mOutputNodes.back().get();
    }

    void IRenderPass::WritePersistent(std::string_view name, DeviceTexture* persisten_resource)
    {
        mOutputNodes.push_back(RenderPassNode::PersistentPassResource(name, this, persisten_resource));
    }

    RenderPassNode* IRenderPass::FindOutput(std::string_view name)
    {
        auto ret = std::find_if(mOutputNodes.begin(), mOutputNodes.end(), [&](std::unique_ptr<RenderPassNode>& node) { return node->Name == name; });
        ASSERT(ret != mOutputNodes.end());

        return (ret != mOutputNodes.end()) ? ret->get() : nullptr;
    }

    RenderPassNode* IRenderPass::FindInput(std::string_view name)
    {
        auto ret = std::find_if(mInputNodes.begin(), mInputNodes.end(), [&](RenderPassNode* node) { return node->Name == name; });
        ASSERT(ret != mInputNodes.end());

        return (ret != mInputNodes.end()) ? *ret : nullptr;
    }

    void IRenderPass::UpdatePassStateDesc()
    {
        mPassState.DepthStencilFormat = static_cast<ETextureFormat>(mDepthStencil ? mDepthStencil->GetTextureFormatKey().Info.Format : ETextureFormat_None);
        mPassState.NumRenderTarget = GetRenderTargetsSize();

        for (uint32 i = 0; i < mPassState.NumRenderTarget; i++)
        {
            mPassState.RenderTargetFormats[i] = static_cast<ETextureFormat>(mRenderTargets[i]->GetTextureFormatKey().Info.Format);
        }
    }

    const RenderPassStateDesc* IRenderPass::GetPassStateDesc() const
    {
        return &mPassState;
    }
    
    TextureFormatKey RenderPassNode::GetTextureFormatKey()
    {
        if (Type == ERenderPassNodeType_Transient)
        {
            return TransientResource.RenderTargetKey;
        }
        else if (Type == ERenderPassNodeType_Persisten)
        {
            DeviceTexture* texture = dynamic_cast<DeviceTexture*>(PersistentResource);
            ASSERT(texture);

            return TextureFormatKey
            {
                static_cast<uint16>(texture->Width()),
                static_cast<uint16>(texture->Height()),
                texture->Resource()->Format(),
            };
        }
        else
        {
            return PassNodeReference->GetTextureFormatKey();
        }
    }

    IDeviceResource* RenderPassNode::GetResource()
    {
        if (Type == ERenderPassNodeType_Transient)
        {
            ASSERT(TransientResource.Resource);
            return TransientResource.Resource;
        }
        else if (Type == ERenderPassNodeType_Persisten)
        {
            return PersistentResource;
        }
        else if (Type == ERenderPassNodeType_Reference)
        {
            return PassNodeReference->GetResource();
        }
        else 
        {
            ASSERT(false);
            return nullptr;
        }
    }

    // find the RenderPassNode that holds the actual resource
    RenderPassNode* RenderPassNode::GetActualPassNode()
    {
        if (Type == ERenderPassNodeType_Reference) 
        {
            return PassNodeReference->GetActualPassNode();
        }
        else 
        {
            return this;
        }
    }

    void ShaderParameter::JsonSerialize(nlohmann::json& json, const ShaderParameter& t)
    {
        Overload overloads{
            [&](bool val) {json = val; },
            [&](float val) {json = val; },
            [&](Vector2 vec) {json = std::array<float, 2>{vec.x, vec.y}; },
            [&](Vector3 vec) {json = std::array<float, 3>{vec.x, vec.y, vec.z}; },
            [&](Vector4 vec) {json = std::array<float, 4>{vec.x, vec.y, vec.z, vec.w}; },
        };

        std::visit(overloads, t.mData);
    }

    void ShaderParameter::JsonDeserialize(nlohmann::json& json, ShaderParameter& t)
    {
        Overload overloads
        {
            [&](bool val) { t.mData = val; },
            [&](float val) { t.mData = val; },
            [&](const std::array<float, 2>& vec) { t.mData = Vector2(vec[0], vec[1]); },
            [&](const std::array<float, 3>& vec) { t.mData = Vector3(vec[0], vec[1], vec[2]); },
            [&](const std::array<float, 4>& vec) { t.mData = Vector4(vec[0], vec[1], vec[2], vec[3]); },
        };

        if (json.is_number())
        {
            overloads(json.get<float>());
        }
        else if (json.is_boolean())
        {
            overloads(json.get<bool>());
        }
        else if (json.is_array())
        {
            if (json.size() == 2)
                overloads(json.get<std::array<float, 2>>());
            else if (json.size() == 3)
                overloads(json.get<std::array<float, 3>>());
            else if (json.size() == 4)
                overloads(json.get<std::array<float, 4>>());
        }
    }
}