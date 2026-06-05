// Behavioral tests for the During tick and the auto-detected During/StateUpdate
// Host hooks (issue 0005). The During tick runs the current Leaf State's activity
// independent of any Event: `during()` calls the Host's during_<Name>() hook,
// `during(input)` calls stateUpdate_<Name>(input). Both are detected by the same
// reflection mechanism as entry/exit -- a Host writes only the hooks it needs.
//
// The hosts here are tiny and purpose-built: the all/some/none coverage the
// acceptance criteria ask for is about Host hook *presence*, not machine
// topology, so a trivial two-Leaf machine is enough. The current Leaf is the only
// State a During tick ever touches, so no hierarchy is needed to exercise it.

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

#include <gtest/gtest.h>

#include <string>

namespace eta_hsm::during_test {
namespace {

enum class State { Top, Running, Idle };
enum class Event { Toggle };

// The trivial machine shared by every Host: Top forwards into Running, and
// Toggle flips between the two Leaves. Parameterized on the Host so each Host
// variant below binds its own constexpr table.
template <class H>
constexpr auto make_table()
{
    return Hsm<H, State, Event>{}
        .state(State::Running, State::Top)
        .state(State::Idle, State::Top)
        .initial(State::Top, State::Running)
        .on(State::Running, Event::Toggle, State::Idle)
        .on(State::Idle, Event::Toggle, State::Running);
}

// Defines every During/StateUpdate hook, for both Leaves. `log` records which
// fired so tests can assert the tick reached exactly the right hook.
struct AllHooks {
    std::string log;
    void during_Running() { log += "during_Running;"; }
    void during_Idle() { log += "during_Idle;"; }
    void stateUpdate_Running(int input) { log += "stateUpdate_Running(" + std::to_string(input) + ");"; }
    void stateUpdate_Idle(int input) { log += "stateUpdate_Idle(" + std::to_string(input) + ");"; }
};
inline constexpr auto all_hooks = make_table<AllHooks>();

// Declares no During/StateUpdate hooks at all. A During tick on this Host must do
// nothing -- no stub or empty hook is required for the inert States.
struct NoHooks {
    std::string log;
};
inline constexpr auto no_hooks = make_table<NoHooks>();

// Declares only some of the hooks: a no-input during for one Leaf and an
// input-consuming stateUpdate for the other. Detection fires exactly the declared
// hooks and skips the absent ones, per State and per tick form.
struct SomeHooks {
    std::string log;
    void during_Running() { log += "during_Running;"; }
    void stateUpdate_Idle(int input) { log += "stateUpdate_Idle(" + std::to_string(input) + ");"; }
};
inline constexpr auto some_hooks = make_table<SomeHooks>();

// The During tick runs the current Leaf State's during_<Name>() hook. At rest the
// machine is in Running, so during() reaches during_Running.
TEST(During, FiresCurrentLeafDuringHook)
{
    Machine<all_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);
    m.during();
    EXPECT_EQ(m.host().log, "during_Running;");
}

// The During tick runs no Transition: the machine rests in the same Leaf it was
// in before the tick.
TEST(During, DoesNotChangeState)
{
    Machine<all_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);
    m.during();
    EXPECT_EQ(m.identify(), State::Running);
}

// The tick follows the current Leaf: once an Event moves the machine to Idle,
// during() reaches during_Idle, not the previous State's hook.
TEST(During, FollowsTheCurrentLeaf)
{
    Machine<all_hooks> m;
    m.dispatch(Event::Toggle);  // Running -> Idle
    ASSERT_EQ(m.identify(), State::Idle);
    m.host().log.clear();
    m.during();
    EXPECT_EQ(m.host().log, "during_Idle;");
}

// A Host that declares no During hook for the current Leaf has nothing called:
// the tick is a silent no-op, requiring no empty stub. State is unchanged.
TEST(During, MissingHookIsSilentlySkipped)
{
    Machine<no_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);
    m.during();
    EXPECT_EQ(m.host().log, "");
    EXPECT_EQ(m.identify(), State::Running);
}

// The input-consuming tick reaches the current Leaf's stateUpdate_<Name>(input)
// hook and forwards the Input value through to it.
TEST(During, InputTickFiresStateUpdateHookWithInput)
{
    Machine<all_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);
    m.during(7);
    EXPECT_EQ(m.host().log, "stateUpdate_Running(7);");
}

// Option A separation: during() routes only to during_<Name>, never to
// stateUpdate; during(input) routes only to stateUpdate_<Name>, never to during.
// The Input's presence alone picks the hook.
TEST(During, NoInputAndInputTicksReachDistinctHooks)
{
    Machine<all_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);

    m.during();
    EXPECT_EQ(m.host().log, "during_Running;");  // no stateUpdate_Running here

    m.host().log.clear();
    m.during(42);
    EXPECT_EQ(m.host().log, "stateUpdate_Running(42);");  // no during_Running here
}

// A Host declaring only some hooks fires exactly those: during() on Running runs
// during_Running, but during(input) on Running finds no stateUpdate_Running and
// does nothing; in Idle the reverse holds. Detection is independent per State and
// per tick form -- absent hooks need no stubs.
TEST(During, SomeHooksFiresOnlyTheDeclaredOnes)
{
    Machine<some_hooks> m;
    ASSERT_EQ(m.identify(), State::Running);

    m.during();
    EXPECT_EQ(m.host().log, "during_Running;");  // declared
    m.host().log.clear();
    m.during(1);
    EXPECT_EQ(m.host().log, "");  // no stateUpdate_Running

    m.dispatch(Event::Toggle);  // -> Idle
    ASSERT_EQ(m.identify(), State::Idle);
    m.host().log.clear();

    m.during();
    EXPECT_EQ(m.host().log, "");  // no during_Idle
    m.during(9);
    EXPECT_EQ(m.host().log, "stateUpdate_Idle(9);");  // declared
}

}  // namespace
}  // namespace eta_hsm::during_test
