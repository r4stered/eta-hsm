#pragma once

// Public enum reflection utility over C++26 P2996 reflection (std::meta).
// Replaces wise_enum for name/count/value queries on a plain `enum class`:
//
//   eta_hsm::enum_count<E>()   -> number of enumerators
//   eta_hsm::enum_name(value)  -> enumerator identifier, or nullopt if `value`
//                                 is not a named enumerator
//
// Sentinel enumerators (e.g. eNone/eTop) are reported like any other.

#include <array>
#include <cstddef>
#include <meta>
#include <optional>
#include <string_view>
#include <utility>

namespace eta_hsm {

// Number of enumerators declared in enum `E`.
template <typename E>
consteval std::size_t enum_count()
{
    return std::meta::enumerators_of(^^E).size();
}

namespace detail {

// A compile-time (value, identifier) table for every enumerator of `E`, in
// declaration order. Built inside a consteval function so the std::meta vector
// is consumed during constant evaluation, leaving a plain array of values.
template <typename E>
consteval std::array<std::pair<E, std::string_view>, enum_count<E>()> enum_table()
{
    std::array<std::pair<E, std::string_view>, enum_count<E>()> table{};
    std::size_t i = 0;
    for (std::meta::info e : std::meta::enumerators_of(^^E)) {
        table[i].first = std::meta::extract<E>(e);
        table[i].second = std::meta::identifier_of(e);
        ++i;
    }
    return table;
}

}  // namespace detail

// The enumerators of `E`, in declaration order. Usable as a constexpr value,
// e.g. to size arrays or iterate every enumerator.
template <typename E>
consteval std::array<E, enum_count<E>()> enum_values()
{
    std::array<E, enum_count<E>()> values{};
    auto const table = detail::enum_table<E>();
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = table[i].first;
    }
    return values;
}

// Identifier of the enumerator equal to `value`, or nullopt if `value` does not
// name a declared enumerator. `E` is deduced from the argument.
template <typename E>
constexpr std::optional<std::string_view> enum_name(E value)
{
    constexpr auto table = detail::enum_table<E>();
    for (auto const& [enumerator, name] : table) {
        if (enumerator == value) {
            return name;
        }
    }
    return std::nullopt;
}

}  // namespace eta_hsm
