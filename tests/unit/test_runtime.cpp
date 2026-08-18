// ChuckStation5 – Runtime Manager unit tests (Phase 3)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Runtime/Runtime.h"

using namespace ChuckStation5::Runtime;

// Isolated registry per test: shutdown clears state between runs
static void Setup()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    Reset(); // clear previous registrations safely
}
static void Teardown()
{
    ShutdownAll();
    Reset();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Runtime – SubsystemName returns non-empty string", "[runtime]")
{
    REQUIRE(std::string(SubsystemName(SubsystemId::Logger))   == "Logger");
    REQUIRE(std::string(SubsystemName(SubsystemId::Memory))   == "Memory");
    REQUIRE(std::string(SubsystemName(SubsystemId::Debugger)) == "Debugger");
    REQUIRE(std::string(SubsystemName(SubsystemId::COUNT))    == "Unknown");
}

TEST_CASE("Runtime – SubsystemStateName coverage", "[runtime]")
{
    REQUIRE(std::string(SubsystemStateName(SubsystemState::Running))  == "Running");
    REQUIRE(std::string(SubsystemStateName(SubsystemState::Failed))   == "Failed");
    REQUIRE(std::string(SubsystemStateName(SubsystemState::Stopped))  == "Stopped");
}

TEST_CASE("Runtime – single subsystem registers and inits", "[runtime]")
{
    Setup();
    bool initCalled = false, shutCalled = false;

    Register({
        SubsystemId::Logger, "Logger",
        [&]{ initCalled = true; return true; },
        [&]{ shutCalled = true; },
        {}, false
    });

    REQUIRE(InitAll());
    REQUIRE(initCalled);
    REQUIRE(IsRunning(SubsystemId::Logger));

    ShutdownAll();
    REQUIRE(shutCalled);
    REQUIRE(!IsRunning(SubsystemId::Logger));
    Teardown();
}

TEST_CASE("Runtime – dependency ordering enforced", "[runtime]")
{
    Setup();
    std::vector<std::string> order;

    Register({SubsystemId::Logger, "Logger",
        [&]{ order.push_back("Logger"); return true; }, []{}, {}, false});
    Register({SubsystemId::Config, "Config",
        [&]{ order.push_back("Config"); return true; }, []{},
        {SubsystemId::Logger}, false});
    Register({SubsystemId::Memory, "Memory",
        [&]{ order.push_back("Memory"); return true; }, []{},
        {SubsystemId::Logger}, false});

    REQUIRE(InitAll());
    // Logger must come before Config and Memory
    auto li = std::find(order.begin(), order.end(), "Logger");
    auto ci = std::find(order.begin(), order.end(), "Config");
    auto mi = std::find(order.begin(), order.end(), "Memory");
    REQUIRE(li < ci);
    REQUIRE(li < mi);
    Teardown();
}

TEST_CASE("Runtime – required subsystem failure aborts InitAll", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return false; }, // fails
        []{}, {}, false});   // NOT optional

    bool result = InitAll();
    REQUIRE(!result);
    REQUIRE(GetState(SubsystemId::Logger) == SubsystemState::Failed);
    Teardown();
}

TEST_CASE("Runtime – optional subsystem failure does not abort", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return true; }, []{}, {}, false});
    Register({SubsystemId::Audio, "Audio",
        []{ return false; }, // fails
        []{}, {SubsystemId::Logger}, true}); // optional

    REQUIRE(InitAll()); // still returns true
    REQUIRE(IsRunning(SubsystemId::Logger));
    REQUIRE(GetState(SubsystemId::Audio) == SubsystemState::Failed);
    Teardown();
}

TEST_CASE("Runtime – GetTiming records init time", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return true; }, []{}, {}, false});

    REQUIRE(InitAll());
    auto t = GetTiming(SubsystemId::Logger);
    REQUIRE(t.has_value());
    REQUIRE(t->initMs >= 0.0);
    REQUIRE(t->state == SubsystemState::Running);
    Teardown();
}

TEST_CASE("Runtime – GetAllTimings covers registered entries", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return true; }, []{}, {}, false});
    Register({SubsystemId::Config, "Config",
        []{ return true; }, []{}, {SubsystemId::Logger}, false});

    InitAll();
    auto all = GetAllTimings();
    REQUIRE(all.size() >= 2);
    Teardown();
}

TEST_CASE("Runtime – InitOne starts a single subsystem", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return true; }, []{}, {}, false});
    Register({SubsystemId::Config, "Config",
        []{ return true; }, []{}, {SubsystemId::Logger}, false});

    REQUIRE(InitOne(SubsystemId::Logger));
    REQUIRE(IsRunning(SubsystemId::Logger));
    REQUIRE(!IsRunning(SubsystemId::Config)); // not started yet
    Teardown();
}

TEST_CASE("Runtime – ReportTimings does not crash", "[runtime]")
{
    Setup();
    Register({SubsystemId::Logger, "Logger",
        []{ return true; }, []{}, {}, false});
    InitAll();
    ReportTimings(); // must not throw or crash
    Teardown();
}
