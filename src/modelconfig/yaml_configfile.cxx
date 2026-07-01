/*
 * Copyright (C) 2013 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

/*
 * YAML/JSON configuration front-end. Reads the user-friendly topology +
 * component format (Cytoscape elements for enumerated topologies, or a
 * parametric fabric block for HPC networks) and compiles it down to the same
 * ConfigVTable the legacy .conf text parser produces -- an LPGROUPS section
 * describing group/repetition/lp-type counts and a PARAMS section of key/value
 * pairs. codes_mapping and the network models then read the result unchanged
 * through the configuration_get_* accessors, so the compiler is a narrow seam:
 * a new front-end for an unchanged back-end.
 *
 * Values are carried through as their raw YAML scalar text (not reparsed and
 * reformatted) so a compiled config is byte-for-byte comparable to the .conf it
 * replaces.
 */

#include "codes_config.h"

#include "yaml_configfile.h"
#include "configstoreadapter.h"

#include <ross.h>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{

/* -------------------------------------------------------------------------
 * ryml plumbing
 * ---------------------------------------------------------------------- */

/* Route ryml parse/lookup errors through tw_error so malformed input aborts
 * with a diagnostic, the same way the rest of the config front-end reports it.
 * ryml requires this callback to never return; tw_error aborts, satisfying it. */
[[noreturn]] void yaml_error(const char* msg, size_t msg_len, ryml::Location, void*)
{
    tw_error(TW_LOC, "YAML config error: %.*s", (int)msg_len, msg);
    abort(); /* unreachable; keeps the [[noreturn]] contract explicit */
}

/* raw scalar text of a node, preserving the exact source spelling */
std::string scalar(ryml::ConstNodeRef n)
{
    ryml::csubstr v = n.val();
    return std::string(v.str, v.len);
}

std::string key_of(ryml::ConstNodeRef n)
{
    ryml::csubstr k = n.key();
    return std::string(k.str, k.len);
}

bool has(ryml::ConstNodeRef n, const char* key)
{
    return n.readable() && n.is_map() && n.has_child(ryml::to_csubstr(key));
}

/* -------------------------------------------------------------------------
 * Intermediate representation
 * ---------------------------------------------------------------------- */

using kv_list = std::vector<std::pair<std::string, std::string>>;

/* A custom component: a model paired with configured parameters. */
struct component
{
    std::string key;      /* the components: key referenced by nodes / hosts */
    std::string model;    /* ComponentModel name (nw-lp, simplep2p, dragonfly, ...) */
    std::string network;  /* enumerated flat models: the NIC model a compute node
                             runs its workload over (e.g. simplenet, simplep2p) */
    kv_list params;       /* scalar model params, raw text, in source order */
};

/* A placed component in an enumerated (Cytoscape) topology. */
struct node
{
    std::string id;
    std::string component;  /* references a components: key */
};

/* A per-link-class parameter block (e.g. dragonfly local/global/cn). */
struct link_class
{
    std::string name;   /* class name; combines with each param as <name>_<param> */
    kv_list params;
};

/* A parametric fabric: an HPC topology described by shape parameters. */
struct fabric
{
    std::string model;              /* network model, e.g. "dragonfly" */
    kv_list shape;                  /* shape parameters (also drive count derivation) */
    std::vector<link_class> links;  /* per-link-class bandwidth / vc_size */
    kv_list routing;                /* routing.* (algorithm maps to PARAMS "routing") */
    kv_list extra;                  /* other scalar fabric keys -> PARAMS verbatim */
    std::string hosts_component;    /* hosts.component: the per-terminal workload */
};

struct config
{
    std::vector<component> components;
    bool parametric = false;
    fabric fab;               /* parametric topology */
    std::vector<node> nodes;  /* enumerated topology */

    const component* find_component(const std::string& k) const
    {
        for (const component& c : components)
            if (c.key == k)
                return &c;
        return nullptr;
    }
};

/* -------------------------------------------------------------------------
 * Model registry -- maps a friendly fabric model name to the LP-type names,
 * modelnet_order method names, and shape->counts derivation the model expects.
 * ---------------------------------------------------------------------- */

struct fabric_model
{
    const char* name;             /* friendly name used in fabric.model */
    const char* terminal_lp;      /* LPGROUPS lp-type name for the NIC/terminal */
    const char* router_lp;        /* LPGROUPS lp-type name for the router */
    const char* term_method;      /* modelnet_order method name for the terminal */
    const char* router_method;    /* modelnet_order method name for the router */

    /* Derive the layout from the shape parameters: total router repetitions and
     * the number of compute-node terminals per router. */
    void (*derive)(const kv_list& shape, long& repetitions, long& cns_per_router);
};

/* Look up a shape value by name, aborting if absent. */
long shape_int(const kv_list& shape, const char* key)
{
    for (const auto& kv : shape)
        if (kv.first == key)
            return strtol(kv.second.c_str(), nullptr, 10);
    tw_error(TW_LOC, "YAML config error: fabric shape is missing required key \"%s\"", key);
    return 0;
}

/* Regular (Kim-Dally) dragonfly: every count follows from num_routers, the
 * number of routers per group -- the same derivation the model does internally
 * (num_cn = num_routers/2, num_groups = num_routers*num_cn + 1). */
void derive_dragonfly(const kv_list& shape, long& repetitions, long& cns_per_router)
{
    long num_routers = shape_int(shape, "num_routers");
    if (num_routers <= 0)
        tw_error(TW_LOC, "YAML config error: dragonfly num_routers must be positive");
    long num_cn = num_routers / 2;
    long num_groups = num_routers * num_cn + 1;
    cns_per_router = num_cn;
    repetitions = num_groups * num_routers;
}

const fabric_model fabric_models[] = {
    {"dragonfly", "modelnet_dragonfly", "modelnet_dragonfly_router", "dragonfly",
     "dragonfly_router", derive_dragonfly},
};

const fabric_model* find_fabric_model(const std::string& name)
{
    for (const fabric_model& m : fabric_models)
        if (name == m.name)
            return &m;
    return nullptr;
}

/* A flat (enumerated) network model: one NIC LP per compute node, all peers.
 * Maps a friendly network name to the LPGROUPS lp-type name and the
 * modelnet_order method name the model registers. */
struct network_model
{
    const char* name;       /* friendly name used in a component's network: field */
    const char* nic_lp;     /* LPGROUPS lp-type name for the NIC */
    const char* method;     /* modelnet_order method name */
};

const network_model network_models[] = {
    {"simplenet", "modelnet_simplenet", "simplenet"},
    {"simplep2p", "modelnet_simplep2p", "simplep2p"},
};

const network_model* find_network_model(const std::string& name)
{
    for (const network_model& m : network_models)
        if (name == m.name)
            return &m;
    return nullptr;
}

/* -------------------------------------------------------------------------
 * Parse: ryml tree -> IR
 * ---------------------------------------------------------------------- */

void parse_components(ryml::ConstNodeRef root, config& cfg)
{
    if (!has(root, "components"))
        return;
    for (ryml::ConstNodeRef cnode : root["components"].children())
    {
        component c;
        c.key = key_of(cnode);
        for (ryml::ConstNodeRef f : cnode.children())
        {
            std::string k = key_of(f);
            if (k == "model")
                c.model = scalar(f);
            else if (k == "network")
                c.network = scalar(f);
            else if (k == "type")
                ; /* inferred from the model; not needed for the compiled config */
            else if (f.is_keyval())
                c.params.emplace_back(k, scalar(f));
            /* nested param blocks (e.g. workload:) are flattened by the model-net
             * pass-through where they apply; enumerated-model support adds that. */
        }
        cfg.components.push_back(std::move(c));
    }
}

void parse_fabric(ryml::ConstNodeRef fnode, fabric& fab)
{
    for (ryml::ConstNodeRef c : fnode.children())
    {
        std::string k = key_of(c);
        if (k == "model")
            fab.model = scalar(c);
        else if (k == "shape")
        {
            for (ryml::ConstNodeRef s : c.children())
                fab.shape.emplace_back(key_of(s), scalar(s));
        }
        else if (k == "links")
        {
            for (ryml::ConstNodeRef lc : c.children())
            {
                link_class cls;
                cls.name = key_of(lc);
                for (ryml::ConstNodeRef p : lc.children())
                    cls.params.emplace_back(key_of(p), scalar(p));
                fab.links.push_back(std::move(cls));
            }
        }
        else if (k == "routing")
        {
            for (ryml::ConstNodeRef r : c.children())
                fab.routing.emplace_back(key_of(r), scalar(r));
        }
        else if (k == "connections")
        {
            /* file-enumerated dragonflies reference the binary connection files
             * by path; pass each through to PARAMS verbatim. */
            for (ryml::ConstNodeRef cn : c.children())
                fab.extra.emplace_back(key_of(cn), scalar(cn));
        }
        else if (c.is_keyval())
            fab.extra.emplace_back(k, scalar(c));
    }
}

/* Read the nodes from a Cytoscape elements block. Accepts the object form
 * ({ nodes: [...], edges: [...] }); each node carries its fields under `data`.
 * Edges describe connectivity/link rates for future WAN models; the flat models
 * here take their link table from a referenced matrix file, so edges are parsed
 * past but not consumed. */
void parse_nodes(ryml::ConstNodeRef elements, config& cfg)
{
    if (!has(elements, "nodes"))
        tw_error(TW_LOC, "YAML config error: cytoscape elements need a \"nodes\" list");
    for (ryml::ConstNodeRef n : elements["nodes"].children())
    {
        ryml::ConstNodeRef data = has(n, "data") ? n["data"] : n;
        node nd;
        if (has(data, "id"))
            nd.id = scalar(data["id"]);
        if (has(data, "component"))
            nd.component = scalar(data["component"]);
        else
            tw_error(TW_LOC, "YAML config error: node \"%s\" has no component", nd.id.c_str());
        cfg.nodes.push_back(std::move(nd));
    }
}

void parse_topology(ryml::ConstNodeRef root, config& cfg)
{
    if (!has(root, "topology"))
        tw_error(TW_LOC, "YAML config error: missing required \"topology\" block");
    ryml::ConstNodeRef topo = root["topology"];

    std::string format;
    if (has(topo, "format"))
        format = scalar(topo["format"]);

    if (format == "parametric")
    {
        cfg.parametric = true;
        if (!has(topo, "fabric"))
            tw_error(TW_LOC, "YAML config error: parametric topology needs a \"fabric\" block");
        parse_fabric(topo["fabric"], cfg.fab);
        if (has(topo, "hosts") && has(topo["hosts"], "component"))
            cfg.fab.hosts_component = scalar(topo["hosts"]["component"]);
    }
    else if (format == "cytoscape" || format.empty())
    {
        if (has(topo, "elements"))
            parse_nodes(topo["elements"], cfg);
        else
            tw_error(TW_LOC, "YAML config error: cytoscape topology needs an \"elements\" block");
    }
    else
    {
        tw_error(TW_LOC, "YAML config error: unknown topology format \"%s\"", format.c_str());
    }
}

config parse(const char* data, size_t len)
{
    ryml::set_callbacks(ryml::Callbacks(nullptr, nullptr, nullptr, yaml_error));

    /* copy into a mutable buffer ryml can own; parse_in_arena keeps the source
     * text alive in the tree's arena for the node scalars we read. */
    std::string text(data, len);
    ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(text));
    ryml::ConstNodeRef root = tree.rootref();

    config cfg;
    parse_components(root, cfg);
    parse_topology(root, cfg);
    return cfg;
}

/* -------------------------------------------------------------------------
 * Compile: IR -> ConfigVTable
 * ---------------------------------------------------------------------- */

void put_key(ConfigVTable* cf, SectionHandle sec, const std::string& key, const std::string& val)
{
    const char* v = val.c_str();
    cf_createKey(cf, sec, key.c_str(), &v, 1);
}

/* Compile a parametric fabric into LPGROUPS + PARAMS. */
void compile_fabric(const config& cfg, ConfigVTable* cf)
{
    const fabric& fab = cfg.fab;

    const fabric_model* model = find_fabric_model(fab.model);
    if (!model)
        tw_error(TW_LOC, "YAML config error: unknown fabric model \"%s\"", fab.model.c_str());

    const component* host = cfg.find_component(fab.hosts_component);
    if (!host)
        tw_error(TW_LOC,
                 "YAML config error: hosts.component \"%s\" is not defined under components:",
                 fab.hosts_component.c_str());

    long repetitions = 0, cns_per_router = 0;
    model->derive(fab.shape, repetitions, cns_per_router);

    /* --- LPGROUPS: one group of `repetitions` router-sized slices, each with
     * the per-terminal workload + NIC LPs and a single router LP. Emit the
     * lp-types in [workload, terminal, router] order -- codes_mapping assigns
     * LP ids in this order, so it must match the layout the model expects. --- */
    SectionHandle lpgroups, grp;
    cf_createSection(cf, ROOT_SECTION, "LPGROUPS", &lpgroups);
    cf_createSection(cf, lpgroups, "MODELNET_GRP", &grp);

    put_key(cf, grp, "repetitions", std::to_string(repetitions));
    put_key(cf, grp, host->model, std::to_string(cns_per_router));
    put_key(cf, grp, model->terminal_lp, std::to_string(cns_per_router));
    put_key(cf, grp, model->router_lp, "1");

    /* --- PARAMS --- */
    SectionHandle params;
    cf_createSection(cf, ROOT_SECTION, "PARAMS", &params);

    /* modelnet_order is derived from the fabric model (terminal then router). */
    const char* order[2] = {model->term_method, model->router_method};
    cf_createKey(cf, params, "modelnet_order", order, 2);

    /* shape parameters pass straight through (num_routers etc.). */
    for (const auto& kv : fab.shape)
        put_key(cf, params, kv.first, kv.second);

    /* per-link-class params become <class>_<param> (local_bandwidth, ...). */
    for (const link_class& cls : fab.links)
        for (const auto& kv : cls.params)
            put_key(cf, params, cls.name + "_" + kv.first, kv.second);

    /* routing.algorithm -> "routing"; any other routing.* passes through. */
    for (const auto& kv : fab.routing)
        put_key(cf, params, kv.first == "algorithm" ? "routing" : kv.first, kv.second);

    /* remaining scalar fabric keys (packet_size, chunk_size, and parity
     * pass-through knobs) map to PARAMS verbatim. */
    for (const auto& kv : fab.extra)
        put_key(cf, params, kv.first, kv.second);

    /* the workload component's own params (if any) also land in PARAMS. */
    for (const auto& kv : host->params)
        put_key(cf, params, kv.first, kv.second);
}

/* Compile an enumerated (Cytoscape) topology of flat compute nodes into
 * LPGROUPS + PARAMS. Each node is one repetition running its workload LP over a
 * NIC LP; all nodes reference the same compute-node component. */
void compile_enumerated(const config& cfg, ConfigVTable* cf)
{
    if (cfg.nodes.empty())
        tw_error(TW_LOC, "YAML config error: enumerated topology has no nodes");

    /* One homogeneous compute-node component for now; heterogeneous regions are
     * the multi-network work. */
    const std::string& comp_key = cfg.nodes.front().component;
    for (const node& n : cfg.nodes)
        if (n.component != comp_key)
            tw_error(TW_LOC,
                     "YAML config error: enumerated topologies with more than one component are "
                     "not yet supported (node \"%s\" uses \"%s\", expected \"%s\")",
                     n.id.c_str(), n.component.c_str(), comp_key.c_str());

    const component* comp = cfg.find_component(comp_key);
    if (!comp)
        tw_error(TW_LOC,
                 "YAML config error: node component \"%s\" is not defined under components:",
                 comp_key.c_str());
    if (comp->network.empty())
        tw_error(TW_LOC,
                 "YAML config error: component \"%s\" needs a network: field naming its NIC model",
                 comp_key.c_str());

    const network_model* net = find_network_model(comp->network);
    if (!net)
        tw_error(TW_LOC, "YAML config error: unknown network model \"%s\"", comp->network.c_str());

    /* --- LPGROUPS: one repetition per node, each a workload LP + its NIC LP,
     * emitted in [workload, NIC] order to match the model's layout. --- */
    SectionHandle lpgroups, grp;
    cf_createSection(cf, ROOT_SECTION, "LPGROUPS", &lpgroups);
    cf_createSection(cf, lpgroups, "MODELNET_GRP", &grp);
    put_key(cf, grp, "repetitions", std::to_string(cfg.nodes.size()));
    put_key(cf, grp, comp->model, "1");
    put_key(cf, grp, net->nic_lp, "1");

    /* --- PARAMS: modelnet_order is derived from the network model; the
     * component's params (message_size, packet_size, matrix-file references,
     * ...) pass straight through. --- */
    SectionHandle params;
    cf_createSection(cf, ROOT_SECTION, "PARAMS", &params);
    const char* order[1] = {net->method};
    cf_createKey(cf, params, "modelnet_order", order, 1);
    for (const auto& kv : comp->params)
        put_key(cf, params, kv.first, kv.second);
}

} // namespace

/* -------------------------------------------------------------------------
 * C entry point
 * ---------------------------------------------------------------------- */

extern "C" struct ConfigVTable* yaml_configfile_load(const char* data, size_t len)
{
    config cfg = parse(data, len);

    ConfigVTable* cf = cfsa_create_empty();
    if (cfg.parametric)
        compile_fabric(cfg, cf);
    else
        compile_enumerated(cfg, cf);

    return cf;
}
