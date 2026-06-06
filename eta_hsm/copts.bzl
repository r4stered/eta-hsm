"""Strict warning flags for first-party eta_hsm targets.

These flags are applied per-target via the eta_cc_test / eta_cc_binary macros so
they reach only code we own. They are deliberately NOT set in .bazelrc: a global
--cxxopt would also apply -Werror to the vendored googletest build, which does not
compile clean under this set.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_test")

WARNING_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wshadow",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wnull-dereference",
    "-Wdouble-promotion",
    "-Wimplicit-fallthrough",
    "-Wcast-align",
    "-Wunused",
    "-Wformat=2",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wold-style-cast",
    "-Wuseless-cast",
    "-Wsign-promo",
    "-Werror",
]

def eta_cc_test(name, copts = [], **kwargs):
    """A cc_test compiled under the strict eta_hsm warning set."""
    cc_test(
        name = name,
        copts = WARNING_COPTS + copts,
        **kwargs
    )

def eta_cc_binary(name, copts = [], **kwargs):
    """A cc_binary compiled under the strict eta_hsm warning set."""
    cc_binary(
        name = name,
        copts = WARNING_COPTS + copts,
        **kwargs
    )
