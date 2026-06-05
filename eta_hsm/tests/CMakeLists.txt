include(FetchContent)

# GTest is the only test framework (the parallel catch_tests are retired).
find_package(GTest QUIET)
if (NOT GTest_FOUND)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)  # keep GTest out of EtaHsm's install
    FetchContent_MakeAvailable(googletest)
endif ()

include(GoogleTest)

add_executable(reflection_smoke_test reflection_smoke_test.cpp)
target_link_libraries(reflection_smoke_test
    PRIVATE
        EtaHsm::eta_hsm
        GTest::gtest_main
)
gtest_discover_tests(reflection_smoke_test)
