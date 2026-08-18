// PS5x – Metrics unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PS5x/Metrics/Metrics.h"
#include "PS5x/Logger/Logger.h"

TEST_CASE("Metrics::Counter", "[metrics][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Metrics::Init();
    auto& c = PS5x::Metrics::GetCounter("test.counter");
    REQUIRE(c.Get() == 0);
    c.Increment();
    REQUIRE(c.Get() == 1);
    c.Increment(5);
    REQUIRE(c.Get() == 6);
    c.Reset();
    REQUIRE(c.Get() == 0);
    PS5x::Metrics::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Metrics::Gauge", "[metrics][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Metrics::Init();
    auto& g = PS5x::Metrics::GetGauge("test.gauge");
    REQUIRE(g.Get() == 0.0);
    g.Set(42.5);
    REQUIRE_THAT(g.Get(), Catch::Matchers::WithinAbs(42.5, 0.001));
    g.Set(0.0);
    REQUIRE(g.Get() == 0.0);
    PS5x::Metrics::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Metrics::Histogram", "[metrics][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Metrics::Init();
    auto& h = PS5x::Metrics::GetHistogram("test.latency");
    h.Record(10.0);
    h.Record(20.0);
    h.Record(50.0);
    auto snap = h.GetSnapshot();
    REQUIRE(snap.count == 3);
    REQUIRE(snap.min == 10.0);
    REQUIRE(snap.max == 50.0);
    h.Reset();
    snap = h.GetSnapshot();
    REQUIRE(snap.count == 0);
    PS5x::Metrics::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Metrics::Registry", "[metrics][phase9]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Metrics::Init();
    auto& c1 = PS5x::Metrics::GetCounter("registry.test1");
    auto& c2 = PS5x::Metrics::GetCounter("registry.test2");
    c1.Increment(10);
    c2.Increment(20);
    // Same counter returns same reference
    PS5x::Metrics::GetCounter("registry.test1").Increment(5);
    REQUIRE(c1.Get() == 15);
    PS5x::Metrics::ResetAll();
    REQUIRE(c1.Get() == 0);
    REQUIRE(c2.Get() == 0);
    PS5x::Metrics::Shutdown();
    PS5x::Logger::Shutdown();
}
