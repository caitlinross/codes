/*
 * Stable include point for the vendored RapidYAML amalgamation.
 *
 * Consumers #include <codes_ryml.hpp> instead of the versioned amalgamation
 * header directly, so a version bump touches only this one line (plus the
 * `version` in thirdparty/rapidyaml/update.sh) rather than every call site.
 *
 * Do not edit the amalgamation itself — it is a generated upstream drop. See
 * README.kitware.md.
 */
#ifndef CODES_THIRDPARTY_RYML_HPP
#define CODES_THIRDPARTY_RYML_HPP

#include <rapidyaml-0.15.2.hpp>

#endif /* CODES_THIRDPARTY_RYML_HPP */
