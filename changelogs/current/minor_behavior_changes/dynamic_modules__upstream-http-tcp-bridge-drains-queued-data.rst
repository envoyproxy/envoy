When upstream data a module sent is still queued as a request ends, the upstream HTTP TCP bridge
(``envoy.upstreams.http.dynamic_modules``) keeps the connection out of the pool until that data is
written, for as long as each write makes progress within one second, and otherwise tries to reset
the connection, which only Linux supports. Such a drain counts in the cluster's
``upstream_cx_destroy_with_active_rq`` statistic, and in
``upstream_cx_destroy_local_with_active_rq`` or, when the upstream closes first,
``upstream_cx_destroy_remote_with_active_rq``, and the request stays in ``upstream_rq_active`` until
the connection closes. A close Envoy makes for another reason, such as when it shuts down, ends the
drain with a plain close.
