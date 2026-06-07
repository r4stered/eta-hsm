cmake_minimum_required(VERSION 3.28)

project(EtaHsm VERSION 2.0.0 LANGUAGES CXX)

# Single C++26 baseline (ADR-0001). No extensions, no fallback. Governs the
# developer targets (tests, tool); the library is INTERFACE and carries its own
# cxx_std_26 requirement so a downstream consumer is bumped regardless.
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

# The header-only library lives in eta_hsm/. It defines the EtaHsm::eta_hsm
# INTERFACE target (and the internal EtaHsm::warnings flags) and nothing else, so
# a downstream FetchContent_MakeAvailable(EtaHsm) or add_subdirectory() brings in
# only that target.
add_subdirectory(eta_hsm)

# ----------------------------------------------------------------------------
# Developer-only surface: the install/export rules, the diagram tool, and the
# test suite (with its GoogleTest fetch). Guarded so it fires only when this repo
# is configured as the top-level project -- never when the project is pulled in as
# a subdirectory by a downstream build.
# ----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    include(CMakePackageConfigHelpers)

    # Install + find_package(EtaHsm) export. Ship every public header directory --
    # machine/, log/, diagram/, reflect/, utils/ -- and only those: examples/,
    # reference/, probe/, and tests/ are developer surface and stay out.
    install(TARGETS eta_hsm EXPORT EtaHsmTargets)
    install(
        DIRECTORY
            "${CMAKE_CURRENT_SOURCE_DIR}/eta_hsm/machine"
            "${CMAKE_CURRENT_SOURCE_DIR}/eta_hsm/log"
            "${CMAKE_CURRENT_SOURCE_DIR}/eta_hsm/diagram"
            "${CMAKE_CURRENT_SOURCE_DIR}/eta_hsm/reflect"
            "${CMAKE_CURRENT_SOURCE_DIR}/eta_hsm/utils"
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

    # Standalone diagram tool: include a machine's header, emit its diagram from
    # the constexpr table. cd_player is the demonstrator (its diagram has a golden
    # test).
    add_executable(cd_player_diagram eta_hsm/diagram/cd_player_diagram_main.cpp)
    target_link_libraries(cd_player_diagram PRIVATE eta_hsm eta_hsm_warnings)

    # Tests (GTest only).
    if(BUILD_TESTING)
        add_subdirectory(eta_hsm/tests)
    endif()
endif()
