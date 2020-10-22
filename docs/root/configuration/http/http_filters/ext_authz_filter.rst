.. _config_http_filters_ext_authz:

External Authorization
======================
* External authorization :ref:`architecture overview <arch_overview_ext_authz>`
* :ref:`HTTP filter v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`
* This filter should be configured with the name *envoy.filters.http.ext_authz*.

The external authorization filter calls an external gRPC or HTTP service to check whether an incoming
HTTP request is authorized or not.
If the request is deemed unauthorized, then the request will be denied normally with 403 (Forbidden) response.
Note that sending additional custom metadata from the authorization service to the upstream, to the downstream or to the authorization service is
also possible. This is explained in more details at :ref:`HTTP filter <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`.

The content of the requests that are passed to an authorization service is specified by
:ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>`.

.. _config_http_filters_ext_authz_http_configuration:

The HTTP filter, using a gRPC/HTTP service, can be configured as follows. You can see all the
configuration options at
:ref:`HTTP filter <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`.

Configuration Examples
----------------------

A sample filter configuration for a gRPC authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
        grpc_service:
          envoy_grpc:
            cluster_name: ext-authz

          # Default is 200ms; override if your server needs e.g. warmup time.
          timeout: 0.5s
        include_peer_certificate: true

.. code-block:: yaml

  clusters:
    - name: ext-authz
      type: static
      http2_protocol_options: {}
      load_assignment:
        cluster_name: ext-authz
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 10003

      # This timeout controls the initial TCP handshake timeout - not the timeout for the
      # entire request.
      connect_timeout: 0.25s

.. note::

  One of the features of this filter is to send HTTP request body to the configured gRPC
  authorization server as part of the :ref:`check request
  <envoy_v3_api_msg_service.auth.v3.CheckRequest>`.

  A sample configuration is as follows:

  .. code:: yaml

    http_filters:
      - name: envoy.filters.http.ext_authz
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
          grpc_service:
            envoy_grpc:
              cluster_name: ext-authz
          with_request_body:
            max_request_bytes: 1024
            allow_partial_message: true
            pack_as_bytes: true

  Please note that by default :ref:`check request<envoy_v3_api_msg_service.auth.v3.CheckRequest>`
  carries the HTTP request body as UTF-8 string and it fills the :ref:`body
  <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.body>` field. To pack the request
  body as raw bytes, it is needed to set :ref:`pack_as_bytes
  <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.BufferSettings.pack_as_bytes>` field to
  true. In effect to that, the :ref:`raw_body
  <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.raw_body>`
  field will be set and :ref:`body
  <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.body>` field will be empty.

A sample filter configuration for a raw HTTP authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
        http_service:
            server_uri:
              uri: 127.0.0.1:10003
              cluster: ext-authz
              timeout: 0.25s
              failure_mode_allow: false
        include_peer_certificate: true

.. code-block:: yaml

  clusters:
    - name: ext-authz
      connect_timeout: 0.25s
      type: logical_dns
      lb_policy: round_robin
      load_assignment:
        cluster_name: ext-authz
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 10003

Per-Route Configuration
-----------------------

A sample virtual host and route filter configuration.
In this example we add additional context on the virtual host, and disabled the filter for `/static` prefixed routes.

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      typed_per_filter_config:
        envoy.filters.http.ext_authz:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute
          check_settings:
            context_extensions:
              virtual_host: local_service
      routes:
      - match: { prefix: "/static" }
        route: { cluster: some_service }
        typed_per_filter_config:
          envoy.filters.http.ext_authz:
            "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute
            disabled: true
      - match: { prefix: "/" }
        route: { cluster: some_service }

Statistics
----------
.. _config_http_filters_ext_authz_stats:

The HTTP filter outputs statistics in the *cluster.<route target cluster>.ext_authz.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total responses from the filter.
  error, Counter, Total errors (including timeouts) contacting the external service.
  timeout, Counter, Total timeouts contacting the external service (only counted when timeout is measured when check request is created).
  denied, Counter, Total responses from the authorizations service that were to deny the traffic.
  disabled, Counter, Total requests that are allowed without calling external services due to the filter is disabled.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of failure_mode_allow set to true."

Dynamic Metadata
----------------
.. _config_http_filters_ext_authz_dynamic_metadata:

.. note::

  The External Authorization filter emits dynamic metadata only when it is configured to use
  gRPC service as the authorization server.

The External Authorization filter emits dynamic metadata as an opaque ``google.protobuf.Struct``
*only* when the gRPC authorization server returns a :ref:`CheckResponse
<envoy_v3_api_msg_service.auth.v3.CheckResponse>` with a filled :ref:`dynamic_metadata
<envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` field.

Runtime
-------
The fraction of requests for which the filter is enabled can be configured via the :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` value of the :ref:`filter_enabled
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.filter_enabled>` field.
