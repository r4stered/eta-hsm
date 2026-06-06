cmake_minimum_required(VERSION 3.28)

project(EtaHsm VERSION 2.0.0 LANGUAGES CXX)

# Single C++26 baseline (ADR-0001). No extensions, no fallback.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(
        WARNING
        "eta_hsm is supported only on GCC 16+ (P2996 reflection). "
        "Configuring with '${CMAKE_CXX_COMPILER_ID}' is unsupported."
    )
endif()

# ----------------------------------------------------------------------------
# eta_hsm: header-only INTERFACE library.
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

# ----------------------------------------------------------------------------
# Install + find_package(EtaHsm) export.
# ----------------------------------------------------------------------------
include(CTest)
include(CMakePackageConfigHelpers)

install(TARGETS eta_hsm EXPORT EtaHsmTargets)
install(
    DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/reflect"
        "${CMAKE_CURRENT_SOURCE_DIR}/utils"
    DESTINATION include/eta_hsm
    FILES_MATCHING
    PATTERN "*.hpp"
)

set(ConfigPackageLocation lib/cmake/EtaHsm)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/EtaHsmConfigVersion.cmake"
    VERSION ${EtaHsm_VERSION}
    COMPATIBILITY AnyNewerVersion
)
configure_file(
    cmake/EtaHsmConfig.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/EtaHsmConfig.cmake"
    COPYONLY
)

# Export for find_package() from both the build tree and an install tree.
export(
    EXPORT EtaHsmTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/EtaHsmTargets.cmake"
    NAMESPACE EtaHsm::
)
install(
    EXPORT EtaHsmTargets
    FILE EtaHsmTargets.cmake
    NAMESPACE EtaHsm::
    DESTINATION ${ConfigPackageLocation}
)
install(
    FILES
        cmake/EtaHsmConfig.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/EtaHsmConfigVersion.cmake"
    DESTINATION ${ConfigPackageLocation}
    COMPONENT Devel
)

# ----------------------------------------------------------------------------
# Standalone diagram tool: include a machine's header, emit its diagram from the
# constexpr table. cd_player is the demonstrator (its diagram has a golden test).
# ----------------------------------------------------------------------------
add_executable(cd_player_diagram diagram/cd_player_diagram_main.cpp)
target_link_libraries(cd_player_diagram PRIVATE eta_hsm eta_hsm_warnings)

# ----------------------------------------------------------------------------
# Tests (GTest only).
# ----------------------------------------------------------------------------
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
