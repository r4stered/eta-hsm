load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

# Header-only HSM library. Headers are referenced repo-root-relative,
# e.g. #include "eta_hsm/reflect/reflection_smoke.hpp".
cc_library(
    name = "eta_hsm",
    hdrs = glob([
        "reflect/*.hpp",
        "machine/*.hpp",
        "reference/*.hpp",
        "log/*.hpp",
        "utils/*.hpp",
        "examples/**/*.hpp",
        "probe/*.hpp",
        "diagram/*.hpp",
    ]),
    visibility = ["//visibility:public"],
)

# Standalone diagram tool: include cd_player's header and emit its diagram from
# the constexpr table (the single source of truth). The replacement for the old
# regex-based Python diagram scraper.
cc_binary(
    name = "cd_player_diagram",
    srcs = ["diagram/cd_player_diagram_main.cpp"],
    deps = [":eta_hsm"],
)
