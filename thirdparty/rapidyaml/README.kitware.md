# RapidYAML

This directory contains a reduced copy of [RapidYAML][ryml] (ryml) vendored into
CODES to back the YAML/JSON configuration front-end.

    Upstream:  https://github.com/biojppm/rapidyaml
    Version:   v0.15.2  (commit 85bfe56e, 2026-06-08)

**Do not make changes here directly.** The upstream drop in `rapidyaml/` is
imported from the upstream repository; make changes upstream, then re-import (see
below). The wrapper files in *this* directory (`CMakeLists.txt`, `codes_ryml.hpp`,
`ryml.cpp`, this README, `update.sh`) are CODES-maintained and are preserved
across re-imports.

## What is vendored

`rapidyaml/` holds the upstream **single-header amalgamation**
(`rapidyaml-0.15.2.hpp`) — a generated, self-contained file that inlines ryml and
its bundled c4core dependency — together with the ryml and c4core `LICENSE`s. We
vendor the amalgamation rather than the multi-file source tree (plus its c4core
submodule) so the build needs no network access and no submodules: the header is
always present and always built. There is no `find_package`, no `FetchContent`,
and no on/off option — the config front-end is core, so ryml is a hard,
build-it-always dependency.

## How CODES builds it

The amalgamation is header-only except that its implementation must be compiled
in exactly one translation unit. `ryml.cpp` does that (it defines
`RYML_SINGLE_HDR_DEFINE_NOW` and includes the header). `CMakeLists.txt` publishes
that TU and the include dirs, and `src/` compiles the TU straight into the
`codes` library (a static library in an install/export set, so it can't link a
separate in-tree lib without dragging it into the export). Consumers just
`#include <codes_ryml.hpp>` — the stable shim that forwards to the versioned
amalgamation, so a bump changes one `#include` line, not every call site.

## Updating

Run the update script from anywhere in the checkout:

    ./thirdparty/rapidyaml/update.sh

It follows the [Kitware third-party update convention][update]: the shared
`thirdparty/update-common.sh` clones upstream at the pinned `tag`, our
`extract_source` fetches the matching release's amalgamation and copies the
`LICENSE`s out of the tagged tree, and the result is merged onto the `rapidyaml/`
subtree as a single import commit. To move to a new release, bump `tag` and
`version` in `update.sh`, update the `#include` in `codes_ryml.hpp`, and re-run.

`thirdparty/update-common.sh` itself is Kitware's script, vendored verbatim so
its own updates stay easy to diff — do not fork it.

[ryml]: https://github.com/biojppm/rapidyaml
[update]: https://gitlab.kitware.com/utils/git/-/blob/master/README-update-common.md
