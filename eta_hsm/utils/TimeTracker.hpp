// eta_hsm/utils/TimeTracker.hpp
#pragma once

#include <array>
#include <chrono>
#include <cstddef>

#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm {
namespace utils {

/// A utility to keep track of how long we have been in each (potentially nested) state.
template <typename StateEnum, typename LocalClock>
class TimeTracker {
public:
    TimeTracker(const LocalClock& clock) : mClock{clock} {}

    /// We will return durations with whatever precision the provided clock uses.
    using Duration = std::chrono::duration<typename LocalClock::rep, typename LocalClock::period>;

    /// Record the time upon entry into each state.
    void enter(StateEnum state)
    {
        mEntryTimes.at(index(state)) = mClock.now();
        mInState.at(index(state)) = true;
    }

    /// Record that we have exited each state so that we can avoid returning incorrect durations
    /// for states that we are no longer in.
    void exit(StateEnum state) { mInState.at(index(state)) = false; }

    /// Query how long we have been in any particular state.
    /// If we are not currently in that state, return 0.
    Duration timeInState(StateEnum state) const
    {
        if (!mInState.at(index(state)))
        {
            return std::chrono::milliseconds{0};
        }
        return mClock.now() - mEntryTimes.at(index(state));
    }

private:
    const LocalClock& mClock;

    /// Index the per-state arrays by the enum's value, narrowed to the array size_type.
    /// The contiguous-from-zero contract below guarantees the value is in range.
    static constexpr std::size_t index(StateEnum state) { return static_cast<std::size_t>(state); }

    // TimeTracker indexes mEntryTimes/mInState by the raw StateEnum value
    // (index(state) in enter/exit/timeInState) and sizes them by
    // enum_count<StateEnum>() -- the enumerator COUNT, not max+1. So StateEnum must
    // be contiguous from 0; otherwise a state would index past the array. Make that
    // assumption a compile-time contract.
    static_assert(enum_is_contiguous_from_zero<StateEnum>(),
                  "TimeTracker indexes per-State arrays by the raw enum value; "
                  "StateEnum must be contiguous from 0 (no gaps, starts at 0).");

    /// A list of entry times for each state. Sized by the StateEnum's enumerator count via
    /// reflection.
    std::array<std::chrono::time_point<LocalClock>, enum_count<StateEnum>()> mEntryTimes{};

    /// A simple boolean record of whether we are currently in a particular state.
    /// We could probably encode this information with some sort of intentionally invalid mEntryTime,
    /// but that seemed more difficult to understand and potentially more error prone.
    std::array<bool, enum_count<StateEnum>()> mInState{false};
};

}  // namespace utils
}  // namespace eta_hsm
