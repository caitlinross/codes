# The orchestrator: an alternate entry point

Every CODES executable's `main()` carries the same setup boilerplate: load the
config, register its LP types, call `model_net_register()`, `codes_mapping_setup()`
and `model_net_configure()`, then hand each model LP the network id(s) it will
send to. `codes::orchestrator` (in `codes/orchestrator.h`) encapsulates that
fixed sequence behind a small name-keyed registry plus a single configure call.

This is a **second, cleaner way** to write a CODES `main()`, not a replacement.
The existing per-model `main()`s are unchanged, both entry styles are fully
supported, and both load the same `.conf`/YAML configs through the same
front-end. The orchestrator is also the seam future work hangs typed C++ LP
classes and workload wiring on (see [What is not in it yet](#what-is-not-in-it-yet)).

## The API

```cpp
namespace codes {
class orchestrator {
    using lp_registration_fn = void (*)();
    using net_id_fn          = void (*)(const int* net_ids, int num_nets);

    void register_lp_type(const std::string& name, lp_registration_fn register_fn,
                          net_id_fn net_id_cb = nullptr);

    void configure_simulation(const std::string& config_file);
    void configure_simulation(const std::string& config_file, MPI_Comm comm);

    const tw_lptype* lp_type_lookup(const std::string& name) const;
    void report_model_net_stats() const;
    const int* net_ids() const;
    int num_networks() const;
};
}
```

A program **registers** each of its LP types by name, giving a registration
callback (the `lp_type_register(...)` call the model already has) and an optional
net-id callback. It then calls **one** `configure_simulation()`, which runs the
whole setup sequence in order:

1. set `MPI_COMM_CODES` (to `MPI_COMM_ROSS` for the one-argument overload,
   mirroring `codes_comm_update()`; or to the communicator passed to the
   two-argument overload),
2. `configuration_load()` the config file (`.conf` or YAML, resolved by the
   front-end),
3. run each registered **LP-type registration callback**,
4. `model_net_register()`,
5. `codes_mapping_setup()`,
6. `model_net_configure()`, storing the returned network ids,
7. run each registered **net-id callback**, passing it the whole id array and
   count.

The net-id callback receives `(const int* net_ids, int num_nets)` rather than a
single id: a single-network model just takes `net_ids[0]`, while a model spanning
several networks can distribute them (as the heterogeneous example's `main()`
does by hand today). This is the one deliberate shape change from the earlier
prototype, whose net-id callback took a lone id and asserted exactly one network.

User-facing mistakes fail loudly through `tw_error`, matching how the rest of
CODES reports runtime-boundary failures: a duplicate registration name, a null
registration callback, an unknown LP type at `lp_type_lookup`, calling
`configure_simulation` twice, or a missing/unreadable config file.

`orchestrator` is a **plain object**, not a singleton — a program constructs one,
hands it around, and lets it destruct at end of scope (it owns the
`model_net_configure()` id array). Nothing in CODES currently needs to reach it
from inside an LP callback or from C code, so a stack-local object is simpler than
managing a global instance's lifetime.

## Side by side

The classic `main()` (from `tests/modelnet-test.c`):

```c
tw_opt_add(app_opt);
tw_init(&argc, &argv);
/* ... */
configuration_load(argv[2], MPI_COMM_WORLD, &config);

model_net_register();
svr_add_lp_type();
codes_mapping_setup();

net_ids = model_net_configure(&num_nets);
net_id = *net_ids;
free(net_ids);

/* ... codes_mapping_get_lp_count, lp-io prepare ... */
tw_run();
model_net_report_stats(net_id);
/* ... lp-io flush ... */
tw_end();
```

The same program through the orchestrator (from
`tests/modelnet-orchestrator-test.cxx`):

```cpp
tw_opt_add(app_opt);
tw_init(&argc, &argv);
/* ... */
codes::orchestrator orch;
orch.register_lp_type("nw-lp", svr_add_lp_type, svr_set_net_id);
orch.configure_simulation(argv[2]);

/* ... codes_mapping_get_lp_count, lp-io prepare ... */
tw_run();
orch.report_model_net_stats();
/* ... lp-io flush ... */
tw_end();
```

where the net id the model sends to is delivered by a small callback instead of
being pulled out of the `model_net_configure()` return by hand:

```cpp
static void svr_set_net_id(const int* net_ids, int num_nets) {
    assert(num_nets >= 1);
    net_id = net_ids[0];
}
```

The two binaries are proven equivalent by the `orchestrator-simplenet-*` ctests,
which run `modelnet-test` and `modelnet-orchestrator-test` on the same config and
diff their per-LP lp-io output (and committed event count) — for the `.conf`, its
YAML twin, and once multi-rank optimistic. Because the twin's server LP is a copy
of `modelnet-test.c`'s, those tests also guard the copy: if one LP changes and the
other does not, the binaries diverge and the test fails.

## What is not in it yet

The orchestrator is a first alternate entry point, not a framework. Deliberately
out of scope for now:

- **Typed C++ LP classes.** Registration is still a name plus the model's existing
  C-style `lp_type_register` callback. The orchestrator is where a future typed-LP
  registration API will attach, but it does not define one yet.
- **Workload/traffic wiring.** The program still reads its own workload parameters
  and drives `tw_run()` itself; the orchestrator only owns the config-to-mapping
  setup sequence.
- **Mapping policy.** It drives the existing `codes_mapping_setup()`; it does not
  introduce a new mapper or placement policy.

Both entry styles — the hand-written `main()` and the orchestrator — are
supported and will remain so.
```
