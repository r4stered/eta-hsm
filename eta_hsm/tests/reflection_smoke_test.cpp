// Baseline smoke test: proves the supported toolchain (GCC 16, -std=c++26
// -freflection) actually compiles and runs P2996 reflection over a plain
// `enum class`. This guards only that the compiler's reflection support is
// wired into both build systems; the public enum utility
// (enum_name/enum_count/enum_values) is tested in enum_reflection_test.cpp.
#include "eta_hsm/reflect/reflection_smoke.hpp"

#include <gtest/gtest.h>

namespace {

enum class Color { Red, Green, Blue };

TEST(ReflectionSmoke, CountsEnumerators) { EXPECT_EQ(eta_hsm::smoke::enumerator_count<Color>(), 3u); }

TEST(ReflectionSmoke, RecoversEnumeratorNames)
{
    EXPECT_EQ(eta_hsm::smoke::enumerator_name<Color>(0), "Red");
    EXPECT_EQ(eta_hsm::smoke::enumerator_name<Color>(2), "Blue");
}

}  // namespace
