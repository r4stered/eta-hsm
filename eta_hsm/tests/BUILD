load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "reflection_smoke_test",
    srcs = ["reflection_smoke_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "enum_reflection_test",
    srcs = ["enum_reflection_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "cd_player_test",
    srcs = ["cd_player_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "nested_test",
    srcs = ["nested_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "reference_interpreter_test",
    srcs = ["reference_interpreter_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "example_control_test",
    srcs = ["example_control_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "diagram_test",
    srcs = [
        "diagram_test.cpp",
        # Goldens are #included as raw-string literals, so the golden match needs
        # no file IO and works identically under CMake and Bazel.
        "golden/cd_player.mmd.inc",
        "golden/cd_player.puml.inc",
    ],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "during_test",
    srcs = ["during_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "auto_logged_machine_test",
    srcs = ["auto_logged_machine_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "event_bucket_test",
    srcs = ["event_bucket_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "timer_test",
    srcs = ["timer_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "time_tracker_test",
    srcs = ["time_tracker_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "validator_test",
    srcs = ["validator_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builder_deduction_test",
    srcs = ["builder_deduction_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "scaling_probe_test",
    srcs = ["scaling_probe_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "footprint_test",
    srcs = ["footprint_test.cpp"],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "v1_parity_test",
    srcs = [
        "v1_parity_test.cpp",
        # v1's frozen auto-log transcripts, #included as raw-string literals, so
        # the golden match needs no file IO and works under CMake and Bazel alike.
        "golden/cd_player_transcript.inc",
        "golden/example_control_transcript.inc",
    ],
    deps = [
        "//eta_hsm:eta_hsm",
        "@googletest//:gtest_main",
    ],
)
