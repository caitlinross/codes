# Vendored third-party dependencies.
#
# Each subdirectory is an upstream drop managed with the Kitware third-party
# update convention (the shared update-common.sh here, driven by a per-package
# update.sh) plus a thin CMake wrapper that exposes a codes::thirdparty::<name>
# target. See each package's README.kitware.md for its upstream origin, pinned
# version, and how to re-import it.
add_subdirectory(rapidyaml)
