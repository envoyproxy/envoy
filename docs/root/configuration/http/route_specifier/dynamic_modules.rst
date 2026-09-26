.. _config_http_route_specifiers_dynamic_modules:

Dynamic modules route specifier
===============================

Overview
--------

The :ref:`DynamicModuleRouteSpecifier <envoy_v3_api_msg_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier>`
configuration specifies a :ref:`route specifier <config_http_conn_man_route_specifiers>` backed by a
:ref:`dynamic module <arch_overview_dynamic_modules>`. For each request the module keeps the route
that route matching resolved, refines it, replaces it with one of the route templates it declares,
or drops it.

The module is invoked while the route is being resolved, and again whenever the route is recomputed,
so it must be able to reach a decision from the request and the stream info alone. The call is
synchronous and cannot be time boxed, so a module must not block or perform I/O.

Route templates
---------------

A module cannot build a route out of thin air. Instead it selects one of the
:ref:`route_templates <envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.route_templates>`
the specifier declares. Each template is an ordinary
:ref:`Route <envoy_v3_api_msg_config.route.v3.Route>` that is built and validated once, when the
specifier is configured, so an invalid template is rejected at configuration load rather than on the
request path. A template inherits from the virtual host and the route configuration the specifier is
configured on, exactly like a configured route does.

When the module selects a template, Envoy evaluates it against the request like a configured route.
The :ref:`match <envoy_v3_api_field_config.route.v3.Route.match>` must hold, and the action is
resolved the usual way, including
:ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` and
:ref:`cluster specifier plugins <envoy_v3_api_field_config.route.v3.RouteAction.cluster_specifier_plugin>`.
A match that does not hold is a failure, because the rewrites of the action are only correct for a
request the match accepts.

Decisions
---------

The module returns one of five decisions:

* ``PassThrough`` uses the route the specifier was given, unchanged.
* ``Override`` uses that route with the properties the module recorded applied on top.
* ``SelectTemplate`` uses the selected template, with the recorded properties applied on top.
* ``NoRoute`` uses no route, so the request is handled as if nothing had matched.
* ``Error`` reports that the module could not decide.

A decision Envoy cannot honor is handled by the configured
:ref:`failure_policy <envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.failure_policy>`,
which either passes the request through to the route table or drops the route. The SDK reports an
error when the module panics, so a panic is handled by the same policy rather than crashing Envoy.

Shadowing a routing change
--------------------------

A module receives the route that route matching resolved through its context, so it can shadow a
routing change on its own without any support from Envoy. In a dry run the module computes the route
it would ask for, compares it against the resolved route, counts the outcome on its own metrics, and
returns ``PassThrough`` so that routing stays unchanged. Once the counts give confidence, the same
module returns ``Override`` or ``SelectTemplate`` to apply the decision. The
``route_specifier_shadow.rs`` test module under ``test/extensions/dynamic_modules/test_data/rust``
shows both runs, comparing the cluster name.

Route overrides
---------------

The properties that are built from other extensions, such as the retry policy and the request
mirroring policies, are declared as
:ref:`route_overrides <envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.route_overrides>`.
Each override is built and validated once when the specifier is configured, and the module selects
one by ``override_id``. An override that replaces no property is rejected, so a module can rely on a
declared override changing something.

Notes
-----

* The request headers are read-only. A recorded path, authority or header mutation is applied
  through the header transforms of the route the decision produces, so that Envoy applies it at the
  right point of the request lifetime.
* ``get_cluster_host_count`` reports whether a cluster is routable from the current worker and
  returns host counts at a priority level. It uses ``getThreadLocalCluster()``, so it can return
  false even when the cluster is configured but not yet warmed on the worker.
* Custom counters, gauges and histograms can be defined during configuration and recorded during
  resolution, and are emitted under the ``metrics_namespace`` prefix of ``DynamicModuleConfig``.

Statistics
----------

The specifier emits statistics rooted at ``<metrics_namespace>.route_specifier.<stat_prefix>.``,
where ``stat_prefix`` is the
:ref:`stat_prefix <envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.stat_prefix>`
of the specifier, sharing the ``metrics_namespace`` of the module-defined metrics above.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decision_pass_through, Counter, Requests for which the module kept the resolved route.
  decision_override, Counter, Requests for which the module refined the resolved route.
  decision_select_template, Counter, Requests for which the module selected a route template.
  decision_no_route, Counter, Requests for which the module dropped the route.
  decision_error, Counter, Requests for which the module could not decide.
  runtime_skipped, Counter, Requests outside ``runtime_fraction``.
  failure_module_error, Counter, Decisions not honored because the module reported an error.
  failure_template_not_selected, Counter, Decisions not honored because no known template was selected.
  failure_template_match_failed, Counter, Decisions not honored because the match of the selected template did not hold.
  failure_override_without_route, Counter, Decisions not honored because there was no route to refine.
  failure_override_on_non_route_entry, Counter, Decisions not honored because route entry properties were recorded for a direct response.
  failure_route_metadata, Counter, Decisions not honored because a typed metadata factory rejected the recorded metadata.
  on_route_duration, Histogram, Time in microseconds the module spent deciding.
  specifier_duration, Histogram, Time in microseconds the specifier spent on a request.

Configuration
-------------

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier>`

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
      route_specifiers:
      - name: envoy.router.route_specifiers.dynamic_modules
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier
          dynamic_module_config:
            name: my_route_specifier
            do_not_close: true
          specifier_name: my_specifier_impl
          stat_prefix: my_specifier
          failure_policy: PASS_THROUGH
          route_templates:
          - template_id: canary
            route:
              match:
                prefix: "/"
              route:
                cluster: canary_service
                timeout: 5s
      routes:
      - match:
          prefix: "/"
        route:
          cluster: web_service
