Fixed a race where the route level configuration of the
:ref:`composite <envoy_v3_api_msg_extensions.filters.http.composite.v3.CompositePerRoute>` filter
could be destroyed on a worker thread when an RDS update replaced the route configuration. The
match tree, and the delegated filter configurations its actions own, are now released on the main
thread.
