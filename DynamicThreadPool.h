#ifndef DYNAMICTHREADPOOL_H
#define DYNAMICTHREADPOOL_H 1

#include <iostream>
#include <atomic>
#include <vector>
#include <future>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

struct ThreadInfo
{
    std::thread                             thread_;            // 线程对象
    std::chrono::steady_clock::time_point   lastActiveTime_;    // 上次活跃时间
};

class DynamicThreadPool
{
public:
    using Task = std::packaged_task<void()>;

    DynamicThreadPool(size_t minthreads = 1, size_t maxthreads = std::thread::hardware_concurrency(), size_t idle_time = 30)
    : stop_(false), pending_tasks_(0), idle_time_(idle_time)
    {
        if(minthreads < 1) min_threadNums_.store(1);
        else min_threadNums_.store(minthreads);

        if(maxthreads < min_threadNums_.load()) max_threadNums_.store(min_threadNums_.load());
        else max_threadNums_.store(maxthreads);

        threadnums_.store(min_threadNums_.load());      // 取最小值为初始线程数

        for(size_t i = 0; i < threadnums_.load(); i ++)
        {
            auto thread_info = std::make_shared<ThreadInfo>();
            thread_info -> thread_ = std::thread(&DynamicThreadPool::workerThread, this, thread_info);
            thread_info -> lastActiveTime_ = std::chrono::steady_clock::now();
            pool_.emplace_back(thread_info);
        }

        adjust_thread_ = std::thread([this]() {
            while(!stop_.load()) {
                std::unique_lock<std::mutex> lock(pool_mtx_);
                
                // 每次添加任务后才检查是否需要增加线程
                pool_cv_.wait(lock, [this]() {
                    return stop_.load() || pending_tasks_.load() > threadnums_.load() * 2;
                });

                if(stop_.load()) break;

                // 检查是否需要增加线程
                // 如果待处理任务数大于当前线程数的两倍，且当前线程数小于最大线程数，则增加线程
                if( pending_tasks_.load() > threadnums_.load() * 2 
                    && threadnums_.load() < max_threadNums_.load()) {

                    size_t new_threads = std::min(
                        std::max((size_t)1, (pending_tasks_.load() / 2) - threadnums_.load()), 
                        max_threadNums_.load() - threadnums_.load()
                    );

                    for(size_t i = 0; i < new_threads; i ++) 
                    {
                        auto thread_info = std::make_shared<ThreadInfo>();
                        thread_info -> thread_ = std::thread(&DynamicThreadPool::workerThread, this, thread_info);
                        thread_info -> lastActiveTime_ = std::chrono::steady_clock::now();
                        pool_.emplace_back(thread_info);
                        threadnums_ ++;
                    }
                    // std::cout << "Increased thread count by " << new_threads 
                    //           << ". Current thread count: " << threadnums_.load() << std::endl;
                }
            }
        });

        std::cout << "ThreadPool initialized with " << threadnums_.load() << " threads." << std::endl;
    }

    ~DynamicThreadPool() { stop(); }

    template<typename Func, typename... Args>
    auto addTask(Func&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        using RetType = decltype(f(args...));

        auto task = std::make_shared<std::packaged_task<RetType()>>(std::bind(std::forward<Func>(f), std::forward<Args>(args)...));

        std::future<RetType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(tasks_mtx_);
            // if(stop_.load()) throw std::runtime_error("ThreadPool has been stopped, cannot add new tasks.");
            tasks_.emplace([task]() { (*task)(); });
        }

        pending_tasks_ ++;       // 增加待处理任务数

        tasks_cv_.notify_all();  // 通知工作线程有新任务

        pool_cv_.notify_one(); // 通知管理线程检查是否需要增加线程

        return result;
    }


private:
    // 工作线程函数
    void workerThread(std::shared_ptr<ThreadInfo> t_info)
    {
        // std::cout << "Thread " << syscall(SYS_gettid) << " started." << std::endl;
        while(!stop_.load())
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(tasks_mtx_);

                if(tasks_cv_.wait_for(lock, idle_time_, [this]() {
                    return stop_.load() || !tasks_.empty();
                })) 
                {
                    // 被唤醒，可能有任务或者停止信号
                    
                    if(stop_.load() && tasks_.empty()) break;

                    if(!tasks_.empty()) {
                        task = std::move(tasks_.front());
                        tasks_.pop();
                        pending_tasks_ --;
                    }
                } else {
                    // 超时未被唤醒，检查是否需要退出线程

                    if(threadnums_.load() > min_threadNums_.load()
                        && t_info -> lastActiveTime_ + idle_time_ <= std::chrono::steady_clock::now()) {
                        // 达到最大空闲时间，且线程数大于最小线程数，退出线程

                        // std::cout << "Thread " << syscall(SYS_gettid) << " is exiting due to inactivity." << std::endl;

                        threadnums_ --;     // 减少线程数

                        std::lock_guard<std::mutex> pool_lock(pool_mtx_);       // 保护线程池数据结构

                        // 移除当前线程信息
                        auto it = std::find_if(pool_.begin(), pool_.end(), [&t_info](std::shared_ptr<ThreadInfo> info) {
                            return (info -> thread_).get_id() == (t_info -> thread_).get_id();
                        });

                        if(it != pool_.end()) {
                            if(((*it)->thread_).joinable())
                                ((*it)->thread_).detach(); // 分离线程，避免阻塞
                            pool_.erase(it);
                        }

                        // std::cout << "Thread " << syscall(SYS_gettid) << " exited. Current thread count: " << threadnums_.load() << std::endl;
                        return; // 退出线程
                    }

                    // 没有任务，继续等待
                    continue;
                }
            }

            if(task.valid()) 
            {
                t_info -> lastActiveTime_ = std::chrono::steady_clock::now();      // 更新活跃时间
                task();
            }
        }
    }

    void stop()
    {
        stop_.store(true);

        // 通知所有线程退出
        tasks_cv_.notify_all();
        for(auto& th : pool_)
            if((th -> thread_).joinable()) 
                (th -> thread_).join();
        
        // 通知管理线程退出
        pool_cv_.notify_one();
        if(adjust_thread_.joinable())
            adjust_thread_.join();
        
        // 清理资源
        pool_.clear();
    }

private:
    std::atomic<bool>                           stop_;                  // 停止标志
    std::mutex                                  tasks_mtx_;             // 任务队列互斥锁
    std::condition_variable                     tasks_cv_;              // 任务队列条件变量
    std::vector<std::shared_ptr<ThreadInfo>>    pool_;                  // 线程池
    std::queue<Task>                            tasks_;                 // 任务队列
    std::atomic<size_t>                         min_threadNums_;        // 最小线程数
    std::atomic<size_t>                         max_threadNums_;        // 最大线程数
    std::atomic<size_t>                         threadnums_;            // 当前线程数
    std::atomic<size_t>                         pending_tasks_;         // 待处理任务数
    std::chrono::seconds                        idle_time_;             // 线程最大空闲时间


    // 管理者线程，动态调整线程池大小
    std::mutex                                  pool_mtx_;              // 线程池数据结构互斥锁
    std::condition_variable                     pool_cv_;               // 线程池条件变量
    std::thread                                 adjust_thread_;         // 管理者线程
};

#endif // DYNAMICTHREADPOOL_H