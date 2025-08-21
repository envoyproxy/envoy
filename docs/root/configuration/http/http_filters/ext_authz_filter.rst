.. _config_http_filters_ext_authz:

External Authorization
======================
* External authorization :ref:`architecture overview <arch_overview_ext_authz>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`

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

.. _config_http_filters_ext_authz_security_considerations:

Security Considerations
-----------------------

.. attention::

   **Route Cache Clearing Risk**: When using per-route ExtAuthZ configuration, subsequent filters
   in the filter chain may clear the route cache, potentially leading to privilege escalation
   vulnerabilities where requests bypass authorization checks.

   For more information about this security risk, including affected filters and general
   mitigation strategies, see :ref:`Filter route mutation security considerations
   <arch_overview_http_filters_route_mutation>`.

   **ExtAuthZ-Specific Considerations**: The security risk is particularly important for ExtAuthZ
   because it often handles authentication and authorization decisions that directly impact access
   control. When route cache is cleared after ExtAuthZ has run, a request may be re-routed to
   endpoints with different authorization requirements, bypassing those checks entirely.

   **Example Vulnerable Configuration**:

   .. code-block:: yaml

      http_filters:
      - name: envoy.filters.http.ext_authz
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
          # ... ext_authz config ...
      - name: envoy.filters.http.lua
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
          inline_code: |
            function envoy_on_request(request_handle)
              -- This clears route cache after ext_authz has run
              request_handle:clearRouteCache()
              -- Request may now match a different route with different auth requirements
            end

   In this example, if the initial route had ExtAuthZ disabled but a subsequent route match
   (after cache clearing) requires authorization, the request will bypass the authorization
   check entirely.

Configuration Examples
----------------------

A sample filter configuration for a gRPC authorization server:

.. literalinclude:: _include/ext-authz-grpc-filter.yaml
    :language: yaml
    :lines: 26-35
    :lineno-start: 26
    :linenos:
    :caption: :download:`ext-authz-grpc-filter.yaml <_include/ext-authz-grpc-filter.yaml>`

.. literalinclude:: _include/ext-authz-grpc-filter.yaml
    :language: yaml
    :lines: 41-56
    :lineno-start: 41
    :linenos:
    :caption: :download:`ext-authz-grpc-filter.yaml <_include/ext-authz-grpc-filter.yaml>`

.. note::

  One of the features of this filter is to send HTTP request body to the configured gRPC
  authorization server as part of the :ref:`check request
  <envoy_v3_api_msg_service.auth.v3.CheckRequest>`.

  A sample configuration is as follows:

  .. literalinclude:: _include/ext-authz-grpc-body-filter.yaml
      :language: yaml
      :lines: 26-36
      :lineno-start: 26
      :linenos:
      :caption: :download:`ext-authz-grpc-body-filter.yaml <_include/ext-authz-grpc-body-filter.yaml>`

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

.. literalinclude:: _include/ext-authz-http-filter.yaml
    :language: yaml
    :lines: 26-36
    :lineno-start: 26
    :linenos:
    :caption: :download:`ext-authz-http-filter.yaml <_include/ext-authz-http-filter.yaml>`

.. literalinclude:: _include/ext-authz-http-filter.yaml
    :language: yaml
    :lines: 41-53
    :lineno-start: 41
    :linenos:
    :caption: :download:`ext-authz-http-filter.yaml <_include/ext-authz-http-filter.yaml>`

Per-Route Configuration
-----------------------

.. literalinclude:: _include/ext-authz-routes-filter.yaml
    :language: yaml
    :lines: 15-38
    :lineno-start: 15
    :linenos:
    :caption: :download:`ext-authz-routes-filter.yaml <_include/ext-authz-routes-filter.yaml>`

A sample virtual host and route filter configuration.
In this example we add additional context on the virtual host, and disabled the filter for ``/static`` prefixed routes.

Statistics
----------
.. _config_http_filters_ext_authz_stats:

The HTTP filter outputs statistics in the ``cluster.<route target cluster>.ext_authz.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total responses from the filter.
  error, Counter, Total errors contacting the external service.
  denied, Counter, Total responses from the authorizations service that were to deny the traffic.
  disabled, Counter, Total requests that are allowed without calling external services due to the filter is disabled.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of failure_mode_allow set to true."

Dynamic Metadata
----------------
.. _config_http_filters_ext_authz_dynamic_metadata:

The External Authorization filter supports emitting dynamic metadata as an opaque ``google.protobuf.Struct``.

When using a gRPC authorization server, dynamic metadata will be emitted only when the :ref:`CheckResponse
<envoy_v3_api_msg_service.auth.v3.CheckResponse>` contains a non-empty :ref:`dynamic_metadata
<envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` field.

When using an HTTP authorization server, dynamic metadata will be emitted only when there are response headers
from the authorization server that match the configured
:ref:`dynamic_metadata_from_headers <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.dynamic_metadata_from_headers>`,
if set. For every response header that matches, the filter will emit dynamic metadata whose key is the name of the matched header and whose value is the value of the matched header.

Both the HTTP and gRPC external authorization filters support a dynamic metadata field called ``ext_authz_duration`` which records the time it takes to complete an authorization request in milliseconds.
This field will not be populated if the request does not complete.

Runtime
-------
The fraction of requests for which the filter is enabled can be configured via the :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` value of the :ref:`filter_enabled
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.filter_enabled>` field.

Tracing
-------
The ext_authz span keeps the sampling status of the parent span, i.e. in the tracing backend we will either see both the parent span and the child ext_authz span, or none of them.

Logging
-------
When :ref:`emit_filter_state_stats <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.emit_filter_state_stats>` is set to true,
ext_authz exposes fields ``latency_us``, ``bytesSent`` and ``bytesReceived`` for usage in CEL and logging.
* ``filter_state["envoy.filters.http.ext_authz"].latency_us)``
* ``%FILTER_STATE(envoy.filters.http.ext_authz:FIELD:latency_us)%``
