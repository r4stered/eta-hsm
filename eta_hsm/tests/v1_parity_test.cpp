// v1 golden-transcript parity. The frozen v1 HSM (tag v1.1.2) is the behavioral
// oracle: its own AutoLoggedStateMachine was driven through scripted Event streams
// once and its Transition / Entry / Exit / init transcript frozen as the golden
// files under golden/. These tests drive the v2 AutoLoggedMachine through the same
// streams and assert it reproduces each golden transcript line for line.
//
// The goldens are written in v2's enum spelling; v1's enumerators carried an `e`
// prefix and two misspellings (`eUnconcious`, `eDrinkWiskey`). The full v1->v2
// name mapping and the feature-parity coverage matrix live in
// v1_parity_checklist.md alongside this test.
//
// Construction output is dropped before each comparison: v1 bootstraps its initial
// configuration with an external self-Transition on Top that Exits and re-enters
// the root, an artifact v2's constructor does not reproduce. Parity is asserted on
// the per-Dispatch transcript, which is the machine's observable running behavior.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/example_control/example_control.hpp"
#include "eta_hsm/log/auto_logged_machine.hpp"

namespace eta_hsm {
namespace {

// Captures each formatted auto-log line so a run's transcript can be compared to
// the golden. One log() member is the whole Logger contract.
struct CapturingLogger {
    std::vector<std::string> lines;
    void log(std::string_view line) { lines.emplace_back(line); }
};

// Join captured lines into one newline-terminated string, matching the golden
// raw-string-literal layout (one line per Transition / Entry / Exit / init).
std::string transcript(const CapturingLogger& logger)
{
    std::string out;
    for (auto const& line : logger.lines)
    {
        out += line;
        out += '\n';
    }
    return out;
}

// cd_player: a scripted stream over the transitions shared with v1 (Play, Pause,
// EndPause, Stop, OpenClose, CdDetected, and the Top-handled Hammer). The Hammer
// while Stopped defers (its Guard is false) up to Top, whose External Transition to
// Playing Exits and re-enters the root -- so the transcript carries an Exit and
// Entry of Top, matching v1. The v2-only additions (VolumeUp, Next, the Hammer
// Guard's true branch) have no v1 equivalent and are covered by cd_player_test.
TEST(V1Parity, CdPlayerReproducesV1Transcript)
{
    constexpr std::string_view kGolden =
#include "golden/cd_player_transcript.inc"
        ;

    CapturingLogger logger;
    AutoLoggedMachine<examples::cd_player::player, CapturingLogger> m{"CDPlayer", logger, 3};
    logger.lines.clear();  // drop the bootstrap construction chain

    using E = examples::cd_player::Event;
    m.dispatch(E::Play);  // Stopped -> Playing
    m.dispatch(E::Pause);  // Playing -> Paused
    m.dispatch(E::EndPause);  // Paused -> Playing
    m.dispatch(E::Stop);  // Playing -> Stopped
    m.dispatch(E::OpenClose);  // Stopped -> Open
    m.dispatch(E::OpenClose);  // Open -> Empty
    m.dispatch(E::CdDetected);  // Empty -> Stopped
    m.dispatch(E::Hammer);  // Stopped defers to Top -> Playing (Top Exited + re-entered)
    m.dispatch(E::OpenClose);  // Playing -> Open (stop_and_open)

    EXPECT_EQ(transcript(logger), kGolden);
}

// example_control: the deep-hierarchy controller. The stream exercises Guard-gated
// drinking (a drink that does not cross the intoxication threshold defers to
// Awake's BAC-only Internal Transition, emitting no line; one that crosses tips
// Sober into Drunk), an Internal-Transition override (LookAtWatch while Drunk),
// Event deferral up the parent chain (DrinkBeer from Drunk defers to Awake), and a
// cross-level Transition through an intermediate Composite ancestor (PassOut runs
// the Exit chain Drunk -> Awake on the way to Unconscious).
TEST(V1Parity, ExampleControlReproducesV1Transcript)
{
    constexpr std::string_view kGolden =
#include "golden/example_control_transcript.inc"
        ;

    CapturingLogger logger;
    AutoLoggedMachine<examples::example_control::drinker, CapturingLogger> m{"ExampleControl", logger, 3};
    logger.lines.clear();  // drop the bootstrap construction chain

    using E = examples::example_control::Event;
    m.dispatch(E::DrinkWhiskey);  // Sober, BAC below threshold: defers to Awake, no line
    m.dispatch(E::DrinkWhiskey);  // crosses the threshold: Sober -> Drunk
    m.dispatch(E::LookAtWatch);  // Drunk Internal (keep partying): no line
    m.dispatch(E::DrinkBeer);  // Drunk defers to Awake's BAC-only handler: no line
    m.dispatch(E::PassOut);  // deferred to Awake: cross-level Drunk -> Unconscious

    EXPECT_EQ(transcript(logger), kGolden);
}

}  // namespace
}  // namespace eta_hsm
