# GoogleTest

This directory contains a reduced copy of [GoogleTest][gtest] vendored into CODES
as the unit-test framework.

    Upstream:  https://github.com/google/googletest
    Version:   v1.17.0  (commit 52eb8108, 2025-04-30)

**Do not make changes here directly.** The upstream drop in `googletest/` is
imported from the upstream repository; make changes upstream, then re-import (see
below). The wrapper files in *this* directory (`CMakeLists.txt`, this README,
`update.sh`) are CODES-maintained and are preserved across re-imports.

## What is vendored

`googletest/` holds a **reduced copy** of the upstream repository: the repo-root
`CMakeLists.txt` (plus `LICENSE`, `README`, `CONTRIBUTORS`) and the `googletest/`
library subdirectory (`CMakeLists.txt`, `cmake/`, `include/`, `src/`). We drop
`googlemock/`, the library's own `test/` and `samples/`, and the docs, CI, and
bazel files — none are needed to build the framework. GoogleMock is not vendored;
`BUILD_GMOCK=OFF` (below) keeps upstream's root CMake building only `googletest/`.
Death tests — a main reason CODES chose GoogleTest over Catch2 — live in gtest
core, so dropping gmock loses nothing we need.

We vendor the source (rather than `find_package`/`FetchContent`) so the build needs
no network access and no submodules, matching the ryml vendoring alongside it and
the Kitware convention used across ADIOS2, VTK, and CMake.

## How CODES builds it

Unlike ryml (core, always built), GoogleTest is **test-only**: it is built only
when `BUILD_TESTING` is on and is wired from `tests/CMakeLists.txt`, not the
always-built `thirdparty/CMakeLists.txt` dispatcher (`BUILD_TESTING` is not yet
defined at the point that dispatcher runs — `add_subdirectory(thirdparty)` precedes
`include(CTest)` in the top-level `CMakeLists.txt`). It is never installed or
exported.

`CMakeLists.txt` here is a thin wrapper that sets `BUILD_GMOCK=OFF`,
`INSTALL_GTEST=OFF`, and `gtest_force_shared_crt=ON`, then `add_subdirectory`s the
vendored upstream tree with `EXCLUDE_FROM_ALL` — the standard integration, the same
approach ADIOS2 uses (rather than hand-compiling `gtest-all.cc`). Upstream exposes
the `GTest::gtest` / `GTest::gtest_main` targets; a unit-test executable links
`GTest::gtest_main` and is registered with `add_test`. `EXCLUDE_FROM_ALL` means
gtest compiles only when a test links it.

## Updating

Run the update script from anywhere in the checkout:

    ./thirdparty/googletest/update.sh

It follows the [Kitware third-party update convention][update]: the shared
`thirdparty/update-common.sh` clones upstream at the pinned `tag`, our
`extract_source` `git archive`s the reduced path set, and the result is merged onto
the `googletest/` subtree as a single import commit. To move to a new release, bump
`tag` in `update.sh` and re-run.

`thirdparty/update-common.sh` itself is Kitware's script, vendored verbatim so its
own updates stay easy to diff — do not fork it.

[gtest]: https://github.com/google/googletest
[update]: https://gitlab.kitware.com/utils/git/-/blob/master/README-update-common.md
