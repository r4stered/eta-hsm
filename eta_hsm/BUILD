load("@aspect_rules_py//py:defs.bzl", "py_library")
load("@rules_cc//cc:defs.bzl", "cc_library")

# Header-only HSM library (v2). Headers are referenced repo-root-relative,
# e.g. #include "eta_hsm/reflect/reflection_smoke.hpp".
cc_library(
    name = "eta_hsm",
    hdrs = glob([
        "reflect/*.hpp",
        "machine/*.hpp",
        "log/*.hpp",
        "examples/**/*.hpp",
        "probe/*.hpp",
    ]),
    visibility = ["//visibility:public"],
)

py_library(
    name = "parser",
    srcs = ["//python:parser.py"],
    visibility = ["//visibility:public"],
)
