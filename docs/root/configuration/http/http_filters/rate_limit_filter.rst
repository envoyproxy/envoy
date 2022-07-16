.. _config_http_filters_rate_limit:

Rate limit
==========

* Global rate limiting :ref:`architecture overview <arch_overview_global_rate_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>`

The HTTP rate limit filter will call the rate limit service when the request's route or virtual host
has one or more :ref:`rate limit configurations<envoy_v3_api_field_config.route.v3.VirtualHost.rate_limits>`
that match the filter stage setting. The :ref:`route<envoy_v3_api_field_config.route.v3.RouteAction.include_vh_rate_limits>`
can optionally include the virtual host rate limit configurations. More than one configuration can
apply to a request. Each configuration results in a descriptor being sent to the rate limit service.

If the rate limit service is called, and the response for any of the descriptors is over limit, a
429 response is returned (the response is configurable via :ref:`rate_limited_status <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limited_status>`). The rate limit filter also sets the :ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` header,
unless :ref:`disable_x_envoy_ratelimited_header <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.disable_x_envoy_ratelimited_header>` is
set to true.

If there is an error in calling rate limit service or rate limit service returns an error and :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.failure_mode_deny>` is
set to true, a 500 response is returned.

.. _config_http_filters_rate_limit_composing_actions:

Composing Actions
-----------------

Each :ref:`rate limit action <envoy_v3_api_msg_config.route.v3.RateLimit>` on the route or
virtual host populates a descriptor entry. A vector of descriptor entries compose a descriptor. To
create more complex rate limit descriptors, actions can be composed in any order. The descriptor
will be populated in the order the actions are specified in the configuration.

Example 1
^^^^^^^^^

For example, to generate the following descriptor:

.. code-block:: cpp

  ("generic_key", "some_value")
  ("source_cluster", "from_cluster")

The configuration would be:

.. code-block:: yaml

  actions:
      - {source_cluster: {}}
      - {generic_key: {descriptor_value: some_value}}

Example 2
^^^^^^^^^

If an action doesn't append a descriptor entry, no descriptor is generated for
the configuration.

For the following configuration:

.. code-block:: yaml

  actions:
      - {source_cluster: {}}
      - {remote_address: {}}
      - {generic_key: {descriptor_value: some_value}}


If a request did not set :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>`,
no descriptor is generated.

If a request sets :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>`, the
the following descriptor is generated:

.. code-block:: cpp

  ("generic_key", "some_value")
  ("remote_address", "<trusted address from x-forwarded-for>")
  ("source_cluster", "from_cluster")

.. _config_http_filters_rate_limit_rate_limit_override:

Rate Limit Override
-------------------

A :ref:`rate limit action <envoy_v3_api_msg_config.route.v3.RateLimit>` can optionally contain
a :ref:`limit override <envoy_v3_api_msg_config.route.v3.RateLimit.Override>`. The limit value
will be appended to the descriptor produced by the action and sent to the ratelimit service,
overriding the static service configuration.

The override can be configured to be taken from the :ref:`Dynamic Metadata
<envoy_v3_api_msg_config.core.v3.Metadata>` under a specified
:ref:`key <envoy_v3_api_msg_type.metadata.v3.MetadataKey>`.
If the value is misconfigured or key does not exist, the override configuration is ignored.

Example 3
^^^^^^^^^

The following configuration

.. code-block:: yaml

  actions:
      - {generic_key: {descriptor_value: some_value}}
  limit:
     metadata_key:
         key: test.filter.key
         path:
             - key: test

.. _config_http_filters_rate_limit_override_dynamic_metadata:

Will lookup the value of the dynamic metadata. The value must be a structure with integer field
"requests_per_unit" and a string field "unit" which is parseable to :ref:`RateLimitUnit enum
<envoy_v3_api_enum_type.v3.RateLimitUnit>`. For example, with the following dynamic metadata
the rate limit override of 42 requests per hour will be appended to the rate limit descriptor.

.. code-block:: yaml

  test.filter.key:
      test:
          requests_per_unit: 42
          unit: HOUR

Descriptor extensions
---------------------

Rate limit descriptors are extensible with custom descriptors. For example, :ref:`computed descriptors
<envoy_v3_api_msg_extensions.rate_limit_descriptors.expr.v3.Descriptor>` extension allows using any of the
:ref:`request attributes <arch_overview_request_attributes>` as a descriptor value:

.. code-block:: yaml

  actions:
      - extension:
            name: custom
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.rate_limit_descriptors.expr.v3.Descriptor
              descriptor_key: my_descriptor_name
              text: request.method

:ref:`HTTP matching input functions <arch_overview_matching_api>` are supported as descriptor producers:

.. code-block:: yaml

  actions:
      - extension:
            name: custom
            typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: x-header-name

The above example produces an entry with the key ``custom`` and the value of
the request header ``x-header-name``. If the header is absent, then the
descriptor entry is not produced, and no descriptor is generated. If the header
value is present but is an empty string, then the descriptor is generated but
no entry is added.

Statistics
----------

The rate limit filter outputs statistics in the *cluster.<route target cluster>.ratelimit.* namespace.
429 responses or the configured :ref:`rate_limited_status <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limited_status>` are emitted to the normal cluster :ref:`dynamic HTTP statistics
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total under limit responses from the rate limit service
  error, Counter, Total errors contacting the rate limit service
  over_limit, Counter, total over limit responses from the rate limit service
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.failure_mode_deny>` set to false."

Dynamic Metadata
----------------
.. _config_http_filters_ratelimit_dynamic_metadata:

The ratelimit filter emits dynamic metadata as an opaque ``google.protobuf.Struct``
*only* when the gRPC ratelimit service returns a :ref:`RateLimitResponse
<envoy_v3_api_msg_service.ratelimit.v3.RateLimitResponse>` with a filled :ref:`dynamic_metadata
<envoy_v3_api_field_service.ratelimit.v3.RateLimitResponse.dynamic_metadata>` field.

Runtime
-------

The HTTP rate limit filter supports the following runtime settings:

ratelimit.http_filter_enabled
  % of requests that will call the rate limit service. Defaults to 100.

ratelimit.http_filter_enforcing
  % of requests that that will have the rate limit service decision enforced. Defaults to 100.
  This can be used to test what would happen before fully enforcing the outcome.

ratelimit.<route_key>.http_filter_enabled
  % of requests that will call the rate limit service for a given *route_key* specified in the
  :ref:`rate limit configuration <envoy_v3_api_msg_config.route.v3.RateLimit>`. Defaults to 100.
