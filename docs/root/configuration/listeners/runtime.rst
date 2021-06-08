.. _config_listeners_runtime:

Runtime
-------
The following runtime settings are supported:

envoy.resource_limits.listener.<name of listener>.connection_limit
    Sets a limit on the number of active connections to the specified listener.

envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
    Connection limiting :ref:`architecture overview <arch_overview_connection_limit>`
