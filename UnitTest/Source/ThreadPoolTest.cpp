#include "gtest/gtest.h"
#include "Utils/Thread.h"
#include "format"

// 设置线程睡眠时间
void simulate_hard_computation()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(2000 + std::rand() % 2000));
}

// 添加并输出结果
void multiply_output(int& out, const int a, const int b)
{
    simulate_hard_computation();
    out = a * b;
    std::cout << std::format("{} * {} = {}\n", a, b, out);
}

// 结果返回
int multiply_return(const int a, const int b)
{
    simulate_hard_computation();
    const int res = a * b;
    std::cout << std::format("{} * {} = {}\n", a, b, res);
    return res;
}


TEST(ThreadPool, ScheduleTest)
{
    MRenderer::ThreadPool pool(32);

    int output_ref;
    auto future1 = pool.Schedule(multiply_output, std::ref(output_ref), 5, 6);

    future1.get();
    ASSERT_EQ(output_ref, 5 * 6);

    auto future2 = pool.Schedule(multiply_return, 5, 3);

    int res = future2.get();
    ASSERT_EQ(res, 5 * 3);

    std::vector<std::pair<std::future<int>, int>> futures(100);
    for (size_t i = 0; i < 100; i++) 
    {
        int a = std::rand() % 10;
        int b = std::rand() % 10;
        auto future = pool.Schedule(multiply_return, a, b);

        futures[i] = std::pair(std::move(future), a * b);
    }

    for (size_t i = 0; i < 100; i++)
    {
        auto& [future, expect_num] = futures[i];
        ASSERT_EQ(future.get(), expect_num);
    }

}