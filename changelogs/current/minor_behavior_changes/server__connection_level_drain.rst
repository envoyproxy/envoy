Connection drain-close decisions (HTTP connection manager, TCP proxy, Mongo proxy, Redis proxy,
Thrift proxy, generic proxy and the drain-aware HTTP connection manager) are now derived from a
drain event that is pushed to each connection when the drain sequence starts on the main thread,
rather than from polling the listener's ``DrainDecision`` on every response.

This also fixes a bug where the Mongo, Redis, Thrift and generic proxies did not honor
inbound-only drain-close decisions: they always asked whether both inbound and outbound
connections were draining, so ``/drain_listeners?graceful&inboundonly`` left their connections
open even on an inbound listener. Because the new drain event is only delivered to the listeners
covered by the drain, these proxies now drain-close in that case.

This behavioral change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.use_connection_event_drain`` to ``false``. The guard is read once per
connection when the network filter is created, so changing it affects new connections only.
