/*
 * Copyright (C) 2013 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#ifndef SRC_COMMON_MODELCONFIG_YAML_CONFIGFILE_H
#define SRC_COMMON_MODELCONFIG_YAML_CONFIGFILE_H

#include <stddef.h>
#include <codes/configfile.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile a YAML/JSON config (the user-friendly topology + component format)
 * into the same ConfigVTable the legacy .conf text parser produces, so that
 * codes_mapping and every model read it unchanged through configuration_get_*.
 *
 * data/len are the raw file bytes (already read, e.g. via the MPI collective
 * read in configuration_load). On success returns a newly-allocated
 * ConfigVTable (free with cf_free); on a syntax or validation error the
 * function aborts through tw_error with a diagnostic, matching how the rest of
 * the configuration front-end reports malformed input. */
struct ConfigVTable* yaml_configfile_load(const char* data, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
