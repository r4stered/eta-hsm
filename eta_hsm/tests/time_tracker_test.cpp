// Behavioral test for TimeTracker (eta_hsm/utils/TimeTracker.hpp), ported from
// v1 to v2. The state key `TestEnum` is now a plain `enum class` (no wise_enum);
// TimeTracker sizes its per-state arrays by eta_hsm::enum_count<TestEnum>(), so
// this also proves the enum_count-based sizing matches the old wise_enum::size.
#include <gtest/gtest.h>

#include <chrono>

#include "eta_hsm/utils/FakeClock.hpp"
#include "eta_hsm/utils/TimeTracker.hpp"

namespace eta_hsm {
namespace utils {
namespace tests {

// Plain enum class state key (was WISE_ENUM_CLASS in v1). Its enumerator count
// sizes TimeTracker's per-state entry-time and in-state arrays.
enum class TestEnum : int64_t { eAlpha, eBravo, eCharlie };

TEST(TimeTrackerTest, TimeTrackerTest)
{
    auto clock = FakeClock{};

    TimeTracker<TestEnum, FakeClock> tracker(clock);

    // upon initialization, all states return 0
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{0});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{0});
    EXPECT_EQ(tracker.timeInState(TestEnum::eCharlie), std::chrono::seconds{0});

    // enter and exit a series of states
    tracker.enter(TestEnum::eAlpha);
    clock.advance(std::chrono::seconds{2});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{2});

    tracker.enter(TestEnum::eBravo);
    clock.advance(std::chrono::seconds{3});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{5});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{3});

    tracker.enter(TestEnum::eCharlie);
    clock.advance(std::chrono::seconds{7});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{12});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{10});
    EXPECT_EQ(tracker.timeInState(TestEnum::eCharlie), std::chrono::seconds{7});

    tracker.exit(TestEnum::eCharlie);
    clock.advance(std::chrono::seconds{5});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{17});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{15});
    EXPECT_EQ(tracker.timeInState(TestEnum::eCharlie), std::chrono::seconds{0});

    tracker.exit(TestEnum::eBravo);
    clock.advance(std::chrono::seconds{1});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{18});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{0});
    EXPECT_EQ(tracker.timeInState(TestEnum::eCharlie), std::chrono::seconds{0});

    tracker.exit(TestEnum::eAlpha);
    clock.advance(std::chrono::seconds{10});
    EXPECT_EQ(tracker.timeInState(TestEnum::eAlpha), std::chrono::seconds{0});
    EXPECT_EQ(tracker.timeInState(TestEnum::eBravo), std::chrono::seconds{0});
    EXPECT_EQ(tracker.timeInState(TestEnum::eCharlie), std::chrono::seconds{0});
}

}  // namespace tests
}  // namespace utils
}  // namespace eta_hsm
