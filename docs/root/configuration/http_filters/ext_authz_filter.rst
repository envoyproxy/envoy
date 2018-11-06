.. _config_http_filters_ext_authz:

External Authorization
======================
* External authorization :ref:`architecture overview <arch_overview_ext_authz>`
* :ref:`HTTP filter v2 API reference <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`

The external authorization HTTP filter calls an external gRPC or HTTP service to check if the incoming
HTTP request is authorized or not.
If the request is deemed unauthorized then the request will be denied normally with 403 (Forbidden) response.
Note that sending additional custom metadata from the authorization service to the upstream, to the downstream or to the authorization service is
also possible. This is explained in more details at :ref:`HTTP filter <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`.

The content of the requests that are passed to an authorization service is specified by
:ref:`CheckRequest <envoy_api_msg_service.auth.v2alpha.CheckRequest>`.

.. _config_http_filters_ext_authz_http_configuration:

The HTTP filter, using a gRPC/HTTP service, can be configured as follows. You can see all the
configuration options at
:ref:`HTTP filter <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`.

Configuration Examples
-----------------------------

A sample filter configuration for a gRPC authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.ext_authz
      config:
        grpc_service:
          envoy_grpc:
            cluster_name: ext-authz

          # Default is 200ms; override if your server needs e.g. warmup time.
          timeout: 0.5s

.. code-block:: yaml

  clusters:
    - name: ext-authz
      type: static
      http2_protocol_options: {}
      hosts:
        - socket_address: { address: 127.0.0.1, port_value: 10003 }

      # This timeout controls the initial TCP handshake timeout - not the timeout for the
      # entire request.
      connect_timeout: 0.25s

A sample filter configuration for a raw HTTP authorization server:

.. code-block:: yaml

  http_filters:
    - name: envoy.ext_authz
      config:
        http_service:
            server_uri:
              uri: 127.0.0.1:10003
              cluster: ext-authz
              timeout: 0.25s
              failure_mode_allow: false

.. code-block:: yaml

  clusters:
    - name: ext-authz
      connect_timeout: 0.25s
      type: logical_dns
      lb_policy: round_robin
      hosts:
        - socket_address: { address: 127.0.0.1, port_value: 10003 }

Statistics
----------
The HTTP filter outputs statistics in the *cluster.<route target cluster>.ext_authz.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total responses from the filter.
  error, Counter, Total errors contacting the external service.
  denied, Counter, Total responses from the authorizations service that were to deny the traffic.
  failure_mode_allowed, Counter, "Total requests that were error(s) but were allowed through because
  of failure_mode_allow set to true."
