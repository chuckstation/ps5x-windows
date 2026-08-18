// PS5x – Metrics & Telemetry
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PS5x::Metrics {

// Counter: monotonically increasing value
class Counter {
public:
    void Increment(int64_t delta = 1) { value_.fetch_add(delta, std::memory_order_relaxed); }
    int64_t Get() const { return value_.load(std::memory_order_relaxed); }
    void Reset() { value_.store(0, std::memory_order_relaxed); }
private:
    std::atomic<int64_t> value_{0};
};

// Gauge: point-in-time value
class Gauge {
public:
    void Set(double v) { value_.store(v, std::memory_order_relaxed); }
    double Get() const { return value_.load(std::memory_order_relaxed); }
private:
    std::atomic<double> value_{0.0};  // Use atomic<double> (C++20)
};

// Histogram: value distribution with configurable buckets
class Histogram {
public:
    explicit Histogram(const std::vector<double>& bounds = {});
    void Record(double value);
    struct Snapshot {
        double min, max, mean, p50, p95, p99;
        uint64_t count;
        double sum;
    };
    Snapshot GetSnapshot() const;
    void Reset();
private:
    std::vector<double> bounds_;
    std::unique_ptr<std::atomic<uint64_t>[]> buckets_;
    std::atomic<uint64_t> count_{0};
    std::atomic<double> min_{0}, max_{0}, sum_{0};
    mutable std::mutex mtx_;
};

// Registry: singleton for named metrics
bool Init();
void Shutdown();

Counter&  GetCounter(const std::string& name);
Gauge&    GetGauge(const std::string& name);
Histogram& GetHistogram(const std::string& name,
                        const std::vector<double>& bounds = {});

// Snapshot all metrics as structured log
void LogAll();

// Reset all metrics
void ResetAll();

} // namespace PS5x::Metrics
