Fixed a use-after-free when :ref:`upstream_http_filters
<envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>` are configured
via :ref:`config_discovery
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`
(ECDS). The router held the upstream filter config provider manager, an unpinned singleton, only
for the duration of its constructor, while the ECDS subscriptions it created retain a raw reference
to that manager and dereference it when they are destroyed. If no cluster was keeping the singleton
alive at the time the router configuration was built -- for example a statically configured
listener with CDS-supplied clusters -- the manager was freed immediately and the subscriptions were
left holding a dangling reference for the lifetime of the process. The router now retains the
manager for as long as it owns the providers, matching how ``ClusterInfoImpl`` and the UDP proxy
already hold it. The composite filter was hardened in the same way for ``dynamic_config`` actions.
