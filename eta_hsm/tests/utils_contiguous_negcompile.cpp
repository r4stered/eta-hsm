// Build-failure harness for the contiguous-from-zero enum contract (issue 0008).
//
// StaticTimerBank and TimeTracker size per-enumerator std::arrays by
// enum_count<E>() and index them by the RAW enum value (static_cast to the
// underlying integer). That is only sound when the enum's enumerators are
// exactly 0,1,2,...,N-1: a gapped / above-zero enum would index PAST the array.
// Each class now carries a static_assert(enum_is_contiguous_from_zero<E>()),
// turning that previously-silent assumption into a hard compile error.
//
// One CASE is selected per compile via -DCONTIG_CASE=<n>;
// tools/expect_compile_fail.sh compiles each case and asserts the build fails
// with a diagnostic matching the static_assert message ("contiguous from 0").
// Merely naming the type is not enough -- the member must be instantiated -- so
// each case declares an object of the offending specialization.

#include "eta_hsm/utils/TimeTracker.hpp"
#include "eta_hsm/utils/Timer.hpp"

namespace {

using namespace eta_hsm::utils;

// A non-contiguous state key: gap at value 2 (eCharlie == 3). Indexing a
// count-sized (3) array by static_cast<long long>(eCharlie) == 3 is out of
// bounds, which is exactly what enum_is_contiguous_from_zero rejects.
enum class NonContiguousStateEnum : int64_t { eAlpha = 0, eBravo = 1, eCharlie = 3 };

// A gapped group key for the StaticTimerBank case (value 2 skipped).
enum class GappedGroupEnum : uint32_t { eNone = 0, eRed = 1, eBlue = 3 };

// Minimal clock satisfying TimeTracker's LocalClock requirements.
struct DummyClock {
    using rep = int64_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<DummyClock>;
    time_point now() const { return {}; }
};

#ifndef CONTIG_CASE
#error "define CONTIG_CASE (1|2) to select a negative case"
#endif

#if CONTIG_CASE == 1
// TimeTracker over a gapped StateEnum: the per-State arrays are indexed by the
// raw enum value, so StateEnum must be contiguous from 0.
DummyClock gClock{};
TimeTracker<NonContiguousStateEnum, DummyClock> gTracker{gClock};

#elif CONTIG_CASE == 2
// StaticTimerBank over a gapped GroupEnum: the per-group array is indexed by the
// raw enum value, so GroupEnum must be contiguous from 0.
struct DummyEvent {
    enum class E { eNone };
};
StaticTimerBank<TimerTraits<DummyClock, DummyEvent::E, GappedGroupEnum>> gBank{};

#else
#error "CONTIG_CASE must be 1 or 2"
#endif

}  // namespace

int main() { return 0; }
