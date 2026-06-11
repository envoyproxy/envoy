#pragma once

#include "envoy/server/factory_context.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Stat leaf names for dynamic-module config-load failures. They live in the shared package so that
// every dynamic-module extension type (HTTP/UDP/network/listener filters, transport sockets, load
// balancers, ...) reports failures under a single consistent ``dynamic_modules.`` namespace.
constexpr absl::string_view DynamicModulesStatRoot = "dynamic_modules";
// The module itself could not be loaded (missing/invalid source, dlopen failure, by-name miss).
constexpr absl::string_view ModuleLoadErrorStat = "module_load_error";
// A remote module source could not be fetched or loaded (incl. NACK-mode cache misses).
constexpr absl::string_view RemoteFetchErrorStat = "remote_fetch_error";
// The module loaded but initializing the in-module configuration failed.
constexpr absl::string_view ConfigInitErrorStat = "config_init_error";
// A per-route configuration failed to load or initialize.
constexpr absl::string_view PerRouteConfigErrorStat = "per_route_config_error";

/**
 * Increments the ``dynamic_modules.<leaf>`` counter, tagged with ``config_name``.
 *
 * The counter is created on the context's server-wide scope (``CommonFactoryContext::scope()``),
 * NOT a listener scope. A load failure makes the extension factory return a non-ok status, which
 * rejects the whole config update; the draft listener and its stats scope are then destroyed before
 * being merged into the live store, so a counter created on the listener scope would silently
 * disappear. The server scope outlives config add/remove/reject.
 *
 * @param context the factory context whose scope the counter is created on. If absent (a caller
 * without any factory context, e.g. the upstream HTTP conn-pool factory), this is a no-op.
 * @param config_name the configured name of the extension instance using the module (e.g.
 * ``filter_name``, ``transport_socket_name``, ``lb_policy_name``). Falls back to ``default`` if
 * empty.
 * @param leaf one of the ``*Stat`` leaf-name constants above.
 */
void incrementLoadFailure(OptRef<Server::Configuration::CommonFactoryContext> context,
                          absl::string_view config_name, absl::string_view leaf);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
