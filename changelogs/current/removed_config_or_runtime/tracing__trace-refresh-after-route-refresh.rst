Removed the runtime guard ``envoy.reloadable_features.trace_refresh_after_route_refresh`` and the
legacy code path it guarded. The HTTP connection manager now always refreshes the trace decision and
decorator when the route is refreshed, and charges the tracing statistics from ``chargeStats`` rather
than from the old un-refreshed code path.
