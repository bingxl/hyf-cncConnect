#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <type_traits>
#include "CncData.hpp"

template<typename T>
class AsyncData {
public:
    template<typename Func>
    void start(Func&& fn) {
        bool expected = false;
        if (!loading_.compare_exchange_strong(expected, true)) return;
        loaded_ = false;

        std::thread([this, fn = std::forward<Func>(fn)]() mutable {
            T result = fn();
            {
                std::lock_guard lock(mtx_);
                data_ = std::move(result);
            }
            loaded_ = true;
            loading_ = false;
        }).detach();
    }

    bool is_loading() const { return loading_.load(); }
    bool is_loaded() const { return loaded_.load(); }

    void reset() {
        std::lock_guard lock(mtx_);
        data_ = T{};
        loaded_ = false;
        loading_ = false;
    }

    std::lock_guard<std::mutex> lock() { return std::lock_guard(mtx_); }
    const T& data() const { return data_; }
    T& mutable_data() { return data_; }

private:
    std::mutex mtx_;
    T data_{};
    std::atomic<bool> loading_{false};
    std::atomic<bool> loaded_{false};
};

template<typename Func>
auto parallel_fetch(const std::vector<MachineInfo>& machines, Func&& func)
    -> std::vector<std::invoke_result_t<Func, const MachineInfo&>>
{
    using R = std::invoke_result_t<Func, const MachineInfo&>;
    std::vector<R> results(machines.size());
    std::vector<std::jthread> threads;
    threads.reserve(machines.size());
    for (size_t i = 0; i < machines.size(); ++i) {
        threads.emplace_back([&results, &func, i, &machines]() {
            results[i] = func(machines[i]);
        });
    }
    return results;
}
