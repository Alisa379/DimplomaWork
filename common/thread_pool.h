#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Пул потоков с общей очередью задач.
// Задачи добавляются через submit(); waitAndStop() дожидается выполнения
// всех поставленных задач и завершает потоки. Деструктор делает то же самое,
// поэтому потоки никогда не остаются «висеть».
class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(Task task);
    void waitAndStop();

    std::size_t size() const { return workers_.size(); }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stopping_ = false;
};
