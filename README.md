# eta-hsm

A C++ library for defining and running **hierarchical state machines** (HSMs).

> **v2 is a clean break.** eta_hsm v2 is a header-only library built on
> **C++26** reflection (P2996). It targets **GCC 16+** and drops the external
> `wise_enum` dependency. The v1 CRTP API is not part of v2 — see
> [Using v1](#using-v1) below to keep building the previous release.

## Toolchain

The single supported toolchain is **GCC 16+** at `-std=c++26 -freflection`
(the first GCC line shipping P2996 reflection). See
[ADR-0001](docs/adr/0001-cpp26-single-baseline.md).

A Docker image pins this toolchain for local builds:

```bash
./docker_build                                    # build the image
./docker_build cmake -S eta_hsm -B build -G Ninja # configure
./docker_build cmake --build build                # build
./docker_build ctest --test-dir build --output-on-failure
./docker_build bazel test //...                   # or via Bazel
```

## Build (CMake)

```bash
cmake -S eta_hsm -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

eta_hsm installs as a header-only package with a `find_package` export:

```bash
cmake --install build --prefix /your/prefix
```

```cmake
find_package(EtaHsm REQUIRED)
target_link_libraries(your_target PRIVATE EtaHsm::eta_hsm)
```

The imported target propagates C++26 and `-freflection`, so consumers don't
set them by hand. A minimal consumer lives in [`tests/consumer`](tests/consumer).

## Enum reflection

[`eta_hsm/reflect/enum_reflection.hpp`](eta_hsm/reflect/enum_reflection.hpp)
gives reflection over any plain `enum class` via `std::meta`, replacing
`wise_enum` for name/count/value queries:

```cpp
#include "eta_hsm/reflect/enum_reflection.hpp"

enum class Color { Red, Green, Blue };

eta_hsm::enum_count<Color>();        // 3
eta_hsm::enum_values<Color>();       // std::array<Color, 3>, declaration order
eta_hsm::enum_name(Color::Red);      // std::optional{"Red"}
eta_hsm::enum_name((Color)99);       // std::nullopt
```

All three are usable in `constexpr`/`consteval` contexts; sentinel enumerators
(e.g. `eNone`/`eTop`) are reported like any other.

## Build (Bazel)

```bash
bazel test //...
```

`bazel build //eta_hsm:eta_hsm` gives the header-only library target.

## Testing

Tests use **GoogleTest** only. The reflection smoke test
([`eta_hsm/tests/reflection_smoke_test.cpp`](eta_hsm/tests/reflection_smoke_test.cpp))
proves the toolchain compiles and runs P2996 reflection under both build
systems, and
[`enum_reflection_test.cpp`](eta_hsm/tests/enum_reflection_test.cpp)
covers the public enum reflection utility (names, counts, values, sentinels,
non-`int` underlying types, and compile-time use).

## Using v1

v1 (the C++17, `wise_enum`-based CRTP API) remains available from its tags.
Pin the latest v1 release:

```
# CMake (FetchContent) / Bazel (git_repository): use tag v1.1.2
git checkout v1.1.2
```

v1 consumers keep building against `v1.1.2` until they migrate to the v2 table
API. v1 is not maintained on `main`.
