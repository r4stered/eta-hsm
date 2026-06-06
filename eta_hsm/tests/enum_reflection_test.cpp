// Behavioral tests for the public enum reflection utility
// (eta_hsm::enum_name / enum_count / enum_values), built over P2996 std::meta.
// Tests exercise only the public interface and observable behavior: counts,
// name round-trips, the enumerator sequence, sentinels, a non-int underlying
// type, and compile-time usability.
#include "eta_hsm/reflect/enum_reflection.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace {

enum class Color { Red, Green, Blue };

// Mirrors a large machine's enum shape: leading/trailing sentinels around real states.
enum class State { eNone, Stopped, Playing, eTop };

// Non-default underlying type with explicit, non-contiguous values, exercising
// the value-matching path beyond the implicit 0,1,2,... case.
enum class Register : std::uint16_t { Status = 0x10, Control = 0x20, Data = 0xFFFF };

// Enums exercising enum_is_contiguous_from_zero. A clean 0-based enum is the
// only shape safe to index per-enumerator arrays by the raw enum value.
enum class Contiguous { A, B, C };  // 0,1,2  -> contiguous
enum class StartsAboveZero { A = 1, B = 2, C = 3 };  // 1,2,3  -> NOT
enum class Gapped { A = 0, B = 1, C = 3 };  // gap at 2 -> NOT
enum class ExplicitOutOfRange { A = 0, B = 5, C = 1 };  // 5 >= count -> NOT
// Non-int underlying type but still contiguous from 0.
enum class ContiguousByte : std::uint8_t { A, B, C, D };  // 0,1,2,3 -> contiguous
enum class Empty {};  // no enumerators -> trivially true

TEST(EnumReflection, CountsEnumeratorsOfPlainEnumClass) { EXPECT_EQ(eta_hsm::enum_count<Color>(), 3u); }

TEST(EnumReflection, NameRoundTripsKnownEnumerator)
{
    EXPECT_EQ(eta_hsm::enum_name(Color::Red), "Red");
    EXPECT_EQ(eta_hsm::enum_name(Color::Blue), "Blue");
}

TEST(EnumReflection, NameOfUnnamedValueIsNullopt)
{
    EXPECT_EQ(eta_hsm::enum_name(static_cast<Color>(99)), std::nullopt);
}

TEST(EnumReflection, ValuesYieldsEnumeratorsInDeclarationOrder)
{
    constexpr auto values = eta_hsm::enum_values<Color>();
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], Color::Red);
    EXPECT_EQ(values[1], Color::Green);
    EXPECT_EQ(values[2], Color::Blue);
}

TEST(EnumReflection, SentinelsAreCountedAndNamedLikeAnyOther)
{
    // Sentinels participate in the count exactly like real states — this is what
    // keeps enum_count<E>()-sized arrays matching wise_enum's at the call sites.
    EXPECT_EQ(eta_hsm::enum_count<State>(), 4u);
    EXPECT_EQ(eta_hsm::enum_name(State::eNone), "eNone");
    EXPECT_EQ(eta_hsm::enum_name(State::eTop), "eTop");

    constexpr auto values = eta_hsm::enum_values<State>();
    EXPECT_EQ(values.front(), State::eNone);
    EXPECT_EQ(values.back(), State::eTop);
}

TEST(EnumReflection, WorksWithNonIntUnderlyingType)
{
    EXPECT_EQ(eta_hsm::enum_count<Register>(), 3u);
    EXPECT_EQ(eta_hsm::enum_name(Register::Status), "Status");
    EXPECT_EQ(eta_hsm::enum_name(Register::Data), "Data");
    EXPECT_EQ(eta_hsm::enum_name(static_cast<Register>(0x99)), std::nullopt);

    constexpr auto values = eta_hsm::enum_values<Register>();
    EXPECT_EQ(values[1], Register::Control);
}

TEST(EnumReflection, ContiguousFromZeroAcceptsCleanZeroBasedEnum)
{
    EXPECT_TRUE(eta_hsm::enum_is_contiguous_from_zero<Contiguous>());
    EXPECT_TRUE(eta_hsm::enum_is_contiguous_from_zero<ContiguousByte>());
    // An enum with no enumerators is trivially contiguous (nothing to index).
    EXPECT_TRUE(eta_hsm::enum_is_contiguous_from_zero<Empty>());
}

TEST(EnumReflection, ContiguousFromZeroRejectsNonContiguousEnums)
{
    EXPECT_FALSE(eta_hsm::enum_is_contiguous_from_zero<StartsAboveZero>());
    EXPECT_FALSE(eta_hsm::enum_is_contiguous_from_zero<Gapped>());
    EXPECT_FALSE(eta_hsm::enum_is_contiguous_from_zero<ExplicitOutOfRange>());
    EXPECT_FALSE(eta_hsm::enum_is_contiguous_from_zero<Register>());
}

// enum_is_contiguous_from_zero must be a compile-time predicate too: these are
// the exact form the StaticTimerBank / TimeTracker static_asserts rely on.
static_assert(eta_hsm::enum_is_contiguous_from_zero<Contiguous>());
static_assert(eta_hsm::enum_is_contiguous_from_zero<ContiguousByte>());
static_assert(eta_hsm::enum_is_contiguous_from_zero<Empty>());
static_assert(!eta_hsm::enum_is_contiguous_from_zero<StartsAboveZero>());
static_assert(!eta_hsm::enum_is_contiguous_from_zero<Gapped>());
static_assert(!eta_hsm::enum_is_contiguous_from_zero<ExplicitOutOfRange>());

// All three helpers must be usable in constant expressions. These checks run at
// compile time; if any helper were not constexpr/consteval, this file would
// fail to compile rather than fail at runtime.
static_assert(eta_hsm::enum_count<Color>() == 3u);
static_assert(eta_hsm::enum_values<Color>()[1] == Color::Green);
static_assert(eta_hsm::enum_name(Color::Red) == "Red");
static_assert(eta_hsm::enum_name(static_cast<Color>(99)) == std::nullopt);

TEST(EnumReflection, UsableInConstantExpressions)
{
    // Mirror the file-scope static_asserts in a local constant expression, so a
    // regression in constexpr-ness surfaces as a clear compile error here too.
    constexpr bool ok = eta_hsm::enum_count<State>() == 4u && eta_hsm::enum_name(State::Stopped) == "Stopped";
    static_assert(ok);
    SUCCEED();
}

}  // namespace
