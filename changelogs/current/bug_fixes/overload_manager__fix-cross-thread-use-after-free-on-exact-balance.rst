Fixed a use-after-free that could crash a worker thread during Envoy shutdown when a TCP
listener is configured with
:ref:`exact_balance <envoy_v3_api_field_config.listener.v3.Listener.ConnectionBalanceConfig.exact_balance>`
and the ``envoy.resource_monitors.global_downstream_max_connections`` overload manager resource
monitor is configured. A connection accepted on one worker could be handed off to a different
worker via connection balancing while still holding a reference into the *originating* worker's
thread-local overload state; because worker threads shut down independently with no ordering
guarantee relative to one another, the destination worker could destroy the connection after the
originating worker had already torn down its own thread-local state, dereferencing freed memory.
The connection now holds a ``shared_ptr`` to the thread-local overload state instead of a bare
reference, keeping it alive for as long as any connection still references it, regardless of
shutdown order.
