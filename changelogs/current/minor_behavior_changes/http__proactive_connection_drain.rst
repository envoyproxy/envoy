The HTTP connection manager now proactively drains the connections of a draining listener in the
last third of the drain time, rather than only checking the drain state when a response is sent.
This avoids idle connections and connections with upgraded (e.g. WebSocket) or ``CONNECT`` streams
all being closed at the same time at the end of the drain sequence. Connections are not drained
proactively when :option:`--drain-time-s` is too short to leave room for it after the connection
manager's drain timeout. This behavioral change can be temporarily reverted by setting the runtime
guard ``envoy.reloadable_features.use_connection_event_drain`` to ``false``.
