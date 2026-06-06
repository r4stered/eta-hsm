// Behavioral tests for the auto-logging layer. They drive an
// AutoLoggedMachine -- a Machine wrapped with a pluggable Logger -- through its
// public interface and assert the exact human-readable lines a scripted run
// emits at each verbosity level, captured by a test Logger. State/Event names
// come from enum reflection.

#include "eta_hsm/log/auto_logged_machine.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "eta_hsm/examples/nested/nested.hpp"

namespace eta_hsm::examples::nested {
namespace {

// A minimal sink: captures each formatted line so a test can assert the exact
// sequence a run emits. This is the whole Logger contract -- one log() member.
struct CapturingLogger {
    std::vector<std::string> lines;
    void log(std::string_view line) { lines.emplace_back(line); }
};

// At verbosity 1 a single Transition emits exactly one line, reading
// "from <State> to <State> due to <Event>" with names from reflection, prefixed
// by the machine name. ToB targets the Composite B and drills to its Leaf B1a,
// so the leaf-to-leaf line runs A1 -> B1a.
TEST(AutoLoggedMachine, TransitionLineAtVerbosityOne)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 1};
    logger.lines.clear();  // drop construction-time output (none at verbosity 1)

    m.dispatch(Event::ToB);

    ASSERT_EQ(logger.lines.size(), 1u);
    EXPECT_EQ(logger.lines[0], "nested HSM transitioning from A1 to B1a due to ToB");
}

// Verbosity 0 is silence: neither construction nor any Dispatch emits a line.
TEST(AutoLoggedMachine, VerbosityZeroEmitsNothing)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 0};

    m.dispatch(Event::ToB);  // A1 -> B1a, with entry/exit/init chain
    m.dispatch(Event::Reset);  // back to A1 from a deep Leaf
    m.dispatch(Event::Go);  // A1 -> B1b

    EXPECT_TRUE(logger.lines.empty());
}

// At verbosity 2 a Transition additionally emits an Exit line for every State it
// leaves (bottom-up) and an Entry line for every State it enters (top-down), with
// the leaf-to-leaf Transition line last and no init lines. Go crosses from the A
// branch to the B branch under Top: exit A1, A; enter B, B1, B1b.
TEST(AutoLoggedMachine, EntryExitLinesAtVerbosityTwo)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 2};
    logger.lines.clear();  // drop the construction Entry chain

    m.dispatch(Event::Go);

    EXPECT_EQ(logger.lines, (std::vector<std::string>{
                                "nested HSM exiting state A1",
                                "nested HSM exiting state A",
                                "nested HSM entering state B",
                                "nested HSM entering state B1",
                                "nested HSM entering state B1b",
                                "nested HSM transitioning from A1 to B1b due to Go",
                            }));
}

// At verbosity 3 a Transition additionally emits an init line for the declared
// Target and for every Initial Substate it drills through (each right after that
// State's Entry line). ToB targets the Composite B and drills B -> B1 -> B1a, so
// B, B1, and B1a each get an init line; the path ancestors entered above the
// Target (none here) would not.
TEST(AutoLoggedMachine, InitLinesAtVerbosityThree)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 3};
    logger.lines.clear();  // drop the construction init chain

    m.dispatch(Event::ToB);

    EXPECT_EQ(logger.lines, (std::vector<std::string>{
                                "nested HSM exiting state A1",
                                "nested HSM exiting state A",
                                "nested HSM entering state B",
                                "nested HSM initializing state B",
                                "nested HSM entering state B1",
                                "nested HSM initializing state B1",
                                "nested HSM entering state B1a",
                                "nested HSM initializing state B1a",
                                "nested HSM transitioning from A1 to B1a due to ToB",
                            }));
}

// Construction enters the initial configuration -- Top drilled down to the
// resting Leaf -- and at verbosity 3 it emits an Entry and an init line for each
// State on the way down, and no Transition line (there is no prior State to
// transition from). The machine drills Top -> A -> A1.
TEST(AutoLoggedMachine, ConstructionInitChainAtVerbosityThree)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 3};

    EXPECT_EQ(logger.lines, (std::vector<std::string>{
                                "nested HSM entering state Top",
                                "nested HSM initializing state Top",
                                "nested HSM entering state A",
                                "nested HSM initializing state A",
                                "nested HSM entering state A1",
                                "nested HSM initializing state A1",
                            }));
}

// At verbosity 2 the construction chain shows Entries only -- no init lines.
TEST(AutoLoggedMachine, ConstructionEntriesOnlyAtVerbosityTwo)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 2};

    EXPECT_EQ(logger.lines, (std::vector<std::string>{
                                "nested HSM entering state Top",
                                "nested HSM entering state A",
                                "nested HSM entering state A1",
                            }));
}

// The full picture: a scripted multi-Event run at verbosity 3, captured from
// construction through three Dispatches -- a Composite-target drill (ToB), a
// sibling Transition (Within), and an Event that defers to Top and resets the
// machine (Reset). The complete transcript is the spec for how the layer reads.
TEST(AutoLoggedMachine, ScriptedRunTranscriptAtVerbosityThree)
{
    CapturingLogger logger;
    AutoLoggedMachine<model, CapturingLogger> m{"nested", logger, 3};

    m.dispatch(Event::ToB);  // A1 -> B, drills to B1a
    m.dispatch(Event::Within);  // B1a -> B1b
    m.dispatch(Event::Reset);  // defers to Top -> A, drills to A1

    EXPECT_EQ(logger.lines, (std::vector<std::string>{
                                // construction: Top drilled to A1
                                "nested HSM entering state Top",
                                "nested HSM initializing state Top",
                                "nested HSM entering state A",
                                "nested HSM initializing state A",
                                "nested HSM entering state A1",
                                "nested HSM initializing state A1",
                                // ToB: exit A1, A; enter+init B, B1, B1a
                                "nested HSM exiting state A1",
                                "nested HSM exiting state A",
                                "nested HSM entering state B",
                                "nested HSM initializing state B",
                                "nested HSM entering state B1",
                                "nested HSM initializing state B1",
                                "nested HSM entering state B1a",
                                "nested HSM initializing state B1a",
                                "nested HSM transitioning from A1 to B1a due to ToB",
                                // Within: exit B1a; enter+init B1b
                                "nested HSM exiting state B1a",
                                "nested HSM entering state B1b",
                                "nested HSM initializing state B1b",
                                "nested HSM transitioning from B1a to B1b due to Within",
                                // Reset: exit B1b, B1, B; Top-sourced External
                                // Transition exits+re-enters Top; enter+init A, A1
                                "nested HSM exiting state B1b",
                                "nested HSM exiting state B1",
                                "nested HSM exiting state B",
                                "nested HSM exiting state Top",
                                "nested HSM entering state Top",
                                "nested HSM entering state A",
                                "nested HSM initializing state A",
                                "nested HSM entering state A1",
                                "nested HSM initializing state A1",
                                "nested HSM transitioning from B1b to A1 due to Reset",
                            }));
}

}  // namespace
}  // namespace eta_hsm::examples::nested
