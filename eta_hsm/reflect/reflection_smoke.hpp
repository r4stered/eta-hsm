#pragma once

// Minimal P2996 reflection helpers used only by the build-baseline smoke test.
// These exercise `std::meta` so the supported toolchain (GCC 16, -std=c++26
// -freflection) is proven through both build systems. They are intentionally
// trivial and will be superseded by the public enum reflection utility
// (eta_hsm::enum_name / enum_count / enum_values) in a later slice.

#include <cstddef>
#include <meta>
#include <string_view>

namespace eta_hsm::smoke {

// Number of enumerators declared in enum `E`.
template <typename E>
consteval std::size_t enumerator_count()
{
    return std::meta::enumerators_of(^^E).size();
}

// Identifier of the `index`-th enumerator of enum `E`.
template <typename E>
consteval std::string_view enumerator_name(std::size_t index)
{
    return std::meta::identifier_of(std::meta::enumerators_of(^^E)[index]);
}

}  // namespace eta_hsm::smoke
