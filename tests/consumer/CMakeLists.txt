# A standalone project that consumes eta_hsm via find_package(EtaHsm).
# Intentionally does NOT set the C++ standard or -freflection: both must
# propagate from the imported EtaHsm::eta_hsm INTERFACE target.
cmake_minimum_required(VERSION 3.28)
project(EtaHsmConsumer LANGUAGES CXX)

find_package(EtaHsm REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE EtaHsm::eta_hsm)

# Hold our own consuming code to the same strict warning set as the rest of the
# tree. The eta_hsm_warnings target is internal to the main build (not exported),
# so the flags are applied directly here.
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
    consumer
    PRIVATE "$<$<CXX_COMPILER_ID:GNU>:${_eta_warning_flags}>"
)
