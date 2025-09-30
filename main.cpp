#include <iostream>
#include <cstring>
#include <vector>
#include <cmath>
#include <future>
#include "DynamicThreadPool.h"

using namespace std;

// 基础功能测试
void testBasicFunctionality(DynamicThreadPool& pool)
{
    std::cout << "=== 基础功能测试 ===" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交简单任务
    std::vector<std::future<int>> results;
    for (int i = 0; i < 10; ++i) {
        results.push_back(pool.addTask([](int x) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return x * x;
        }, i));
    }
    
    // 收集结果
    for (auto& result : results) {
        std::cout << "Result: " << result.get() << std::endl;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "基础测试完成，耗时: " << duration.count() << "ms" << std::endl;
}

// 吞吐量测试
void testThroughput(DynamicThreadPool& pool)
{
    std::cout << "\n=== 吞吐量测试 ===" << std::endl;
    
    const int TASK_COUNT = 1000;
    std::atomic<int> completed_tasks{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交大量快速任务
    for (int i = 0; i < TASK_COUNT; ++i) {
        pool.addTask([&completed_tasks]() {
            // 模拟轻量级工作
            volatile int x = 0;
            for (int j = 0; j < 1000; ++j) {
                x += j;
            }
            completed_tasks++;
        });
    }
    
    // 等待所有任务完成
    while (completed_tasks < TASK_COUNT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double throughput = TASK_COUNT / (duration.count() / 1000.0);
    std::cout << "完成 " << TASK_COUNT << " 个任务，耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "吞吐量: " << throughput << " 任务/秒" << std::endl;
}

// 并发性能测试
void testConcurrentTasks(DynamicThreadPool& pool)
{
    std::cout << "\n=== 并发性能测试 ===" << std::endl;
    
    const int CONCURRENT_TASKS = 50;
    std::vector<std::future<void>> futures;
    std::atomic<int> running_tasks{0};
    std::atomic<int> max_concurrent{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交并发任务
    for (int i = 0; i < CONCURRENT_TASKS; ++i) {
        futures.push_back(pool.addTask([&running_tasks, &max_concurrent, i]() {
            int current = ++running_tasks;
            
            // 更新最大并发数
            int old_max = max_concurrent.load();
            while (current > old_max && 
                    !max_concurrent.compare_exchange_weak(old_max, current)) {
                // 循环直到成功更新
            }
            
            // 模拟工作负载
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            --running_tasks;
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "最大并发任务数: " << max_concurrent.load() << std::endl;
    std::cout << "并发测试耗时: " << duration.count() << "ms" << std::endl;
}

void testDynamicScaling(DynamicThreadPool& pool)
{
    std::cout << "\n=== 动态扩展测试 ===" << std::endl;
    
    // 第一阶段：少量任务
    std::cout << "第一阶段：提交10个任务" << std::endl;
    for (int i = 0; i < 10; ++i) {
        pool.addTask([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        });
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 第二阶段：突发大量任务
    std::cout << "第二阶段：提交100个任务" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.addTask([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "突发任务处理耗时: " << duration.count() << "ms" << std::endl;
}

// 资源使用测试
void testMemoryUsage(DynamicThreadPool& pool) 
{
    std::cout << "\n=== 资源使用测试 ===" << std::endl;
    
    const size_t LARGE_TASK_COUNT = 100;
    std::vector<std::future<std::vector<int>>> futures;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交内存密集型任务
    for (size_t i = 0; i < LARGE_TASK_COUNT; ++i) {
        futures.push_back(pool.addTask([]() -> std::vector<int> {
            // 分配较大内存
            std::vector<int> data(1000000); // 4MB
            for (size_t j = 0; j < data.size(); ++j) {
                data[j] = j % 256;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return data;
        }));
    }
    
    // 收集结果并计算内存使用
    size_t total_memory = 0;
    for (auto& future : futures) {
        auto data = future.get();
        total_memory += data.size() * sizeof(int);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "总内存分配: " << total_memory / (1024 * 1024) << " MB" << std::endl;
    std::cout << "内存测试耗时: " << duration.count() << "ms" << std::endl;
}

void testStress(DynamicThreadPool& pool) 
{
    std::cout << "\n=== 压力测试 ===" << std::endl;
    
    const int STRESS_TASK_COUNT = 10000;
    std::atomic<int> completed{0};
    std::atomic<int> failed{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 提交大量混合任务
    for (int i = 0; i < STRESS_TASK_COUNT; ++i) {
        try {
            pool.addTask([i, &completed, &failed]() {
                try {
                    // 混合不同类型的工作负载
                    if (i % 5 == 0) {
                        // CPU密集型
                        volatile double result = 0;
                        for (int j = 0; j < 10000; ++j) {
                            result += std::sin(j) * std::cos(j);
                        }
                    } else if (i % 5 == 1) {
                        // 短时I/O模拟
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    } else {
                        // 常规工作
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    completed++;
                } catch (...) {
                    failed++;
                }
            });
        } catch (...) {
            failed++;
        }
    }
    
    // 等待完成或超时
    auto timeout = std::chrono::seconds(30);
    auto check_start = std::chrono::high_resolution_clock::now();
    
    while (completed + failed < STRESS_TASK_COUNT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto now = std::chrono::high_resolution_clock::now();
        if (now - check_start > timeout) {
            std::cout << "压力测试超时!" << std::endl;
            break;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "压力测试结果:" << std::endl;
    std::cout << "完成任务: " << completed.load() << std::endl;
    std::cout << "失败任务: " << failed.load() << std::endl;
    std::cout << "总耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "平均吞吐量: " << (completed.load() / (duration.count() / 1000.0)) << " 任务/秒" << std::endl;
}

void TestDynamicThreadPool()
{
    cout << "============= 动态线程池测试 ================\n" << endl;
    DynamicThreadPool pool(4, 16, 5); // 最小4线程，最大16线程，空闲5秒后回收
    try {
        testBasicFunctionality(pool);
        testThroughput(pool);
        testConcurrentTasks(pool);
        testDynamicScaling(pool);
        testMemoryUsage(pool);
        testStress(pool);
    } catch (const std::exception& e) {
        std::cerr << "测试过程中出现异常: " << e.what() << std::endl;
    }
}

void TestFixedThreadPool()
{
    cout << "\n============= 固定线程池测试 ================\n" << endl;
    DynamicThreadPool pool(4, 4, 0); // 固定4线程
    try {
        testBasicFunctionality(pool);
        testThroughput(pool);
        testConcurrentTasks(pool);
        testDynamicScaling(pool);
        testMemoryUsage(pool);
        testStress(pool);
    } catch (const std::exception& e) {
        std::cerr << "测试过程中出现异常: " << e.what() << std::endl;
    }
}

int main()
{
    TestDynamicThreadPool();
    TestFixedThreadPool();
    return 0;
}