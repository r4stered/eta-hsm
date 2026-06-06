include(FetchContent)

# GTest is the only test framework (the parallel catch_tests are retired).
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) # keep GTest out of EtaHsm's install
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(reflection_smoke_test reflection_smoke_test.cpp)
target_link_libraries(
    reflection_smoke_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(reflection_smoke_test)

add_executable(enum_reflection_test enum_reflection_test.cpp)
target_link_libraries(
    enum_reflection_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(enum_reflection_test)

add_executable(cd_player_test cd_player_test.cpp)
target_link_libraries(cd_player_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(cd_player_test)

add_executable(nested_test nested_test.cpp)
target_link_libraries(nested_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(nested_test)

add_executable(diagram_test diagram_test.cpp)
target_link_libraries(diagram_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
# The repo root lets the README-sync test find README.md (it is skipped when this
# is undefined, e.g. under Bazel).
target_compile_definitions(
    diagram_test
    PRIVATE ETA_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
gtest_discover_tests(diagram_test)

add_executable(during_test during_test.cpp)
target_link_libraries(during_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(during_test)

add_executable(auto_logged_machine_test auto_logged_machine_test.cpp)
target_link_libraries(
    auto_logged_machine_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(auto_logged_machine_test)

add_executable(event_bucket_test event_bucket_test.cpp)
target_link_libraries(
    event_bucket_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(event_bucket_test)

add_executable(timer_test timer_test.cpp)
target_link_libraries(timer_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(timer_test)

add_executable(time_tracker_test time_tracker_test.cpp)
target_link_libraries(
    time_tracker_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(time_tracker_test)

add_executable(validator_test validator_test.cpp)
target_link_libraries(validator_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(validator_test)

add_executable(builder_deduction_test builder_deduction_test.cpp)
target_link_libraries(
    builder_deduction_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(builder_deduction_test)

add_executable(scaling_probe_test scaling_probe_test.cpp)
target_link_libraries(
    scaling_probe_test
    PRIVATE EtaHsm::eta_hsm GTest::gtest_main
)
gtest_discover_tests(scaling_probe_test)

add_executable(footprint_test footprint_test.cpp)
target_link_libraries(footprint_test PRIVATE EtaHsm::eta_hsm GTest::gtest_main)
gtest_discover_tests(footprint_test)

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
