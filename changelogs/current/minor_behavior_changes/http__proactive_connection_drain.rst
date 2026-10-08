The HTTP connection manager now proactively drains connections with active streams in the last
third of the listener drain time, rather than only checking the drain state when a response is sent.
This spreads the closure of upgraded (e.g. WebSocket) and ``CONNECT`` streams across the drain
window. Idle connections begin draining immediately. Proactive draining is skipped when
:option:`--drain-time-s` is too short to leave room for the connection manager's drain timeout.
The proactive behavior can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.use_connection_event_drain`` to ``false``.
