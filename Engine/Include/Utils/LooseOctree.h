#include "Utils/MathLib.h"
#include "Utils/Allocator.h"

namespace MRenderer 
{
    template<typename T>
    class LooseOctree 
    {
    public: struct OctreeElement;
    
    protected:
        using ElementTable = NestedObjectAllocator<OctreeElement>;

        // the number of children in an octree node
        static constexpr uint32 NumOctreeLeaf = 8;

        // the max depth of the octree, node will stop subdividing when it reaches this depth
        static constexpr uint32 MaxDepth = 8;

        // nodes will start subdividing when the number of objects in it exceeds this value
        static constexpr uint32 MaxCapacityToSplit = 2;

        // the actual size of the node is lager than the ordinary octree node, which is LooseBound * OrdinarySize
        static constexpr float LooseBound = 1.5f;

        // the actual size of the deepest node in the octree
        // 1. r(0) = 1.0f
        // 2. r(n) = r(n - 1) * 0.5f * LooseBound
        // so we know r(n) = (0.5f * LooseBound)^n
        static constexpr float MinNodeSize = Pow(0.5f * LooseBound, MaxDepth);

        static_assert(LooseBound >= 1.0f && LooseBound <= 1.5f);

    public:
        // Index for referencing an element in the octree
        struct OctreeElement
        {
            friend class LooseOctree;
        public:

            template<typename... Args>
            OctreeElement(int node_index, const AABB& bound, Args&&... args)
                : NodeIndex(node_index), Bound(bound), Object(args...) {
            }

            T& operator->() { return Object; }

        protected:
            int NodeIndex;
            AABB Bound;
            T Object;
        };

    protected:
        struct OctreeNode
        {
        public:
            OctreeNode(const AABB& bounding_box, int child_index, int element_index)
                : Bound(bounding_box), Children(child_index), ElementsIndex(element_index)
            {
            }

            inline bool IsLeaf() const { return Children == -1; }

        public:
            // the bounding box of this node
            // Note: It can be optimized away by deducing it from the recursion depth, but it's a trade-off between time and space
            AABB Bound;

            // index to 8 children, they are allocated in a contiguous memory block
            int Children;

            // list of objects in this node
            int ElementsIndex;
        };

        // we use ElementsIndex to refer the actual vector rather than nest std::vector in OctreeNode, so the size of OctreeNode is 32 bytes, and we can fit 2 objects in a cache line
        static_assert(sizeof(OctreeNode) == 32);


    public:
        LooseOctree(float size)
            : mBound({ -size * 0.5f, -size * 0.5f, -size * 0.5f }, { size * 0.5f, size * 0.5f, size * 0.5f })
        {
            // root node
            mNodeTable.emplace_back(mBound, -1, 0);
            mElementTable.emplace_back();
        }

        template<typename... Args>
        OctreeElement* AddObject(const AABB& bound, Args&&... args)
        {
            OctreeElement* element = AddObjectInternal(0, bound, std::forward<Args>(args)...);
            ASSERT(element);
            
            return element;
        }

        void RemoveElement(OctreeElement*& element) 
        {
            CheckElementId(element);
            mElementTable[mNodeTable[element->NodeIndex].ElementsIndex].Free(element);
            element = nullptr;
        }

        OctreeElement* UpdateElement(OctreeElement* element, AABB new_bound)
        {
            CheckElementId(element);

            if (mNodeTable[element->NodeIndex].Bound.Contain(new_bound))
            {
                // the octree node can keep the object if it's still in bound
                element->Bound = new_bound;
                return element;
            }
            else
            {
                // otherwise, remove and re-add it
                RemoveElement(element);
                return AddObjectInternal(0, element->Bound, std::move(element->Object));
            }
        }

        template<typename Func>
        void FrustumCull(const FrustumVolume& Frustum, const Func& func)
        {
            FrustumCullInternal(Frustum, 0, func);
        }

    protected:
        template<typename... Args>
        OctreeElement* AddObjectInternal(int node_index, const AABB& bound, Args&&... args)
        {
            if (!mNodeTable[node_index].Bound.Contain(bound))
            {
                return nullptr;
            }

            if (mNodeTable[node_index].IsLeaf())
            {
                if (mElementTable[mNodeTable[node_index].ElementsIndex].Size() + 1 > MaxCapacityToSplit && mNodeTable[node_index].Bound.Width() > MinNodeSize)
                {
                    SubDivide(node_index);

                    // try to reassign the objects in the current node to its children
                    ElementTable mTempPool = std::move(mElementTable[mNodeTable[node_index].ElementsIndex]);
                    for (OctreeElement& element : mTempPool)
                    {
                        ASSERT(AddObjectInternal(node_index, element.Bound, std::move(element.Object)));
                    }

                    return AddObjectInternal(node_index, bound, std::forward<Args>(args)...);
                }
                else
                {
                    return mElementTable[mNodeTable[node_index].ElementsIndex].Allocate(node_index, bound, std::forward<Args>(args)...);
                }
            }
            else
            {
                uint32 i = FindBestFitChild(node_index, bound);

                OctreeElement* element = AddObjectInternal(i, bound, std::forward<Args>(args)...);
                if (!element)
                {
                    return mElementTable[mNodeTable[node_index].ElementsIndex].Allocate(node_index, bound, std::forward<Args>(args)...);
                }
                else 
                {
                    return element;
                }
            }
        }

        void SubDivide(int node_index)
        {
            int child_index = static_cast<int>(mNodeTable.size());
            AABB bound = mNodeTable[node_index].Bound;
            Vector3 center = bound.Center();
            Vector3 half_size = bound.Size() * 0.5f;


            for (uint32 i = 0; i < NumOctreeLeaf; i++)
            {
                // the distribution of the children:
                /*    6-------7
                     /|      /|
                    2-+-----3 |
                    | |     | |   y
                    | 4-----+-5   | z
                    |/      |/    |/
                    0-------1     +--x

                         zyx
                    0: 0x000
                    1: 0x001
                    2: 0x010
                    3: 0x011
                    4: 0x100
                    5: 0x101
                    6: 0x110
                    7: 0x111

                    // child node only expand in one direction of the basis axis
                    // sometheing like this
                    |--------|---|
                    |        |   |
                    |--------|---|  y
                    |        |   |  ^
                    |        |   |  |
                    |        |   |  |
                    |--------|---|  ------> x
                */

                Vector3 max =
                {
                    i & 0x1 ? bound.Max.x : center.x,
                    i & 0x2 ? bound.Max.y : center.y,
                    i & 0x4 ? bound.Max.z : center.z
                };

                Vector3 min = max - half_size;
                mNodeTable.emplace_back(AABB(min, max), -1, static_cast<int>(mElementTable.size()));
                mElementTable.emplace_back();
            }

            mNodeTable[node_index].Children = child_index;
        }

        // find the closest child node that near this AABB
        uint32 FindBestFitChild(int node_index, const AABB& bound)
        {
            Vector3 vec = bound.Center() - mNodeTable[node_index].Bound.Center();
            uint32 index = 0;
            
            if (vec.x >= 0)
            {
                index |= 0x1;
            }

            if (vec.y >= 0) 
            {
                index |= 0x2;
            }

            if (vec.z >= 0) 
            {
                index |= 0x4;
            }

            return index + mNodeTable[node_index].Children;
        }

    protected:
        template<typename Func>
        void FrustumCullInternal(const FrustumVolume& volume, uint32 node_index, const Func& func)
        {
            const OctreeNode& node = mNodeTable[node_index];
            if (volume.Contains(node.Bound)) 
            {
                for (OctreeElement& element : mElementTable[node.ElementsIndex])
                {
                    if (volume.Contains(element.Bound)) 
                    {
                        func(element.Object);
                    }
                }

                if (!node.IsLeaf()) 
                {
                    for(uint32 i = 0; i < NumOctreeLeaf; i++) 
                    {
                        FrustumCullInternal(volume, node.Children + i, func);
                    }
                }
            }
        }

        inline void CheckElementId(OctreeElement* element) 
        {
            ASSERT(element->NodeIndex < mNodeTable.size() && mElementTable[mNodeTable[element->NodeIndex].ElementsIndex].Validate(element));
        }

    protected:
        AABB mBound;

        // @OctreeNode references children and objects it stores by indexing these two table.
        // the purpose of this is to ensure that @OctreeNode has a size of 32 bytes, which is more cache-friendly,
        // and we can additionally reuse object of type @OctreeNode and @T
        std::vector<OctreeNode> mNodeTable;
        std::vector<ElementTable> mElementTable;
        ElementTable mTempPool; // temporary pool for subdived process, it's declared as member to avoid heap allocation when subdiveding node
    };
}