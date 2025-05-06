#include "gtest/gtest.h"
#include "Utils\Allocator.h"
#include <vector>

TEST(ObjectPool, AllocationTest) {
    struct TestObj
    {
        int a;
        char* b;
        float c;
    };

    using namespace MRenderer;
    NestedObjectAllocator<TestObj> allocator;
    std::vector<TestObj*> objs;

    // the number of allocator's total object goes like 64, 64 + 96, 64 + 96 + 144
    // we will test allocation and free in second scenario, which means total object eq 64 + 96 = 160
    for (int i = 0; i < 160; i++) 
    {
        objs.push_back(allocator.Allocate());

        // alignment test
        ASSERT_EQ(size_t(objs.back()) % alignof(TestObj), 0);
    }

    const auto expect_total = 160;    
    auto total = allocator.GetStats().Total;
    
    ASSERT_EQ(allocator.GetStats().Occupied, objs.size()); //objs.size() = 160
    ASSERT_EQ(allocator.GetStats().Total, expect_total);
    ASSERT_EQ(allocator.GetStats().Avaliable, 0);

    // no more chunk allocation should be happend
    for (int i = 0; i < 96; i++) 
    {
        auto it = objs.begin() + rand() % objs.size();
        allocator.Free(*it);
        objs.erase(it);
    }

    ASSERT_EQ(allocator.GetStats().Occupied, objs.size()); // objs.size() = 64
    ASSERT_EQ(allocator.GetStats().Total, expect_total);
    ASSERT_EQ(allocator.GetStats().Avaliable, total - objs.size());

    for (int i = 0; i < 96; i++)
    {
        objs.push_back(allocator.Allocate());
        ASSERT_EQ(size_t(objs.back()) % alignof(TestObj), 0);
    }

    ASSERT_EQ(allocator.GetStats().Occupied, 160); // objs.size() = 160
    ASSERT_EQ(allocator.GetStats().Total, expect_total);
    ASSERT_EQ(allocator.GetStats().Avaliable, total - objs.size());

    uint32 size = 0;
    for (auto& it : allocator) 
    {
        size++;
    }
    ASSERT_EQ(allocator.GetStats().Occupied, size);
    ASSERT_EQ(allocator.Size(), size);

    while (!objs.empty()) 
    {
        allocator.Free(objs.back());
        objs.pop_back();
    }
    
    ASSERT_EQ(allocator.GetStats().Occupied, 0); // objs.size() = 0
    ASSERT_EQ(allocator.GetStats().Total, expect_total);
    ASSERT_EQ(allocator.GetStats().Avaliable, expect_total);
    ASSERT_EQ(allocator.Size(), 0);

}

TEST(TLSF, AlignmentTest)
{
    MRenderer::TLSFMeta meta(64 * 1024);
    using Allocation = decltype(meta)::Allocation;

    #define ASSERT_ALLOCATION(allocation, assert_size, assert_alignment)\
    ASSERT_NE((allocation), nullptr);\
    ASSERT_EQ((allocation)->size, (assert_size));\
    ASSERT_EQ((allocation)->offset % (assert_alignment), 0);\
    ASSERT_EQ((allocation)->alignment, (assert_alignment));\

    std::vector<Allocation*> alloc;
    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_ALLOCATION(alloc.back(), 256, 16);

    alloc.push_back(meta.Allocate(256, 32));
    ASSERT_ALLOCATION(alloc.back(), 256, 32);

    alloc.push_back(meta.Allocate(256, 64));
    ASSERT_ALLOCATION(alloc.back(), 256, 64);

    alloc.push_back(meta.Allocate(256, 32));
    ASSERT_ALLOCATION(alloc.back(), 256, 32);

    alloc.push_back(meta.Allocate(256, 128));
    ASSERT_ALLOCATION(alloc.back(), 256, 128);

    alloc.push_back(meta.Allocate(256, 256));
    ASSERT_ALLOCATION(alloc.back(), 256, 256);

    ASSERT_EQ(meta.GetStats().allocated_block, alloc.size());
    ASSERT_EQ(meta.GetStats().free_block, 0);

    while (!alloc.empty())
    {
        meta.Free(alloc.back());
        alloc.pop_back();
    }

    ASSERT_EQ(meta.GetStats().allocated_block, 0);
    ASSERT_EQ(meta.GetStats().free_block, 1);   // all free block will merge into one

    alloc.push_back(meta.Allocate(256, 256));
    ASSERT_ALLOCATION(alloc.back(), 256, 256);

    alloc.push_back(meta.Allocate(256, 128));
    ASSERT_ALLOCATION(alloc.back(), 256, 128);

    alloc.push_back(meta.Allocate(256, 64));
    ASSERT_ALLOCATION(alloc.back(), 256, 64);

    alloc.push_back(meta.Allocate(256, 32));
    ASSERT_ALLOCATION(alloc.back(), 256, 32);

    alloc.push_back(meta.Allocate(256, 64));
    ASSERT_ALLOCATION(alloc.back(), 256, 64);

    alloc.push_back(meta.Allocate(256, 32));
    ASSERT_ALLOCATION(alloc.back(), 256, 32);

    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_ALLOCATION(alloc.back(), 256, 16);

    ASSERT_EQ(meta.GetStats().allocated_block, alloc.size());
    ASSERT_EQ(meta.GetStats().free_block, 1); // the previous merged block will not be allocated during above allocation

    while (!alloc.empty())
    {
        meta.Free(alloc.back());
        alloc.pop_back();
    }

    ASSERT_EQ(meta.GetStats().allocated_block, 0);
    ASSERT_EQ(meta.GetStats().free_block, 1); // every blocks will merge into one agine

    alloc.push_back(meta.Allocate(64, 16));
    ASSERT_ALLOCATION(alloc.back(), 64, 16);

    alloc.push_back(meta.Allocate(64, 32));
    ASSERT_ALLOCATION(alloc.back(), 64, 32);

    alloc.push_back(meta.Allocate(64, 64));
    ASSERT_ALLOCATION(alloc.back(), 64, 64);

    ASSERT_EQ(meta.GetStats().allocated_block, alloc.size());
    ASSERT_EQ(meta.GetStats().free_block, 1);

    while (!alloc.empty())
    {
        meta.Free(alloc.back());
        alloc.pop_back();
    }

    ASSERT_EQ(meta.GetStats().allocated_block, 0);
    ASSERT_EQ(meta.GetStats().free_block, 1);

    alloc.push_back(meta.Allocate(1024, 256));
    ASSERT_ALLOCATION(alloc.back(), 1024, 256);

    alloc.push_back(meta.Allocate(2048, 512));
    ASSERT_ALLOCATION(alloc.back(), 2048, 512);

    alloc.push_back(meta.Allocate(4096, 1024));
    ASSERT_ALLOCATION(alloc.back(), 4096, 1024);
}

TEST(TLSF, SplitMergeTest)
{
    MRenderer::TLSFMeta meta(1024);
    using Allocation = decltype(meta)::Allocation;

    size_t allocated = 0;
    size_t freed = 0;
    size_t unallocated = meta.Size();
    decltype(meta)::Stats stats;
    std::vector<Allocation*> alloc;

    #define ASSERT_STATS(delta_allocated, delta_freed, delta_unallocated, allocated_block_count, free_block_count)\
    allocated += (delta_allocated);\
    freed += (delta_freed);\
    unallocated += (delta_unallocated);\
    stats = meta.GetStats(); \
    ASSERT_EQ(stats.allocated_memory, allocated); \
    ASSERT_EQ(stats.free_memory, freed); \
    ASSERT_EQ(stats.unallocated_memory, unallocated); \
    ASSERT_EQ(stats.allocated_block, (allocated_block_count)); \
    ASSERT_EQ(stats.free_block, (free_block_count)); \
    ASSERT_EQ(stats.allocated_memory + stats.free_memory + stats.unallocated_memory, meta.Size()); \
    ASSERT_EQ(stats.block_allocator_stats.Occupied, stats.allocated_block + stats.free_block);\

    // MergeBlock test
    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, 0, -256, alloc.size(), 0);

    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, 0, -256, alloc.size(), 0);

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-256, 256, 0, alloc.size(), 1);

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-256, 256, 0, alloc.size(), 1); // two 256 block will merge into one 512 block

    alloc.push_back(meta.Allocate(512, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(512, -512, 0, alloc.size(), 0);

    alloc.push_back(meta.Allocate(512, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(512, 0, -512, alloc.size(), 0);

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-512, 512, 0, alloc.size(), 1);

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-512, 512, 0, alloc.size(), 1); // two 512 block will merge into one 1024 block

    alloc.push_back(meta.Allocate(1024, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(1024, -1024, 0, alloc.size(), 0);

    ASSERT_EQ(meta.Allocate(512, 16), nullptr); // should be out of memory here

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-1024, 1024, 0, alloc.size(), 1);


    // SplitBlock test
    alloc.push_back(meta.Allocate(512, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(512, -512, 0, alloc.size(), 1); // the 1024 block will split into two 512 block


    alloc.push_back(meta.Allocate(512, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(512, -512, 0, alloc.size(), 0);

    ASSERT_EQ(meta.Allocate(512, 16), nullptr); // should be out of memory here

    meta.Free(alloc.back());
    alloc.pop_back();
    ASSERT_STATS(-512, 512, 0, alloc.size(), 1);

    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, -256, 0, alloc.size(), 1); // the 512 block will split into two 256 block

    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, -256, 0, alloc.size(), 0);

    ASSERT_EQ(meta.Allocate(512, 16), nullptr); // should be out of memory here

    while (!alloc.empty())
    {
        meta.Free(alloc.back());
        alloc.pop_back();
    }

    ASSERT_STATS(-1024, 1024, 0, alloc.size(), 1); // every blocks will merge into one agine

    // the 1024 block will  split into |256 | 256 | 512|
    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, -256, 0, alloc.size(), 1);

    alloc.push_back(meta.Allocate(512, 512));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(512, -512, 0, alloc.size(), 1);

    alloc.push_back(meta.Allocate(256, 16));
    ASSERT_NE(alloc.back(), nullptr);
    ASSERT_STATS(256, -256, 0, alloc.size(), 0);

    while (!alloc.empty())
    {
        meta.Free(alloc.back());
        alloc.pop_back();
    }

    ASSERT_STATS(-1024, 1024, 0, alloc.size(), 1);
}