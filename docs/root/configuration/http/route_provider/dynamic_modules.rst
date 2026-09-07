.. _config_http_route_provider_dynamic_modules:

Dynamic modules route provider
==============================

Overview
--------

The :ref:`DynamicModuleRouteProvider <envoy_v3_api_msg_extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider>`
configuration specifies a route provider backed by a :ref:`dynamic module <arch_overview_dynamic_modules>`.
The route provider is configured on a :ref:`virtual host <envoy_v3_api_field_config.route.v3.VirtualHost.route_provider>`
in place of ``routes`` or ``matcher``. The module selects one of the route templates for a request
and may replace the cluster, the timeouts, the priority, the request body buffer limit, the route
metadata, the request and response headers, the path, the retry, hash, metadata match, request
mirroring and rate limit policies, the per-filter configuration and the route-level tracing of the
selected template, or resolve the request to a direct response or a redirect.

The module is invoked while the route is being resolved, so its selection is visible to the router
without clearing the route cache. Each route template is a full
:ref:`route <envoy_v3_api_msg_config.route.v3.Route>`, so every route field is reachable by selecting
the right template, and the per-request overrides layer onto the selected template. When the module
selects no route, Envoy resolves no route for the request.

The properties that are built from other extensions are declared as named
:ref:`route action overrides <envoy_v3_api_field_extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider.route_action_overrides>`.
Each override is built and validated once when the route provider is configured, so an invalid
override is rejected at configuration load rather than on the request path, and the module selects
one by name. An override that replaces no property is rejected too. Selecting a name that is not
declared leaves the route action properties of the selected template in effect and logs a warning.

The per-filter configuration and the route-level tracing are declared the same way as named
:ref:`per-route configuration overrides <envoy_v3_api_field_extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider.per_route_config_overrides>`.
The module selects one by name to overlay it onto the selected template, so a filter observes the
selected per-filter configuration while the properties the override leaves unset stay those of the
template. Like the route action overrides, they are built and validated at configuration load, an
override that replaces no property is rejected, and selecting a name that is not declared leaves the
configuration of the selected template in effect and logs a warning.

Configuration
-------------

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider>`

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
      route_provider:
        route_templates:
        - match:
            prefix: "/"
          route:
            cluster: default_cluster
        resolver:
          name: envoy.router.route_provider.dynamic_modules
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider
            dynamic_module_config:
              name: my_route_provider
              do_not_close: true
            provider_name: my_provider_impl
            provider_config:
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
            per_route_config_overrides:
              ui:
                typed_per_filter_config:
                  envoy.filters.http.ext_authz:
                    "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute
                    disabled: true
