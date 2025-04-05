.. _arch_overview_load_balancing_override_host:

Override host
=============

Load balancing algorithms (round robin, random, etc.) are used to select upstream hosts by default.
Also, Envoy supports to select a list of specified addresses directly and bypass the load balancing
algorithms. This feature is called override host.


The :ref:`override host policy <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.override_host_policy>`
is used to extract the override host addresses from the downstream request attributes.


If a valid override host address is specified and the corresponding upstream
host has the
:ref:`expected health status <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.override_host_status>`,
that upstream host will be selected preferentially and the load balancing algorithms will be ignored.
If no override host is specified or the override host is not valid, the load balancing algorithms will be used
to select an upstream host.
