#include <string>

#include "Renderer/Device/Direct12/DeviceResource.h"
#include "Renderer/Device/Direct12/D3D12Device.h"

namespace MRenderer
{
    union TextureFormatKey
    {
        struct
        {
            uint16 Width;
            uint16 Height;
            uint16 MipLevels;
            ETextureFormat Format;
            ETexture2DFlag Flag;
        } Info;
        uint64 Key;

        TextureFormatKey()
            :Key(0)
        {
        }

        TextureFormatKey(uint16 width, uint16 height, uint16 mip_levels, ETextureFormat format, ETexture2DFlag flag)
            :Info{ width, height, mip_levels, format, flag }
        {
        }

        TextureFormatKey(const TextureFormatKey& other)
            :Key(other.Key)
        {
        }

        TextureFormatKey& operator= (const TextureFormatKey& other)
        {
            Key = other.Key;
            return *this;
        }

        inline bool operator==(const TextureFormatKey& other) const { return Key == other.Key; }

        inline bool Empty() const { return Key == 0; }
    };

    static_assert(sizeof(TextureFormatKey) == 8);
}

template <>
struct std::hash<MRenderer::TextureFormatKey>
{
    std::size_t operator()(const MRenderer::TextureFormatKey& k) const
    {
        return k.Key;
    }
};

namespace MRenderer
{
    class Scene;
    class Camera;
    class D3D12CommandList;
    class FrameGraph;
    class D3D12ResourceAllocator;

    using FGResourceId = int32;
    constexpr FGResourceId InvalidFGResourceId = -1;

    class FGResourceIDs
    {
    protected:
        FGResourceIDs() = default;

    public:
        inline static FGResourceIDs* Instance()
        {
            static FGResourceIDs instance;
            return &instance;
        }

        FGResourceId NameToID(const std::string& name)
        {
            auto [it, inserted] = mResourceIdTable.try_emplace(std::string(name), mNextId);
            if (inserted) {
                mNameTable.emplace_back(name);
                mNextId++;
            }
            return it->second;
        }

        std::string_view IdToName(FGResourceId id) 
        {
            return mNameTable[id];
        }

        inline uint32 NumResources() { return static_cast<uint32>(mNameTable.size()); }

    protected:
        uint32 mNextId = 0;
        std::vector<std::string> mNameTable;
        std::unordered_map<std::string, FGResourceId> mResourceIdTable;
    };

    // for transient resource, actual resources wiil be allocated by frame graph
    using FGTransientTextureDescription = TextureFormatKey;
    
    struct FGTransientBufferDescription 
    {
        uint32 Size;
        uint32 Stride;

        bool operator==(const FGTransientBufferDescription& other) const = default;
        inline bool Empty() const { return Size == 0 && Stride == 0; }
    };

    // for persistent buffer, actual resources are maintained by others
    struct FGPersistentResourceDescription 
    {
        IDeviceResource* Resource;

        bool operator==(const FGPersistentResourceDescription& other) const = default;
        inline bool Empty() const { return Resource == nullptr; }
    };

    class FGResourceDescriptionTable
    {
    public:
        using FGResourceDescription = std::variant<FGTransientTextureDescription, FGTransientBufferDescription, FGPersistentResourceDescription>;
    
    protected:
        FGResourceDescriptionTable() = default;

    public:
        inline static FGResourceDescriptionTable* Instance()
        {
            static FGResourceDescriptionTable instance;
            return &instance;
        }

        inline void DeclareTransientTexture(FGResourceId id, uint16 width, uint16 height, uint16 mip_levels, ETextureFormat format, ETexture2DFlag flag)
        {
            DeclareFGResource(id, FGTransientTextureDescription( width, height, mip_levels, format, flag ));
        }

        inline void DeclareTransientBuffer(FGResourceId id, uint32 size, uint32 stride)
        {
            DeclareFGResource(id, FGTransientBufferDescription{ size, stride });
        }

        inline void DeclarePersistentResource(FGResourceId id, IDeviceResource* res)
        {
            DeclareFGResource(id, FGPersistentResourceDescription{ res });
        }

        template<typename T>
        inline auto VisitResourceDescription(FGResourceId id, T overload) const
        {
            return std::visit(overload, mResourceDescriptions[id]);
        }

        inline const FGTransientTextureDescription& GetTransientTexture(FGResourceId id) const
        {
            const FGTransientTextureDescription& desc = std::get<FGTransientTextureDescription>(mResourceDescriptions[id]);
            ASSERT(!desc.Empty());

            return desc;
        }

        inline const FGTransientBufferDescription& GetTransientBuffer(FGResourceId id) const
        {
            const FGTransientBufferDescription& desc = std::get<FGTransientBufferDescription>(mResourceDescriptions[id]);
            ASSERT(!desc.Empty());

            return desc;
        }

        inline const FGPersistentResourceDescription& GetPersistentResource(FGResourceId id) const
        {
            const FGPersistentResourceDescription& desc = std::get<FGPersistentResourceDescription>(mResourceDescriptions[id]);
            ASSERT(!desc.Empty());

            return desc;
        }

    protected:
        inline static bool IsEmptyDescription(const FGResourceDescription& desc) 
        {
            return std::holds_alternative<FGTransientTextureDescription>(desc) && std::get<FGTransientTextureDescription>(desc).Key == 0;
        }

        template<typename T>
            requires variant_contain_v<T, FGResourceDescription>
        void CheckDescription(FGResourceId id, const T& desc) 
        {
            ASSERT(IsEmptyDescription(mResourceDescriptions[id]) || (std::get<T>(mResourceDescriptions[id]) == desc));
        }

        template<typename T>
            requires variant_contain_v<T, FGResourceDescription>
        inline void DeclareFGResource(FGResourceId id, const T& desc)
        {
            if (id >= mResourceDescriptions.size())
            {
                mResourceDescriptions.resize(FGResourceIDs::Instance()->NumResources(), FGResourceDescription{});
            }

            CheckDescription(id, desc);
            mResourceDescriptions[id] = desc;
        }

    protected:
        std::vector<FGResourceDescription> mResourceDescriptions;
    };

    class FGResourceAllocator 
    {
    protected:
        using TransientResourcesTable = std::vector<std::shared_ptr<IDeviceResource>>;

    public:
        FGResourceAllocator() 
            :mFGResourceAllocator(GD3D12RawDevice, std::make_unique<D3D12Memory::D3D12TransientMemoryAllocator>(GD3D12RawDevice))
        {
        }

        void AllocateTransientResource(FGResourceId id) 
        {
            ASSERT(!mTransientResources[id]);

            Overload overloads =
            {
                [&](const FGTransientTextureDescription& desc)
                {
                    mTransientResources[id] = mFGResourceAllocator.CreateTexture2D(desc.Info.Width, desc.Info.Height, desc.Info.MipLevels, desc.Info.Format, desc.Info.Flag);
                },
                [&](const FGTransientBufferDescription& desc)
                {
                    mTransientResources[id] = mFGResourceAllocator.CreateStructuredBuffer(desc.Size, desc.Stride);
                },
                [](const FGPersistentResourceDescription& desc)
                {
                    return;
                },
            };

            FGResourceDescriptionTable::Instance()->VisitResourceDescription(id, overloads);
        }

        void ReleaseTransientResource(FGResourceId id) 
        {
            mTransientResources[id]->Resource()->ReleasePlacedMemory();
        }

        void Reset() 
        {
            mFGResourceAllocator.ResetPlacedMemory();
            mTransientResources = TransientResourcesTable(FGResourceIDs::Instance()->NumResources());
        }

        inline IDeviceResource* GetResource(FGResourceId id)
        {
            IDeviceResource* res = mTransientResources[id].get();
            ASSERT(res);

            return res;
        }

        D3D12ResourceAllocator mFGResourceAllocator;
        std::vector<std::shared_ptr<IDeviceResource>> mTransientResources;
    };

    struct FGContext
    {
        D3D12CommandList* CommandList;
        Scene* Scene;
        Camera* Camera;
        FrameGraph* FrameGraph;
    };
}