Fixed a race where the share provider manager owned by a
:ref:`local rate limit <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`
route level configuration could be destroyed on a worker thread. The manager is an unpinned
singleton that owns a cluster membership callback handle, so a route level configuration may own
the last reference to it and release it when an RDS update replaces the route configuration. It is
now handed to the main dispatcher alongside the rate limiter, which was already posted there.
