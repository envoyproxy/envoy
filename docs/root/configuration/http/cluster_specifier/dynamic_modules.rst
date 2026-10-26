.. _config_http_cluster_specifier_dynamic_modules:

Dynamic modules cluster specifier
=================================

Overview
--------

The :ref:`DynamicModuleClusterSpecifier <envoy_v3_api_msg_extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier>`
configuration specifies a cluster specifier backed by a :ref:`dynamic module <arch_overview_dynamic_modules>`.
The module selects the upstream cluster for a request and may replace the timeout, idle timeout,
priority, request body buffer limit, cluster not found response code, hash policy, retry policy,
metadata match criteria and request mirroring policies of the matched route.

The module is invoked while the route is being resolved, so its selection is visible to the router
without clearing the route cache. It is invoked again whenever a filter calls
:ref:`refreshRouteCluster() <arch_overview_http_filters_route_mutation>` or a retry re-selects the
cluster, so the module must be able to reach a decision from the request headers and the stream info
alone. When the module reports no decision, the properties of the matched route stay in effect.

The properties that are built from other extensions are declared as named
:ref:`route action overrides <envoy_v3_api_field_extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier.route_action_overrides>`.
Each override is built and validated once when the cluster specifier is configured, so an invalid
override is rejected at configuration load rather than on the request path, and the module selects
one by name. An override that replaces no property is rejected too, so a module can rely on a
declared name changing something. Selecting a name that is not declared leaves the route action
properties of the matched route in effect and logs a warning. When
:ref:`validate_clusters <envoy_v3_api_field_config.route.v3.RouteConfiguration.validate_clusters>`
is enabled, the statically named mirror clusters of every override are checked against the cluster
manager.

Not every property can be changed once the request is under way, because Envoy reads each of them at
a different point:

* The cluster name and the priority are read on every upstream attempt, so a later selection
  replaces them.
* The metadata match criteria are read on every upstream attempt as well, but Envoy merges any
  ``envoy.lb`` dynamic metadata over them and caches the result, so a later selection cannot replace
  them once that has happened.
* The idle timeout and the request body buffer limit are read while the route is resolved, and the
  timeout, the retry policy and the request mirroring policies are read before the first upstream
  attempt, so only the first selection applies to them.
* The hash policy is read during host selection on every upstream attempt, so a later selection
  replaces it. A cluster-level hash policy and a load-balancer-level hash policy, when configured,
  take precedence over the route-level policy the module selects.
* The cluster not found response code is read only when the selected cluster does not exist, which
  ends the request, so the selection that named the missing cluster is the one that applies.
* ``get_cluster_host_count`` reports whether a cluster is routable from the current worker and
  returns host counts at a priority level. It uses ``getThreadLocalCluster()``, so it can return
  false even when the cluster is configured but not yet warmed on the worker.
* Custom counters, gauges and histograms can be defined during configuration and recorded during
  selection, and are emitted under the ``metrics_namespace`` prefix of ``DynamicModuleConfig``.

Configuration
-------------

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier>`

.. attention::

   Dynamic modules run in-process with the same privileges as Envoy. Only load modules you trust.
   This extension is currently under active development. Capabilities and ABI are expected to
   evolve.

Configuration example
---------------------

.. code-block:: yaml

  route_config:
    virtual_hosts:
    - name: default
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        route:
          inline_cluster_specifier_plugin:
            extension:
              name: envoy.router.cluster_specifier_plugin.dynamic_modules
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier
                dynamic_module_config:
                  name: my_cluster_specifier
                  do_not_close: true
                specifier_name: my_specifier_impl
                specifier_config:
                  "@type": type.googleapis.com/google.protobuf.Struct
                  value:
                    cluster_name_prefix: shard-
                route_action_overrides:
                  canary:
                    retry_policy:
                      retry_on: 5xx
                      num_retries: 3
                    metadata_match:
                      filter_metadata:
                        envoy.lb:
                          version: canary
