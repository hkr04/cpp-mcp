/**
 * @file mcp_thread_pool.h
 * @brief Dynamic thread pool implementation
 */

#ifndef MCP_THREAD_POOL_H
#define MCP_THREAD_POOL_H

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <type_traits>
#include <chrono>
#include <stdexcept>

namespace mcp {

class thread_pool {
public:
    /**
     * @brief Constructor for dynamic thread pool
     * @param min_threads Minimum number of threads (core threads)
     * @param max_threads Maximum number of threads
     * @param idle_timeout_ms Idle time in milliseconds before a thread exits
     */
    explicit thread_pool(
        size_t min_threads = (std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 2), 
        size_t max_threads = (std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() * 4 : 16),
        size_t idle_timeout_ms = 60000) 
        : min_threads_(min_threads), 
          max_threads_(max_threads), 
          idle_timeout_(idle_timeout_ms), 
          stop_(false) {
        
        if (min_threads_ == 0) min_threads_ = 1;
        if (max_threads_ < min_threads_) max_threads_ = min_threads_;

        // Start core threads
        for (size_t i = 0; i < min_threads_; ++i) {
            spawn_thread();
        }
    }
    
    /**
     * @brief Destructor
     */
    ~thread_pool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        
        condition_.notify_all();
        
        // Wait for all detached threads to cleanly exit
        std::unique_lock<std::mutex> lock(queue_mutex_);
        shutdown_cv_.wait(lock, [this] { return active_threads_ == 0; });
    }
    
    /**
     * @brief Submit task to thread pool
     * @param f Task function
     * @param args Task parameters
     * @return Task future
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            if (stop_) {
                throw std::runtime_error("Thread pool stopped, cannot add task");
            }
            
            tasks_.emplace([task]() { (*task)(); });
            
            // Auto-expansion logic:
            // If there are no idle threads available to pick up this new task, 
            // and we haven't reached the maximum allowed threads, spawn a new one.
            if (idle_threads_ == 0 && active_threads_ < max_threads_) {
                spawn_thread();
            }
        }
        
        condition_.notify_one();
        return result;
    }

    /**
     * @brief Get current number of active threads (for monitoring)
     */
    size_t get_active_threads() const { 
        return active_threads_.load(); 
    }
    
private:
    void spawn_thread() {
        active_threads_++;
        std::thread([this]() {
            worker_loop();
        }).detach();
    }

    void worker_loop() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                idle_threads_++;

                // Wait for tasks or idle timeout
                const bool is_timeout = !condition_.wait_for(lock, idle_timeout_, [this] {
                    return stop_ || !tasks_.empty();
                });

                idle_threads_--;

                // Exit condition 1: Destructor called and queue is empty
                // Exit condition 2: Thread timed out, queue is empty, and we can shrink
                const bool stopping = stop_ && tasks_.empty();
                const bool shrinking = is_timeout && tasks_.empty() && active_threads_ > min_threads_;
                if (stopping || shrinking) {
                    // The destructor waits until active_threads_ hits zero, then
                    // destroys this pool. Publish the exit and wake it before
                    // releasing the mutex, and do not touch the pool afterward.
                    // A later lock (the old post-loop notify) races that destroy:
                    // another worker can already have dropped the count to zero.
                    if (active_threads_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        shutdown_cv_.notify_all();
                    }
                    return;
                }

                // Handle spurious wakeups
                if (tasks_.empty()) {
                    continue;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            // Execute the task without holding the lock
            task();
        }
    }

    size_t min_threads_;
    size_t max_threads_;
    std::chrono::milliseconds idle_timeout_;
    
    std::atomic<size_t> active_threads_{0};
    size_t idle_threads_{0}; // protected by queue_mutex_
    
    std::queue<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable shutdown_cv_;
    
    bool stop_;
};

} // namespace mcp

#endif // MCP_THREAD_POOL_H