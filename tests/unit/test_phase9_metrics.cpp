// ChuckStation5 – Metrics unit tests
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ChuckStation5/Metrics/Metrics.h"
#include "ChuckStation5/Logger/Logger.h"

TEST_CASE("Metrics::Counter", "[metrics][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Metrics::Init();
    auto& c = ChuckStation5::Metrics::GetCounter("test.counter");
    REQUIRE(c.Get() == 0);
    c.Increment();
    REQUIRE(c.Get() == 1);
    c.Increment(5);
    REQUIRE(c.Get() == 6);
    c.Reset();
    REQUIRE(c.Get() == 0);
    ChuckStation5::Metrics::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Metrics::Gauge", "[metrics][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Metrics::Init();
    auto& g = ChuckStation5::Metrics::GetGauge("test.gauge");
    REQUIRE(g.Get() == 0.0);
    g.Set(42.5);
    REQUIRE_THAT(g.Get(), Catch::Matchers::WithinAbs(42.5, 0.001));
    g.Set(0.0);
    REQUIRE(g.Get() == 0.0);
    ChuckStation5::Metrics::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Metrics::Histogram", "[metrics][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Metrics::Init();
    auto& h = ChuckStation5::Metrics::GetHistogram("test.latency");
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
    ChuckStation5::Metrics::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Metrics::Registry", "[metrics][phase9]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Metrics::Init();
    auto& c1 = ChuckStation5::Metrics::GetCounter("registry.test1");
    auto& c2 = ChuckStation5::Metrics::GetCounter("registry.test2");
    c1.Increment(10);
    c2.Increment(20);
    // Same counter returns same reference
    ChuckStation5::Metrics::GetCounter("registry.test1").Increment(5);
    REQUIRE(c1.Get() == 15);
    ChuckStation5::Metrics::ResetAll();
    REQUIRE(c1.Get() == 0);
    REQUIRE(c2.Get() == 0);
    ChuckStation5::Metrics::Shutdown();
    ChuckStation5::Logger::Shutdown();
}
