/*
 * Copyright (C) 2025 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#include "codes/orchestrator.h"

#include "codes/codes.h"
#include "codes/codes_mapping.h"
#include "codes/configuration.h"
#include "codes/lp-type-lookup.h"
#include "codes/model-net.h"

#include <cstdio>
#include <cstdlib>

namespace codes {

orchestrator::~orchestrator() {
    // model_net_configure() returns a malloc'd id array.
    free(this->network_ids);
}

void orchestrator::register_lp_type(const std::string& name, lp_registration_fn register_fn,
                                    net_id_fn net_id_cb) {
    if (!register_fn) {
        tw_error(TW_LOC, "orchestrator: null registration callback for LP type \"%s\"",
                 name.c_str());
    }
    for (const auto& reg : this->registrations) {
        if (reg.name == name) {
            tw_error(TW_LOC, "orchestrator: LP type \"%s\" registered more than once",
                     name.c_str());
        }
    }
    this->registrations.push_back({name, register_fn, net_id_cb});
}

void orchestrator::configure_simulation(const std::string& config_file) {
    // Default MPI_COMM_CODES to MPI_COMM_ROSS (ROSS may have split MPI_COMM_ROSS
    // during tw_init, so this mirrors what a classic main does via
    // codes_comm_update()).
    codes_comm_update();
    this->run_configuration(config_file);
}

void orchestrator::configure_simulation(const std::string& config_file, MPI_Comm comm) {
    MPI_COMM_CODES = comm;
    this->run_configuration(config_file);
}

void orchestrator::run_configuration(const std::string& config_file) {
    if (this->configured) {
        tw_error(TW_LOC, "orchestrator: configure_simulation called more than once");
    }
    if (config_file.empty()) {
        tw_error(TW_LOC, "orchestrator: no config file given");
    }
    // Fail early with a clear message on a missing/unreadable config, rather than
    // deeper inside the loader.
    if (FILE* f = fopen(config_file.c_str(), "r")) {
        fclose(f);
    } else {
        tw_error(TW_LOC, "orchestrator: cannot open config file \"%s\"", config_file.c_str());
    }

    if (configuration_load(config_file.c_str(), MPI_COMM_CODES, &config) != 0) {
        tw_error(TW_LOC, "orchestrator: failed to load config file \"%s\"", config_file.c_str());
    }

    // Register the program's LP types, then the model-net LP types, before
    // mapping so codes_mapping_setup() can place every LP type.
    for (const auto& reg : this->registrations) {
        reg.register_fn();
    }
    model_net_register();

    codes_mapping_setup();

    this->network_ids = model_net_configure(&this->network_count);
    if (this->network_count < 1) {
        tw_error(TW_LOC, "orchestrator: model_net_configure() produced no networks");
    }

    // Hand each model LP the network ids it will send to. Each callback gets the
    // whole array + count so a model spanning several networks can distribute
    // them; single-network models just take net_ids[0].
    for (const auto& reg : this->registrations) {
        if (reg.net_id_cb) {
            reg.net_id_cb(this->network_ids, this->network_count);
        }
    }

    this->configured = true;
}

const tw_lptype* orchestrator::lp_type_lookup(const std::string& name) const {
    const tw_lptype* type = ::lp_type_lookup(name.c_str());
    if (!type) {
        tw_error(TW_LOC, "orchestrator: no LP type registered under name \"%s\"", name.c_str());
    }
    return type;
}

void orchestrator::report_model_net_stats() const {
    if (!this->configured) {
        tw_error(TW_LOC, "orchestrator: report_model_net_stats called before configure_simulation");
    }
    for (int i = 0; i < this->network_count; i++) {
        model_net_report_stats(this->network_ids[i]);
    }
}

} // end namespace codes
