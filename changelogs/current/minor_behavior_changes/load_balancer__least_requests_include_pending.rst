The :ref:`least request <arch_overview_load_balancing_types_least_request>` load balancer can
now include pending requests (streams queued on a connection pool waiting for a connection to
establish) in the per-host load value used for selection. This is disabled by default and can
be enabled by setting runtime guard
``envoy.reloadable_features.least_request_lb_count_pending_requests`` to ``true``.
Also adds a new ``cluster.<name>.endpoint.<addr>.rq_pending_active`` per-endpoint gauge.
