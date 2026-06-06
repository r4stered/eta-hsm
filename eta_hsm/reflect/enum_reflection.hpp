#pragma once

// Public enum reflection utility over C++26 P2996 reflection (std::meta).
// Name/count/value queries on a plain `enum class`:
//
//   eta_hsm::enum_count<E>()                  -> number of enumerators
//   eta_hsm::enum_values<E>()                 -> enumerators, declaration order
//   eta_hsm::enum_name(value)                 -> enumerator identifier, or
//                                                nullopt if `value` is not a
//                                                named enumerator
//   eta_hsm::enum_is_contiguous_from_zero<E>()-> true iff the enumerator values
//                                                are exactly {0,...,N-1}
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
    for (std::meta::info e : std::meta::enumerators_of(^^E))
    {
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
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = table[i].first;
    }
    return values;
}

// True iff `E`'s enumerator values are exactly 0,1,...,N-1 (contiguous,
// zero-based, no gaps, no duplicates), where N == enum_count<E>(). This is the
// precondition for indexing an enum_count<E>()-sized array by the raw enum value
// (static_cast to the underlying integer): only then does every enumerator land
// in [0, N). An enum with no enumerators is trivially contiguous (size-0 array,
// nothing to index).
//
// Correctness: enum_values<E>() has exactly N entries. If every value v
// satisfies 0 <= v < N and all values are distinct, the set of N distinct values
// drawn from [0, N) must be exactly {0,...,N-1}. We check both conditions in a
// single pass: range membership directly, and distinctness via a seen-bitmap
// (safe because each in-range value is a valid index into the bitmap).
template <typename E>
consteval bool enum_is_contiguous_from_zero()
{
    constexpr std::size_t n = enum_count<E>();
    std::array<bool, n> seen{};
    for (E value : enum_values<E>())
    {
        auto const v = static_cast<long long>(value);
        if (v < 0 || static_cast<std::size_t>(v) >= n)
        {
            return false;  // value outside [0, N) -> would index past the array
        }
        if (seen[static_cast<std::size_t>(v)])
        {
            return false;  // duplicate value -> set can't be all of {0,...,N-1}
        }
        seen[static_cast<std::size_t>(v)] = true;
    }
    return true;
}

// Identifier of the enumerator equal to `value`, or nullopt if `value` does not
// name a declared enumerator. `E` is deduced from the argument.
template <typename E>
constexpr std::optional<std::string_view> enum_name(E value)
{
    constexpr auto table = detail::enum_table<E>();
    for (auto const& [enumerator, name] : table)
    {
        if (enumerator == value)
        {
            return name;
        }
    }
    return std::nullopt;
}

}  // namespace eta_hsm
