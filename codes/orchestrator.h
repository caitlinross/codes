/*
 * Copyright (C) 2025 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#ifndef CODES_ORCHESTRATOR_H
#define CODES_ORCHESTRATOR_H

/**
 * @file orchestrator.h
 *
 * An alternate entry point for a CODES program.
 *
 * Every CODES executable's `main()` carries the same boilerplate: load the
 * config, register its LP types, call model_net_register(), codes_mapping_setup()
 * and model_net_configure(), then hand each model LP the network id(s) it will
 * send to. @ref codes::orchestrator encapsulates that fixed sequence behind a
 * small name-keyed registry plus a single configure call, so a program describes
 * *what* LP types it has and lets the orchestrator run the setup in the right
 * order.
 *
 * This is a second, cleaner way to write a CODES `main()`, not a replacement:
 * the existing per-model `main()`s are unchanged, and both styles load the same
 * `.conf`/YAML configs through the same front-end. It is also the seam future
 * work hangs typed C++ LP classes and workload wiring on.
 *
 * Unlike the config front-end (a ROSS-free pure core that throws), the
 * orchestrator drives the run sequence and is therefore inherently ROSS/MPI
 * coupled. It reports user-facing mistakes -- a duplicate registration name, an
 * unknown LP type at lookup, configuring twice, a missing config file -- the way
 * the rest of CODES reports runtime-boundary failures: loudly, through
 * `tw_error`.
 *
 * It is a plain object, not a singleton. Nothing in CODES currently needs to
 * reach the orchestrator from inside an LP callback or from C code, so a
 * stack-local object the program hands around is simpler than managing a global
 * instance's lifetime (the sole reason the earlier prototype was a singleton was
 * to be reachable from LP code, a need this harvested version does not have). A
 * singleton accessor can be layered on later without changing this API if a
 * future callback needs to find it.
 */

#include <mpi.h>
#include <ross.h>

#include <string>
#include <vector>

namespace codes {

/**
 * Runs the fixed CODES setup sequence -- config load, LP-type registration,
 * model_net_register(), codes_mapping_setup(), model_net_configure(), and
 * network-id distribution -- driven by a name-keyed registry the program fills
 * in before calling @ref configure_simulation.
 */
class orchestrator {
  public:
    /** Registers an LP type with ROSS (typically one `lp_type_register` call).
     *  Run, in registration order, during @ref configure_simulation before
     *  model_net_register(). */
    using lp_registration_fn = void (*)();

    /** Hands a model LP the network id(s) produced by model_net_configure().
     *  Receives the whole id array and its count so a model spanning several
     *  networks can distribute them (most take @p net_ids[0]); run, in
     *  registration order, after model_net_configure(). */
    using net_id_fn = void (*)(const int* net_ids, int num_nets);

    orchestrator() = default;
    ~orchestrator();

    // Owns the model_net_configure() id array, so it is non-copyable.
    orchestrator(const orchestrator&) = delete;
    orchestrator& operator=(const orchestrator&) = delete;

    /**
     * Register the callbacks for one LP type under @p name.
     *
     * @param name         registry key; must be unique across all registrations.
     * @param register_fn  registers the LP type with ROSS; required.
     * @param net_id_cb    optional; receives the network ids after
     *                     model_net_configure(). Pass nullptr for LP types that
     *                     do not originate model-net traffic.
     *
     * Fails via `tw_error` if @p register_fn is null or @p name is already
     * registered.
     */
    void register_lp_type(const std::string& name, lp_registration_fn register_fn,
                          net_id_fn net_id_cb = nullptr);

    /**
     * Run the whole setup sequence for @p config_file on MPI_COMM_ROSS.
     *
     * Loads the config (`.conf` or YAML, resolved by the front-end), runs each
     * registered LP-type registration callback, then model_net_register(),
     * codes_mapping_setup() and model_net_configure(), and finally hands the
     * resulting network ids to each registered net-id callback. Sets
     * MPI_COMM_CODES to MPI_COMM_ROSS first, mirroring `codes_comm_update()`.
     *
     * Fails via `tw_error` if @p config_file is missing/unreadable or if
     * configure_simulation has already run on this object.
     */
    void configure_simulation(const std::string& config_file);

    /** As @ref configure_simulation(const std::string&), but runs against @p comm
     *  (sets MPI_COMM_CODES to it) instead of MPI_COMM_ROSS. */
    void configure_simulation(const std::string& config_file, MPI_Comm comm);

    /** Look up an LP type by the name it was registered with (via
     *  lp_type_register). Fails via `tw_error` if no such type is registered. */
    const tw_lptype* lp_type_lookup(const std::string& name) const;

    /** Report model-net statistics for every configured network
     *  (model_net_report_stats). Fails via `tw_error` if called before
     *  @ref configure_simulation. */
    void report_model_net_stats() const;

    /** The network ids from model_net_configure(), or nullptr before
     *  @ref configure_simulation. Owned by the orchestrator. */
    const int* net_ids() const { return this->network_ids; }

    /** The number of networks model_net_configure() produced. */
    int num_networks() const { return this->network_count; }

  private:
    void run_configuration(const std::string& config_file);

    struct lp_registration {
        std::string name;
        lp_registration_fn register_fn;
        net_id_fn net_id_cb;
    };

    std::vector<lp_registration> registrations;
    int* network_ids = nullptr;
    int network_count = 0;
    bool configured = false;
};

} // end namespace codes

#endif /* CODES_ORCHESTRATOR_H */
