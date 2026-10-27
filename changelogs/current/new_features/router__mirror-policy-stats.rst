Added :ref:`stat_prefix <envoy_v3_api_field_config.route.v3.RouteAction.RequestMirrorPolicy.stat_prefix>`
to request mirror policies, at route, virtual host, route configuration and cluster level. When
set, the router emits per policy counters rooted at ``mirror.<stat_prefix>.`` for mirrors selected by the runtime fraction, mirrors skipped because the
mirror cluster is missing, mirrors started, and mirrors that ended with a 2xx response, with a
failure or non-2xx response, or were cancelled. See :ref:`mirror policy statistics
<config_http_filters_router_mirror_stats>`.
