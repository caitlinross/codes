/*
 * Copyright (C) 2013 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#include "codes/codes-workload-config.h"

#include "codes/configuration.h"

#include <ross.h>

#include <string.h>

/* the section the YAML front-end emits synthetic workload params into. */
#define CODES_WORKLOAD_SECTION "WORKLOAD"

int codes_workload_config_present(void) {
    char buf[CONFIGURATION_MAX_NAME];
    return configuration_get_value(&config, CODES_WORKLOAD_SECTION, "type", NULL, buf,
                                   sizeof(buf)) > 0;
}

void codes_workload_config_apply_int(const char* key, int* val, int cli_default) {
    /* a value other than the registered default means the command line set it. */
    if (*val != cli_default)
        return;
    int v;
    if (configuration_get_value_int(&config, CODES_WORKLOAD_SECTION, key, NULL, &v) == 0)
        *val = v;
}

void codes_workload_config_apply_double(const char* key, double* val, double cli_default) {
    if (*val != cli_default)
        return;
    double v;
    if (configuration_get_value_double(&config, CODES_WORKLOAD_SECTION, key, NULL, &v) == 0)
        *val = v;
}

void codes_workload_config_apply_traffic(int* val, int cli_default,
                                         const struct codes_workload_traffic_name* names) {
    if (*val != cli_default)
        return;
    char buf[CONFIGURATION_MAX_NAME];
    if (configuration_get_value(&config, CODES_WORKLOAD_SECTION, "traffic", NULL, buf,
                                sizeof(buf)) <= 0)
        return;
    for (const struct codes_workload_traffic_name* n = names; n->name != NULL; ++n) {
        if (strcmp(n->name, buf) == 0) {
            *val = n->value;
            return;
        }
    }
    tw_error(TW_LOC, "config error: workload traffic pattern \"%s\" is not supported by this model",
             buf);
}
