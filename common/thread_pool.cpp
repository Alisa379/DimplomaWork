#include "thread_pool.h"

#include <iostream>
#include <stdexcept>

ThreadPool::ThreadPool(std::size_t threadCount) {
    if (threadCount == 0) {
        threadCount = 1;
    }
    workers_.reserve(threadCount);
    try {
        for (std::size_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back(&ThreadPool::workerLoop, this);
        }
    } catch (...) {
        waitAndStop();  // корректно останавливаем уже созданные потоки
        throw;
    }
}

ThreadPool::~ThreadPool() {
    waitAndStop();
}

void ThreadPool::submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::logic_error("ThreadPool: нельзя добавлять задачи после остановки пула");
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

void ThreadPool::waitAndStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::workerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            // При остановке поток сначала доделывает оставшиеся в очереди задачи
            if (tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        // Задача выполняется вне блокировки, чтобы потоки работали параллельно.
        // Исключение из задачи не должно завершать рабочий поток.
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "Необработанное исключение в задаче: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "Необработанное исключение неизвестного типа в задаче\n";
        }
    }
}
