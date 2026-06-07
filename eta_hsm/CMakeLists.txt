# ----------------------------------------------------------------------------
# eta_hsm: header-only INTERFACE library. Defining only the targets here (no
# project(), no install, no tests) keeps a downstream add_subdirectory()/
# FetchContent of the repo root to just these interface targets; the developer
# surface lives in the top-level CMakeLists.txt behind PROJECT_IS_TOP_LEVEL.
# ----------------------------------------------------------------------------
add_library(eta_hsm INTERFACE)
add_library(EtaHsm::eta_hsm ALIAS eta_hsm)

# Headers are referenced repo-root-relative, e.g. #include "eta_hsm/reflect/...".
target_include_directories(
    eta_hsm
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_features(eta_hsm INTERFACE cxx_std_26)
# Reflection (P2996) is opt-in on GCC; consumers of the headers need it too.
# Contracts (P2900: pre/post/contract_assert) are likewise behind a flag, and
# -fcontracts is required just to PARSE a header that carries a contract -- so it
# rides the public interface exactly as -freflection does, and is exported to
# downstream consumers for the same reason.
#
# The enforcement SEMANTIC is a separate runtime policy: enforce (a violation
# aborts) in every configuration, including Release. A violation aborts cleanly
# rather than proceeding into undefined behavior, and the check costs nothing that
# matters -- the optimizer eliminates the predicates it can prove and the rest are
# below the noise floor. It is scoped to this build tree so a downstream consumer
# picks its own semantic rather than inheriting ours.
target_compile_options(
    eta_hsm
    INTERFACE
        "$<$<CXX_COMPILER_ID:GNU>:-freflection>"
        "$<$<CXX_COMPILER_ID:GNU>:-fcontracts>"
        "$<BUILD_INTERFACE:$<$<CXX_COMPILER_ID:GNU>:-fcontract-evaluation-semantic=enforce>>"
)
# A checked contract emits a reference to the violation handler that lives in the
# experimental runtime (libstdc++exp). Passing -fcontracts on the link line lets
# the compiler driver pull that library in; without it, any contract-bearing TU
# fails to link with an undefined handle_contract_violation.
target_link_options(eta_hsm INTERFACE "$<$<CXX_COMPILER_ID:GNU>:-fcontracts>")

# ----------------------------------------------------------------------------
# Strict warnings for the targets we compile. This INTERFACE library carries the
# flags so they reach only what links it (tests, tools, the consumer) and never
# ride along on the public eta_hsm headers handed to downstream consumers. It is
# deliberately not part of the install/export set.
# ----------------------------------------------------------------------------
add_library(eta_hsm_warnings INTERFACE)
add_library(EtaHsm::warnings ALIAS eta_hsm_warnings)

set(_eta_warning_flags
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wcast-align
    -Wunused
    -Wformat=2
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wuseless-cast
    -Wsign-promo
    -Werror
)
target_compile_options(
    eta_hsm_warnings
    INTERFACE "$<$<CXX_COMPILER_ID:GNU>:${_eta_warning_flags}>"
)
