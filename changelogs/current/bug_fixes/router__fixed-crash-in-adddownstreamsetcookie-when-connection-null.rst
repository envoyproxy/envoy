Fixed a crash in ``Filter::addDownstreamSetCookie()`` when the router is invoked
without a downstream connection (e.g. ``Http::AsyncClient``-driven paths such as
``ext_authz`` or ratelimit side calls, mirror/shadow, and health checks) and a
cookie hash policy is configured. Previously the function unconditionally
dereferenced ``downstreamConnection()`` to compose the per-connection seed for
the generated cookie value. When the connection pointer was null the worker
would crash. The function now skips the connection-derived seed and returns an
empty cookie value in that path, preserving hash-stability for those async
invocations.
