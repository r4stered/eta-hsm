include(FetchContent)

# GTest is the only test framework. Prefer a system package; otherwise fetch it.
# SYSTEM keeps its headers out of the strict-warning net our targets compile under.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    FIND_PACKAGE_ARGS NAMES GTest
    SYSTEM
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) # keep GTest out of EtaHsm's install
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# A GTest executable built under the strict eta_hsm warning set (eta_hsm_warnings),
# from a single <name>_test.cpp, registered for ctest discovery.
function(eta_add_test name)
    add_executable(${name}_test ${name}_test.cpp)
    target_link_libraries(
        ${name}_test
        PRIVATE EtaHsm::eta_hsm eta_hsm_warnings GTest::gtest_main
    )
    gtest_discover_tests(${name}_test)
endfunction()

eta_add_test(reflection_smoke)
eta_add_test(enum_reflection)
eta_add_test(cd_player)
eta_add_test(nested)
eta_add_test(reference_interpreter)
eta_add_test(backbone)
eta_add_test(compile_time_backbone)
# This TU's correctness payload is entirely `static_assert`s: it runs production
# dispatch and the reference interpreter inside constant evaluation, with only a
# trivial runtime EXPECT echoing the proven counts. Under -fsanitize the compiler
# instruments the dispatch path's pointer-to-member null checks (`action ==
# nullptr`, `guard == nullptr`), and an instrumented pointer-to-member comparison
# is not a constant expression -- so the consteval differential fails to compile
# under the sanitizer build. There is no runtime here for a sanitizer to find UB
# in (the runtime dispatch path is covered under sanitizers by backbone_test /
# random_walk_test / cd_player_test), so exclude just this TU from instrumentation.
# -fno-sanitize=all is a no-op in the non-sanitizer configs, where no -fsanitize is
# set; in the sanitizer config it follows the global flag and wins.
target_compile_options(compile_time_backbone_test PRIVATE -fno-sanitize=all)
eta_add_test(random_walk)
eta_add_test(example_control)
eta_add_test(during)
eta_add_test(auto_logged_machine)
eta_add_test(event_bucket)
eta_add_test(timer)
eta_add_test(time_tracker)
eta_add_test(validator)
eta_add_test(builder_deduction)
eta_add_test(scaling_probe)
eta_add_test(footprint)
eta_add_test(v1_parity)

eta_add_test(diagram)
# The repo root lets the README-sync test find README.md (it is skipped when this
# is undefined, e.g. under Bazel).
target_compile_definitions(
    diagram_test
    PRIVATE ETA_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

# ----------------------------------------------------------------------------
# Build-failure harness for the compile-time validator. Each case
# compiles a deliberately ill-formed machine (validator_negcompile.cpp, one CASE
# per compile) and passes only if the build fails with a diagnostic naming the
# offending element. These are pure ctest cases with no build target: the script
# invokes the compiler itself, so they run wherever g++-16 lives (the project's
# docker toolchain). This proves the half a runtime test cannot -- that an
# ill-formed Machine really fails to compile, with a legible message.
# ----------------------------------------------------------------------------
set(_eta_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(_eta_negcompile "${_eta_repo_root}/tools/expect_compile_fail.sh")

function(eta_add_negcompile_test name case pattern)
    add_test(
        NAME ${name}
        COMMAND bash "${_eta_negcompile}" "${case}" "${pattern}"
        WORKING_DIRECTORY "${_eta_repo_root}"
    )
endfunction()

# Same harness, but against an arbitrary negcompile TU + case macro (the script
# takes them as optional 3rd/4th args). Used for the run_hook hook-detection
# contract; the validator cases above keep the two-argument default form.
function(
    eta_add_negcompile_test_src
    name
    case
    pattern
    src
    macro
)
    add_test(
        NAME ${name}
        COMMAND
            bash "${_eta_negcompile}" "${case}" "${pattern}" "${src}" "${macro}"
        WORKING_DIRECTORY "${_eta_repo_root}"
    )
endfunction()

eta_add_negcompile_test(validator_negcompile_no_top 1 "no Top State declared")
eta_add_negcompile_test(
    validator_negcompile_missing_parent
    2
    "State B has undeclared parent C"
)
eta_add_negcompile_test(
    validator_negcompile_initial_child
    3
    "not one of its children"
)
eta_add_negcompile_test(
    validator_negcompile_bad_target
    4
    "targets undeclared State C"
)
eta_add_negcompile_test(
    validator_negcompile_unwired
    5
    "enumerator B is not wired into the tree"
)
eta_add_negcompile_test(
    validator_negcompile_duplicate
    6
    "without distinct Guards"
)
eta_add_negcompile_test(
    validator_negcompile_too_many_states
    7
    "too many States"
)
eta_add_negcompile_test(
    validator_negcompile_too_many_transitions
    8
    "too many Transitions"
)
eta_add_negcompile_test(
    validator_negcompile_too_many_unwired
    9
    "too many .unwired States"
)

# run_hook hook-detection contract: a fixed-arity entry/exit/during hook with the
# wrong arity must hard-error (not silently no-op). entry_Running(int) is called
# with an empty pack, so the spliced call has no matching function.
eta_add_negcompile_test_src(
    run_hook_negcompile_misshapen_entry
    1
    "no match.*call|no matching function|entry_Running"
    "eta_hsm/tests/run_hook_negcompile.cpp"
    "RUNHOOK_CASE"
)

# Builder enum-deduction contract: once Hsm<Host>{} deduces an enum slot, a later
# call naming a second, different enum in that slot must be a clear compile error
# (no silent re-deduction or coercion). The diagnostic names the foreign enum.
eta_add_negcompile_test_src(
    builder_negcompile_mismatched_state
    1
    "cannot convert.*OtherState|OtherState"
    "eta_hsm/tests/builder_negcompile.cpp"
    "BUILDER_CASE"
)
eta_add_negcompile_test_src(
    builder_negcompile_mismatched_event
    2
    "cannot convert.*OtherEvent|OtherEvent"
    "eta_hsm/tests/builder_negcompile.cpp"
    "BUILDER_CASE"
)

# Template-parameter concepts: a Logger missing log() and an Observer with a
# mistyped notify method must each fail at the instantiation site with the concept
# named, not a deep error inside the observer or a silent no-op.
eta_add_negcompile_test_src(
    concepts_negcompile_bad_logger
    1
    "constraint failure.*Logger|satisfaction of 'Logger"
    "eta_hsm/tests/concepts_negcompile.cpp"
    "CONCEPTS_CASE"
)
eta_add_negcompile_test_src(
    concepts_negcompile_bad_observer
    2
    "constraint failure.*Observer|satisfaction of 'Observer"
    "eta_hsm/tests/concepts_negcompile.cpp"
    "CONCEPTS_CASE"
)

# Compile-time backbone: a planted divergence must fail the BUILD. The
# differential is run with a reference Fault injected, so production and the
# reference disagree on the first reachable matched pair (Stopped + Play); the
# CtReport static_assert then fires with a message naming that (State, Event).
# The "Stopped.Play" in each pattern is that first pair: cd_player's initial Leaf
# is Stopped and Play is its first State-enum Event, so it is the first pair the
# reachability BFS visits -- update these if either ordering changes.
eta_add_negcompile_test_src(
    compile_time_backbone_negcompile_wrong_leaf
    1
    "resting Leaf mismatch.*Stopped.Play"
    "eta_hsm/tests/compile_time_backbone_negcompile.cpp"
    "CTBACKBONE_CASE"
)
eta_add_negcompile_test_src(
    compile_time_backbone_negcompile_dropped_exit
    2
    "Exit sequence mismatch.*Stopped.Play"
    "eta_hsm/tests/compile_time_backbone_negcompile.cpp"
    "CTBACKBONE_CASE"
)

# Contiguous-from-zero enum contract: StaticTimerBank / TimeTracker
# index per-enumerator arrays by the raw enum value, so a gapped / above-zero
# enum must fail to compile (static_assert) rather than throw .at() at runtime.
eta_add_negcompile_test_src(
    utils_contiguous_negcompile_time_tracker
    1
    "contiguous from 0"
    "eta_hsm/tests/utils_contiguous_negcompile.cpp"
    "CONTIG_CASE"
)
eta_add_negcompile_test_src(
    utils_contiguous_negcompile_static_timer_bank
    2
    "contiguous from 0"
    "eta_hsm/tests/utils_contiguous_negcompile.cpp"
    "CONTIG_CASE"
)
