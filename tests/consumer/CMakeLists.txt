# A standalone project that consumes eta_hsm via find_package(EtaHsm).
# Intentionally does NOT set the C++ standard or -freflection: both must
# propagate from the imported EtaHsm::eta_hsm INTERFACE target.
cmake_minimum_required(VERSION 3.28)
project(EtaHsmConsumer LANGUAGES CXX)

find_package(EtaHsm REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE EtaHsm::eta_hsm)
