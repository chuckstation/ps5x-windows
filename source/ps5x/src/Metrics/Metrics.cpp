// PS5x – Metrics & Telemetry implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Metrics/Metrics.h"
#include "PS5x/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace PS5x::Metrics {

// ── Default exponential bucket boundaries ───────────────────────────────────
static const std::vector<double> kDefaultBounds = {
    1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0,
    250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0
};

// ── Histogram implementation ────────────────────────────────────────────────

Histogram::Histogram(const std::vector<double>& bounds)
    : bounds_(bounds.empty() ? kDefaultBounds : bounds)
{
    // One bucket per bound + overflow bucket
    buckets_.resize(bounds_.size() + 1);
    for (auto& b : buckets_)
        b.store(0, std::memory_order_relaxed);
}

void Histogram::Record(double value)
{
    // Find the bucket index: first bound where value <= bound
    size_t idx = bounds_.size(); // overflow bucket by default
    for (size_t i = 0; i < bounds_.size(); ++i)
    {
        if (value <= bounds_[i])
        {
            idx = i;
            break;
        }
    }

    buckets_[idx].fetch_add(1, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
    sum_.store(sum_.load(std::memory_order_relaxed) + value,
               std::memory_order_relaxed);

    // Update min (lock-free CAS loop)
    double curMin = min_.load(std::memory_order_relaxed);
    while (value < curMin &&
           !min_.compare_exchange_weak(curMin, value,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed))
    {
        // curMin is reloaded on failure
    }

    // Update max (lock-free CAS loop)
    double curMax = max_.load(std::memory_order_relaxed);
    while (value > curMax &&
           !max_.compare_exchange_weak(curMax, value,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed))
    {
        // curMax is reloaded on failure
    }
}

Histogram::Snapshot Histogram::GetSnapshot() const
{
    std::lock_guard lock(mtx_);

    Snapshot snap;
    snap.count = count_.load(std::memory_order_relaxed);
    snap.sum   = sum_.load(std::memory_order_relaxed);
    snap.min   = min_.load(std::memory_order_relaxed);
    snap.max   = max_.load(std::memory_order_relaxed);
    snap.mean  = (snap.count > 0) ? (snap.sum / static_cast<double>(snap.count)) : 0.0;

    if (snap.count == 0)
    {
        snap.p50 = snap.p95 = snap.p99 = 0.0;
        return snap;
    }

    // Build a sorted view of all recorded values from buckets to estimate percentiles.
    // We approximate by distributing bucket counts uniformly within bucket ranges.
    // For a more accurate estimation we collect bucket counts and compute from cumulative.

    // Collect bucket counts
    std::vector<uint64_t> counts(buckets_.size());
    for (size_t i = 0; i < buckets_.size(); ++i)
        counts[i] = buckets_[i].load(std::memory_order_relaxed);

    // Compute cumulative counts and estimate percentiles
    auto estimatePercentile = [&](double pct) -> double {
        uint64_t target = static_cast<uint64_t>(
            std::ceil(pct / 100.0 * static_cast<double>(snap.count)));
        if (target == 0) target = 1;

        uint64_t cumulative = 0;
        for (size_t i = 0; i < counts.size(); ++i)
        {
            cumulative += counts[i];
            if (cumulative >= target)
            {
                if (i == 0)
                    return std::min(snap.min, bounds_.empty() ? snap.max : bounds_[0]);
                if (i < bounds_.size())
                    return bounds_[i];
                return snap.max;
            }
        }
        return snap.max;
    };

    snap.p50 = estimatePercentile(50.0);
    snap.p95 = estimatePercentile(95.0);
    snap.p99 = estimatePercentile(99.0);

    return snap;
}

void Histogram::Reset()
{
    std::lock_guard lock(mtx_);
    for (auto& b : buckets_)
        b.store(0, std::memory_order_relaxed);
    count_.store(0, std::memory_order_relaxed);
    min_.store(std::numeric_limits<double>::max(), std::memory_order_relaxed);
    max_.store(std::numeric_limits<double>::lowest(), std::memory_order_relaxed);
    sum_.store(0.0, std::memory_order_relaxed);
}

// ── Registry implementation ─────────────────────────────────────────────────
namespace {

struct Registry
{
    std::mutex                                      mtx;
    std::unordered_map<std::string, Counter>        counters;
    std::unordered_map<std::string, Gauge>          gauges;
    std::unordered_map<std::string, Histogram>      histograms;
    bool                                            initialized{false};

    static Registry& Get()
    {
        static Registry r;
        return r;
    }
};

} // anonymous namespace

bool Init()
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);

    if (reg.initialized)
    {
        PS5X_WARN("Metrics registry already initialized – skipping");
        return true;
    }

    reg.initialized = true;
    PS5X_INFO("Metrics registry initialized");
    return true;
}

void Shutdown()
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);

    if (!reg.initialized)
        return;

    reg.counters.clear();
    reg.gauges.clear();
    reg.histograms.clear();
    reg.initialized = false;

    PS5X_INFO("Metrics registry shut down");
}

Counter& GetCounter(const std::string& name)
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);
    return reg.counters[name];
}

Gauge& GetGauge(const std::string& name)
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);
    return reg.gauges[name];
}

Histogram& GetHistogram(const std::string& name,
                        const std::vector<double>& bounds)
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);

    auto it = reg.histograms.find(name);
    if (it == reg.histograms.end())
    {
        // Create with the provided bounds (or default)
        auto [insIt, inserted] = reg.histograms.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(bounds));
        return insIt->second;
    }

    return it->second;
}

void LogAll()
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);

    PS5X_INFO("=== Metrics Snapshot ===");

    // Counters
    if (!reg.counters.empty())
    {
        PS5X_INFO("  Counters:");
        for (const auto& [name, ctr] : reg.counters)
            PS5X_INFO("    %-40s : %lld", name.c_str(),
                      static_cast<long long>(ctr.Get()));
    }

    // Gauges
    if (!reg.gauges.empty())
    {
        PS5X_INFO("  Gauges:");
        for (const auto& [name, g] : reg.gauges)
            PS5X_INFO("    %-40s : %.6f", name.c_str(), g.Get());
    }

    // Histograms
    if (!reg.histograms.empty())
    {
        PS5X_INFO("  Histograms:");
        for (const auto& [name, h] : reg.histograms)
        {
            auto snap = h.GetSnapshot();
            PS5X_INFO("    %-40s : count=%llu min=%.2f max=%.2f mean=%.2f "
                      "p50=%.2f p95=%.2f p99=%.2f sum=%.2f",
                      name.c_str(),
                      static_cast<unsigned long long>(snap.count),
                      snap.min, snap.max, snap.mean,
                      snap.p50, snap.p95, snap.p99, snap.sum);
        }
    }

    PS5X_INFO("=== End Metrics Snapshot ===");
}

void ResetAll()
{
    auto& reg = Registry::Get();
    std::lock_guard lock(reg.mtx);

    for (auto& [name, ctr] : reg.counters)
        ctr.Reset();

    for (auto& [name, h] : reg.histograms)
        h.Reset();

    // Gauges don't have Reset – set to zero
    for (auto& [name, g] : reg.gauges)
        g.Set(0.0);

    PS5X_INFO("All metrics reset");
}

} // namespace PS5x::Metrics
