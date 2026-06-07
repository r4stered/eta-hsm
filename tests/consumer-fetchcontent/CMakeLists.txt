# A standalone project that consumes eta_hsm via FetchContent with no
# SOURCE_SUBDIR workaround -- the default FetchContent_MakeAvailable path a
# downstream user would write. A local SOURCE_DIR keeps the check hermetic (no
# network); it still exercises the load-bearing part: MakeAvailable add_directory's
# the fetched root, which must carry a CMakeLists.txt that exposes the interface
# target. Like the find_package consumer, it sets neither the C++ standard nor the
# reflection flags: both must propagate from the imported EtaHsm::eta_hsm target.
cmake_minimum_required(VERSION 3.28)
project(EtaHsmFetchContentConsumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(EtaHsm SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
FetchContent_MakeAvailable(EtaHsm)

# Consuming eta_hsm as a subdirectory must bring in only the interface target --
# no GoogleTest, no eta_hsm test executables, no cd_player_diagram tool. Assert
# that here so a regression in the PROJECT_IS_TOP_LEVEL guarding fails this
# configure rather than silently bloating a downstream build.
foreach(
    _dev_target
    IN
    ITEMS cd_player_diagram gtest gtest_main gmock gmock_main
)
    if(TARGET ${_dev_target})
        message(
            FATAL_ERROR
            "FetchContent pulled developer-only target '${_dev_target}'; the "
            "eta_hsm dev surface is not guarded behind PROJECT_IS_TOP_LEVEL."
        )
    endif()
endforeach()

# Build the same real-Machine<> consumer translation unit the find_package case
# uses, so both consumption paths prove the machine/ headers are reachable.
add_executable(consumer ../consumer/main.cpp)
target_link_libraries(consumer PRIVATE EtaHsm::eta_hsm)

include(${CMAKE_CURRENT_SOURCE_DIR}/../StrictConsumerWarnings.cmake)
eta_hsm_strict_warnings(consumer)
