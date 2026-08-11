.. _config_http_cluster_specifier_priority_group:

Priority group cluster specifier
================================


Overview
--------

The priority group cluster specifier splits the candidate clusters of a route into a list of named
groups. The group is selected based on the attempt count of the request first and then the target
cluster is selected from the clusters of the group based on the cluster weights.

By default the initial attempt of a request uses the first group of the configured list, the first
retry uses the second group, and so on. If the attempt count exceeds the number of the configured
groups, the selection wraps around to the beginning of the list.

The groups can also be overridden on a per-request basis by an optional dynamic metadata entry
that contains a list of group overrides. Only the group ``name`` is supported in an override for
now, which selects one of the configured groups by name. See :ref:`group_override_metadata
<envoy_v3_api_field_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier.group_override_metadata>`
for more details.

Because the target cluster is re-selected for every attempt, this cluster specifier should be used
together with :ref:`refresh_cluster_on_retry
<envoy_v3_api_field_config.route.v3.RetryPolicy.refresh_cluster_on_retry>` to retry a request in a
different priority group.

Configuration
-------------

* This cluster specifier should be configured with the type URL ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier>`

Example configuration
---------------------

.. code-block:: yaml

  name: local_route
  virtual_hosts:
  - name: local_service
    domains: ["*"]
    routes:
    - match:
        prefix: "/"
      route:
        inline_cluster_specifier_plugin:
          extension:
            name: envoy.router.cluster_specifier_plugin.priority_group
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier
              priority_groups:
              - name: local
                clusters:
                - name: local_primary
                  weight: 80
                - name: local_secondary
                  weight: 20
              - name: remote
                clusters:
                - name: remote_primary
                  weight: 100
        retry_policy:
          retry_on: 5xx
          num_retries: 1
          refresh_cluster_on_retry: true

With the configuration above, the initial attempt is routed to ``local_primary`` or
``local_secondary`` (80/20 split), and the retry is routed to ``remote_primary``.
