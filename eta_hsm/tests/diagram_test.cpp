// Tests for the diagram emitters. They drive the public emitter functions
// (to_plantuml / to_mermaid) over example tables and assert on the emitted text:
// the State hierarchy, the initial-substate arrows, and the Transition labels
// (Event, Guard, Action names recovered from reflection). The whole diagram is
// derived from the `constexpr` table, so these tests pin that the table is
// faithfully rendered -- never the table's internal layout. The acceptance
// centerpiece is the golden match for the cd_player machine.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "eta_hsm/diagram/mermaid.hpp"
#include "eta_hsm/diagram/plantuml.hpp"
#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/nested/nested.hpp"

namespace eta_hsm::diagram {
namespace {

using eta_hsm::examples::cd_player::player;
using eta_hsm::examples::nested::model;

// True if `haystack` contains `needle` as a substring.
bool contains(std::string const& haystack, std::string const& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// Tracer: a Transition's full label -- Event plus the reflection-recovered Action
// name -- appears in the PlantUML output. This exercises the whole path end to
// end: table walk, enum names, and member-pointer -> identifier resolution.
TEST(Diagram, PlantUmlLabelsTransitionWithEventAndAction)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(contains(uml, "Stopped --> Playing : Play / start_playback")) << uml;
}

// The document is wrapped in the PlantUML @startuml/@enduml markers.
TEST(Diagram, PlantUmlWrapsInStartEndMarkers)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(uml.starts_with("@startuml\n")) << uml;
    EXPECT_TRUE(uml.ends_with("@enduml\n")) << uml;
}

// A Composite State emits the initial-substate arrow for the State it defaults
// into. Top defaults into Stopped.
TEST(Diagram, PlantUmlShowsInitialSubstateArrow)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(contains(uml, "[*] --> Stopped")) << uml;
}

// A Guarded Transition carries its Guard name, in brackets, recovered from
// reflection -- between the Event and the Action.
TEST(Diagram, PlantUmlLabelsGuardName)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(contains(uml, "Stopped --> Open : Hammer [drawer_jammed] / open_drawer")) << uml;
}

// An Internal Transition is rendered as a description line on its State (no arrow,
// no State change), with its Event and Action.
TEST(Diagram, PlantUmlRendersInternalTransitionWithoutArrow)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(contains(uml, "Playing : VolumeUp / turn_up")) << uml;
    EXPECT_FALSE(contains(uml, "--> Playing : VolumeUp")) << "internal transition must not draw an arrow";
}

// A Self-Transition is a real arrow back to the same State (unlike an Internal),
// so it reads Source --> Source.
TEST(Diagram, PlantUmlRendersSelfTransitionAsArrow)
{
    std::string const uml = to_plantuml<player>();
    EXPECT_TRUE(contains(uml, "Playing --> Playing : Next / next_track")) << uml;
}

// Deep hierarchy: nested Composite States nest as nested blocks, each with its
// own initial-substate arrow, and a cross-level Transition is still labeled.
TEST(Diagram, PlantUmlNestsCompositeStatesToAnyDepth)
{
    std::string const uml = to_plantuml<model>();
    EXPECT_TRUE(contains(uml, "[*] --> A1")) << uml;  // A's initial substate
    EXPECT_TRUE(contains(uml, "[*] --> B1a")) << uml;  // B1's initial substate
    EXPECT_TRUE(contains(uml, "state B1a {")) << uml;  // a Leaf nested two deep
    EXPECT_TRUE(contains(uml, "B1a --> B2 : Up")) << uml;  // cross-level Transition
}

// A Local Transition is marked distinctly from an External one: they share
// Source/Target/Event but differ in whether the shared ancestor is exited and
// re-entered, so the diagram must not render them identically. The nested example
// has the matched pair A --> A1 (ReenterExt external, ReenterLoc local).
TEST(Diagram, PlantUmlDistinguishesLocalFromExternalTransition)
{
    std::string const uml = to_plantuml<model>();
    EXPECT_TRUE(contains(uml, "A --> A1 : ReenterLoc (local)")) << uml;
    EXPECT_TRUE(contains(uml, "A --> A1 : ReenterExt\n")) << uml;  // external: no marker
}

// Mermaid output uses the stateDiagram-v2 header and the same label vocabulary.
TEST(Diagram, MermaidUsesStateDiagramHeaderAndLabels)
{
    std::string const mmd = to_mermaid<player>();
    EXPECT_TRUE(mmd.starts_with("stateDiagram-v2\n")) << mmd;
    EXPECT_TRUE(contains(mmd, "Stopped --> Playing : Play / start_playback")) << mmd;
    EXPECT_TRUE(contains(mmd, "Playing : VolumeUp / turn_up")) << mmd;
}

// Acceptance: the cd_player PlantUML output matches its checked-in golden exactly.
TEST(Diagram, PlantUmlMatchesCdPlayerGolden)
{
    constexpr std::string_view kGolden =
#include "golden/cd_player.puml.inc"
        ;
    EXPECT_EQ(to_plantuml<player>(), kGolden);
}

// Acceptance: the cd_player Mermaid output matches its checked-in golden exactly.
TEST(Diagram, MermaidMatchesCdPlayerGolden)
{
    constexpr std::string_view kGolden =
#include "golden/cd_player.mmd.inc"
        ;
    EXPECT_EQ(to_mermaid<player>(), kGolden);
}

// The cd_player Mermaid diagram embedded in the README must equal the emitter's
// live output, so the README demo can never drift from the machine. This check
// reads the repo's README and is only compiled when the repo root is known (the
// CMake build defines ETA_REPO_ROOT); other build configurations skip it.
#ifdef ETA_REPO_ROOT
TEST(Diagram, ReadmeMermaidBlockStaysInSyncWithEmitter)
{
    std::string const path = std::string{ETA_REPO_ROOT} + "/README.md";
    std::ifstream in{path};
    ASSERT_TRUE(in.good()) << "cannot open " << path;
    std::string const readme{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

    // Extract the first ```mermaid ... ``` fenced block.
    constexpr std::string_view kOpen = "```mermaid\n";
    auto const begin = readme.find(kOpen);
    ASSERT_NE(begin, std::string::npos) << "README has no ```mermaid block";
    auto const body = begin + kOpen.size();
    auto const end = readme.find("```", body);
    ASSERT_NE(end, std::string::npos) << "unterminated ```mermaid block";

    std::string const block = readme.substr(body, end - body);
    EXPECT_EQ(block, to_mermaid<player>())
        << "README ```mermaid block is stale -- regenerate it from cd_player_diagram --mermaid";
}
#endif

}  // namespace
}  // namespace eta_hsm::diagram
