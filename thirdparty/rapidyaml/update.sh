#!/usr/bin/env bash

#=============================================================================
# Re-import the vendored RapidYAML amalgamation from upstream.
#
# Usage (run from anywhere in the checkout):
#
#     ./thirdparty/rapidyaml/update.sh
#
# This follows the Kitware third-party update convention: it sets the variables
# below and defines extract_source, then sources the shared update-common.sh,
# which clones upstream at $tag and merges the extracted tree onto the
# $subtree/ subtree as a single import commit.
#
# To move to a new release: bump $tag and $version here, update the #include in
# ../codes_ryml.hpp, and re-run.
#=============================================================================

set -e
shopt -s dotglob

readonly name="rapidyaml"
readonly ownership="RapidYAML Upstream <robot@codes>"
readonly subtree="thirdparty/rapidyaml/rapidyaml"
readonly repo="https://github.com/biojppm/rapidyaml.git"
readonly tag="v0.15.2"
readonly version="0.15.2"
readonly shortlog="false"
# The imported tree is the release amalgamation, not a subset of upstream's own
# tree, so tree-object matching against upstream can't find the previous import;
# fall back to log-based matching on the import commit message.
readonly exact_tree_match="false"

extract_source () {
    # RapidYAML publishes its single-header amalgamation as a *release artifact*
    # (generated; not committed to the repo tree), so fetch that rather than
    # git-archiving the multi-file source and its c4core submodule. The LICENSEs
    # do live in the tagged checkout (c4core is a submodule), so copy them from
    # there — extract_source runs with the upstream checkout as its working dir.
    mkdir -p "$extractdir/$name-reduced"
    cp -v LICENSE.txt            "$extractdir/$name-reduced/LICENSE.txt"
    cp -v ext/c4core/LICENSE.txt "$extractdir/$name-reduced/LICENSE.c4core.txt"
    curl -fSL \
        "https://github.com/biojppm/rapidyaml/releases/download/$tag/rapidyaml-$version.hpp" \
        -o "$extractdir/$name-reduced/rapidyaml-$version.hpp"
}

. "${BASH_SOURCE%/*}/../update-common.sh"
