A non-graceful drain (``/drain_listeners`` without ``graceful``) now starts a drain sequence and
notifies the connections of the covered listeners that a drain has begun, in addition to stopping
the listeners. Previously nothing was drained: the listeners simply stopped accepting, and the
connections they already owned were never told, so no connection-level drain logic ran for them. The
drain honors the configured :option:`--drain-strategy`, as a graceful drain does; ``graceful`` only
controls whether the listeners keep accepting for a drain period before they are stopped. As a
result, ``skip_exit`` is now accepted without ``graceful`` (it was rejected with a 400 before) and
means "drain the connections, but never stop the listeners", which is what ``graceful&skip_exit``
already did. This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.non_graceful_drain_notifies_connections`` to ``false``.
