.. _config_listeners_runtime:

Runtime
-------
The following runtime settings are supported:

envoy.resource_limits.listener.<name of listener>.connection_limit
    Sets a limit on the number of active connections to the specified listener.

envoy.resource_limits.listener.<name of listener>.udp_flow_limit
    Sets a limit on the number of tracked downstream UDP flows for hot restart handoff on the
    specified connectionless UDP listener. Flows beyond the limit are served normally. During a
    hot restart they are forwarded to the new instance as new flows rather than kept on the
    draining instance.
