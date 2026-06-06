# v1 feature-parity checklist

The frozen v1 HSM (tag `v1.1.2`) is the behavioral oracle for the v2 redesign.
This document records two things:

1. **Golden-transcript parity** — v1's own `AutoLoggedStateMachine` was driven
   through scripted Event streams once and its Transition / Entry / Exit / init
   transcript frozen as golden files. `v1_parity_test.cpp` asserts the v2
   `AutoLoggedMachine` reproduces those transcripts line for line.
2. **Capability coverage** — every v1 HSM capability maps to a v2 test that
   exercises it. Gaps and intentional divergences are listed explicitly.

## How the oracle was captured

v1 is frozen, so a one-time capture is the oracle (no live two-toolchain harness).
A snapshot of tag `v1.1.2` was driven through the streams below via v1's
`AutoLoggedStateMachine<…, utils::TestLog>` at verbosity 3 and the auto-log lines
(the four `<name> HSM …` shapes) frozen. `ExampleControl` already logged through
`TestLog`; v1's `cd_player` never used the auto-logger, so its `Player` was
temporarily re-hosted on `AutoLoggedStateMachine` for the capture. The capture
harness is not part of the repository — the goldens it produced are.

The auto-log wording is line-for-line identical between v1's
`AutoLoggedStateMachine` and v2's `LoggingObserver`, at all four verbosity levels
(`0` none, `1` Transitions, `2` + Entry/Exit, `3` + init), so parity is a direct
string comparison once names are mapped.

## v1 → v2 name mapping

v1 enumerators carried an `e` prefix and two misspellings; v2 dropped the prefix
and corrected the spellings. The goldens are written in v2's spelling.

### cd_player

| v1 State   | v2 State  | v1 Event       | v2 Event     |
|------------|-----------|----------------|--------------|
| `eTop`     | `Top`     | `ePlay`        | `Play`       |
| `eStopped` | `Stopped` | `eOpenClose`   | `OpenClose`  |
| `eOpen`    | `Open`    | `eStop`        | `Stop`       |
| `eEmpty`   | `Empty`   | `eCdDetected`  | `CdDetected` |
| `ePlaying` | `Playing` | `ePause`       | `Pause`      |
| `ePaused`  | `Paused`  | `eEndPause`    | `EndPause`   |
| `eBroken`  | `Broken`  | `eHammer`      | `Hammer`     |

### example_control (v1 `controller`)

| v1 State       | v2 State       | v1 Event        | v2 Event        |
|----------------|----------------|-----------------|-----------------|
| `eTop`         | `Top`          | `eDrinkBeer`    | `DrinkBeer`     |
| `eAwake`       | `Awake`        | `eDrinkWiskey`  | `DrinkWhiskey`  |
| `eSober`       | `Sober`        | `eLookAtWatch`  | `LookAtWatch`   |
| `eDrunk`       | `Drunk`        | `ePassOut`      | `PassOut`       |
| `eBored`       | `Bored`        | `eStartWatch`   | *(timer-only,*  |
| `eUnconcious`  | `Unconscious`  |                 | *not ported)*   |

## Golden streams

- **cd_player** (`golden/cd_player_transcript.inc`): `Play, Pause, EndPause, Stop,
  OpenClose, OpenClose, CdDetected, Hammer, OpenClose` — the transitions shared with
  v1. The Leaf-to-Leaf moves stay under `Top`; the `Hammer` defers to `Top`, whose
  External Transition to `Playing` Exits and re-enters the root.
- **example_control** (`golden/example_control_transcript.inc`): `DrinkWhiskey,
  DrinkWhiskey, LookAtWatch, DrinkBeer, PassOut` — a guard below then across the
  intoxication threshold, an Internal-Transition override, a deferral to the
  parent, and a cross-level Transition through a Composite ancestor.

## Capability coverage

| v1 capability | Covering v2 test(s) |
|---|---|
| Event deferral to nearest ancestor handler | `cd_player_test.cpp` `CdPlayer.EventUnhandledByLeafDefersToParent`; `nested_test.cpp` `Nested.EventDefersToNearestAncestorHandler`; `example_control_test.cpp` `ExampleControl.EventDefersUpParentChainFromBored`; `v1_parity_test.cpp` `V1Parity.ExampleControlReproducesV1Transcript` (DrinkBeer / PassOut from Drunk) |
| Guards (gate a Transition; false Guard keeps deferring) | `cd_player_test.cpp` `CdPlayer.GuardFalseDefersToParent`, `CdPlayer.GuardTrueTakesTheTransition`; `v1_parity_test.cpp` `V1Parity.ExampleControlReproducesV1Transcript` (Sober's threshold Guard) |
| Actions (run between Exit and Entry) | `cd_player_test.cpp` (asserts the Action log on each Transition); `v1_parity_test.cpp` `V1Parity.CdPlayerReproducesV1Transcript` (Action-bearing Transitions in order) |
| Internal Transitions (Action only, no State change) | `cd_player_test.cpp` `CdPlayer.InternalTransitionRunsActionWithoutStateChange`; `v1_parity_test.cpp` `V1Parity.ExampleControlReproducesV1Transcript` (LookAtWatch while Drunk emits no line) |
| Self Transitions (Exit + re-enter same State) | `cd_player_test.cpp` `CdPlayer.SelfTransitionReentersSameState` |
| Hierarchy (Composite drill to Initial Substate; cross-level Transition) | `cd_player_test.cpp` `CdPlayer.RestsInInitialSubstate`; `nested_test.cpp` `Nested.CompositeTargetDrillsToInitialSubstate`; `example_control_test.cpp` (PassOut cross-level); `v1_parity_test.cpp` both tests |
| External vs Local Transition semantics | `nested_test.cpp` `Nested.ExternalParentChildReentersAncestor`, `Nested.LocalParentChildKeepsAncestor` |
| During / StateUpdate ticks | `during_test.cpp` (`During.*`); `example_control_test.cpp` `ExampleControl.DuringTickMetabolizesWithoutTransition`, `ExampleControl.DuringTickRunsCurrentLeafHookInDrunk` |
| Timers / EventBucket / TimeTracker | `timer_test.cpp`, `time_tracker_test.cpp`, `event_bucket_test.cpp` |
| Auto-logging layer (Transition / Entry / Exit / init, verbosity levels) | `auto_logged_machine_test.cpp` (all); `v1_parity_test.cpp` both tests |
| `isInSubstateOf` ancestry query | `cd_player_test.cpp` `CdPlayer.IsInSubstateOfReportsAncestry`; `nested_test.cpp` `Nested.IsInSubstateOfReportsNestedAncestry`; `example_control_test.cpp` `ExampleControl.IsInSubstateOfReportsDeepAncestry` |

## Parity-driven fix

- **Top-sourced External Transition re-enters Top.** The first draft of this
  parity suite surfaced a divergence: v2 special-cased the root so a `Top`-handled
  External Transition to a descendant did *not* Exit and re-enter `Top`, while v1
  (and v2's own treatment of every other Composite Source) does. Because the root
  has no parent for the LCA to clamp above, this was an inconsistency in v2, not a
  v1 quirk, so v2 was corrected: `take_transition` now Exits and re-enters `Top`
  for a Top-sourced External Transition. cd_player's `Hammer -> Playing` is in the
  golden stream and `cd_player_test.cpp` `CdPlayer.TopHandledTransitionReentersTop`
  pins it at the behavioral level.

## Intentional divergences (not parity gaps)

- **Construction bootstrap.** v1 establishes its initial configuration with an
  external self-Transition on `Top` (`Transition<Top, Top, Top>`), which Exits and
  re-enters the root before drilling to the resting Leaf. v2's constructor drills
  from `Top` without Exiting it. The construction transcript is therefore dropped
  before each parity comparison; parity is asserted on the per-Dispatch transcript.
- **Guard timing.** v1's Sober drink handlers raise the BAC and then test the
  threshold; v2 uses a predictive Guard that tests `bac + drink >= threshold`
  before the Action. The transition decision and the resulting BAC are identical,
  so the transcript is unaffected.

## v2-only behavior (no v1 equivalent)

- cd_player's `VolumeUp` (Internal), `Next` (Self), and the `drawer_jammed` Guard's
  *true* branch on `Hammer` (jammed drawer -> Open) are v2 additions; the Guard's
  *false* branch (deferral to Top) is shared and is in the parity stream. v1's
  cd_player instead had a `stopped_again` Action on `Stop` while `Stopped` (a
  non-transitioning handler), dropped in v2. The v2-only behavior is covered by
  `cd_player_test.cpp`, not by the parity stream.
