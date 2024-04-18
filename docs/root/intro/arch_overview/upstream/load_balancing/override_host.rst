.. _arch_overview_load_balancing_override_host:

Override host
=============

Load balancing algorithms (round robin, random, etc.) are used to select upstream hosts by default.
Also, Envoy supports overriding the results of the load balancing algorithms by specifying a valid
override host address. If a valid override host address is specified and the corresponding upstream
host has the
:ref:`expected health status <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.override_host_status>`,
that upstream host will be selected preferentially.

For example, :ref:`stateful session filter <config_http_filters_stateful_session>` will specify
override host address directly based on the downstream request attributes. Then the results of load
balancing algorithms will be ignored. By this way, stateful session stickiness can be achieved.

In summary, override host provides a mechanism by which L4/L7 extensions can influence the final
results of upstream load balancing. This mechanism can be used by different extensions in different
scenarios.
