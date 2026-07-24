Fixed :ref:`composite clusters <arch_overview_composite_cluster>` not retrying to the next
sub-cluster when a sub-cluster has no healthy upstream hosts available. Previously, the
:ref:`composite cluster docs <arch_overview_composite_cluster>` stated that a missing healthy host
would "potentially trigger another retry attempt if configured", but no ``retry_on`` condition
covered this case — the request failed immediately with ``503 no_healthy_upstream`` regardless of
retry policy. A new ``no-healthy-upstream`` :ref:`retry condition
<config_http_filters_router_x-envoy-retry-on>` has been added to enable this behavior explicitly.
