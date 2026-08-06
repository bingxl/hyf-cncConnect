#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <type_traits>
#include "CncData.hpp"

template<typename T>
class StreamingData {
public:
    void start(int total) {
        {
            std::lock_guard lock(mtx_);
            items_.clear();
        }
        total_ = total;
        remaining_ = total;
        loading_ = true;
    }

    void start_with(const std::vector<T>& initial) {
        {
            std::lock_guard lock(mtx_);
            items_ = initial;
        }
        int n = static_cast<int>(initial.size());
        total_ = n;
        remaining_ = n;
        loading_ = true;
    }

    void push(T item) {
        {
            std::lock_guard lock(mtx_);
            items_.push_back(std::move(item));
        }
        if (remaining_.fetch_sub(1) <= 1)
            loading_ = false;
    }

    void update(int index, T item) {
        {
            std::lock_guard lock(mtx_);
            if (index < 0 || index >= static_cast<int>(items_.size()))
                return;
            items_[index] = std::move(item);
        }
        if (remaining_.fetch_sub(1) <= 1)
            loading_ = false;
    }

    void finish() {
        loading_ = false;
        remaining_ = 0;
    }

    bool is_loading() const { return loading_.load(); }
    int count() const { return static_cast<int>(items_.size()); }
    int total() const { return total_.load(); }
    int completed() const { return total_.load() - remaining_.load(); }

    std::lock_guard<std::mutex> lock() { return std::lock_guard(mtx_); }
    std::vector<T>& items() { return items_; }

private:
    std::mutex mtx_;
    std::vector<T> items_;
    std::atomic<int> total_{0};
    std::atomic<int> remaining_{0};
    std::atomic<bool> loading_{false};
};

template<typename T, typename Func>
void streaming_fetch_update(
    const std::vector<MachineInfo>& machines,
    StreamingData<T>& target,
    std::vector<T> initial,
    Func&& per_machine_fn)
{
    target.start_with(initial);
    if (machines.empty()) {
        target.finish();
        return;
    }
    std::thread([machines, &target, fn = std::forward<Func>(per_machine_fn)]() mutable {
        std::vector<std::jthread> threads;
        threads.reserve(machines.size());
        for (size_t i = 0; i < machines.size(); ++i) {
            threads.emplace_back([&machines, &target, &fn, i]() {
                T result = fn(machines[i]);
                target.update(static_cast<int>(i), std::move(result));
            });
        }
    }).detach();
}
