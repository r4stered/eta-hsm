# eta-hsm

A C++ library for defining and running **hierarchical state machines** (HSMs).

> **v2 is a clean break.** eta_hsm v2 is a header-only library built on
> **C++26** reflection (P2996). It targets **GCC 16+** and drops the external
> `wise_enum` dependency. The v1 CRTP API is not part of v2 — see
> [Using v1](#using-v1) below to keep building the previous release.

## Toolchain

The single supported toolchain is **GCC 16+** at `-std=c++26 -freflection`
(the first GCC line shipping P2996 reflection).

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

## Linting & formatting

CI runs three lint gates — `python_linting`, `cpp_linting`, `cmake_format`
(`.github/workflows/linux.yml`); buildifier runs as a local pre-commit hook.
Reproduce them with the **same tools the pipeline pins** — these are
formatters/linters, not the compiler, so they run on the host, *not* in the
toolchain image.

### Run all three gates

```bash
./tools/lint.sh
```

This runs `cmake_format`, `cpp_linting`, and `python_linting` with the exact
versions CI pins. On the first run it builds a cached, git-ignored venv
(`tools/.lintenv`) and reuses it afterward, so repeat runs do no install work; it
rebuilds only when the pins in the script change. A non-zero exit (with a diff)
means that gate would fail CI.

This does not cover the Bazel `buildifier` hook (pre-commit only, not a CI gate);
run `pre-commit run --all-files` if you've touched `BUILD`/`*.bazel` files.

### Individual gates

**CMake + Bazel formatting** — via pre-commit, which pins the exact versions CI
uses (`gersemi==0.27.7`, `buildifier 7.3.1.2`):

```bash
pip install pre-commit
pre-commit run --all-files          # gersemi (cmake_format job) + buildifier
```

Or run gersemi directly, exactly as the `cmake_format` job does:

```bash
gersemi --check eta_hsm tests/consumer
```

**C++ formatting** (`cpp_linting` job) — clang-format **21** (older versions
mangle the C++26 reflection `^^` syntax):

```bash
# macOS: brew install clang-format   (or: pip install 'clang-format==21.*')
clang-format --version              # must report 21.x
find eta_hsm \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 clang-format --dry-run --Werror
```

**Python** (`python_linting` job):

```bash
pip install flake8 pep8-naming
flake8 python --count --max-complexity=25 --max-line-length=127 --statistics
```

To **auto-fix** rather than check: `pre-commit run --all-files` rewrites the
CMake/Bazel files in place, and `clang-format -i <files>` rewrites C++.

## Using v1

v1 (the C++17, `wise_enum`-based CRTP API) remains available from its tags.
Pin the latest v1 release:

```
# CMake (FetchContent) / Bazel (git_repository): use tag v1.1.2
git checkout v1.1.2
```

v1 consumers keep building against `v1.1.2` until they migrate to the v2 table
API. v1 is not maintained on `main`.
